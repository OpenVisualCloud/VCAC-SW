/*
 * Intel VCA Software Stack (VCASS)
 *
 * Copyright(c) 2016-2017 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Intel VCA Block IO driver
 */
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/kthread.h>
#include <linux/miscdevice.h>


#ifdef TEST_BUILD
#include "vcablk_test_hw_ops.h"
#else
#include "plx_hw_ops_blockio.h"
#endif

#include "vcablk_common/vcablk_common.h"
#include "vcablk_bcknd_hal.h"
#include "vcablk_bcknd_disk.h"
#include "vcablk_bcknd_ioctl.h"
#include "vcablk_bcknd_media.h"

MODULE_LICENSE("GPL");

#define VCA_BLK_BCKND_MAGIC 0xB10CBACE

#define TIMEOUT_CLOSE_MS 5000
static ushort vcablk_bcknd_max_dev = 8;
module_param(vcablk_bcknd_max_dev, ushort, S_IRUGO);
MODULE_PARM_DESC(vcablk_bcknd_max_dev, "Maximum number of vca block devices");

static long vcablk_bcknd_ioctl (struct file *file, unsigned int cmd,
		unsigned long parm);
static void vcablk_bcknd_set_dev_page(struct vcablk_bcknd_dev * bdev,
		pci_addr dev_pages_phy);
int vcablk_bcknd_add_to_devpage(struct vcablk_bcknd_dev *bdev,
		struct vcablk_dev_page *dev_page,
		struct vcablk_bcknd_disk *bckd);

static const struct file_operations bcknd_ctl_fops = {
	.open		= nonseekable_open,
	.unlocked_ioctl	= vcablk_bcknd_ioctl,
	.compat_ioctl	= vcablk_bcknd_ioctl,
	.owner		= THIS_MODULE,
	.llseek		= noop_llseek,
};

static int
vcablk_bcknd_try_stop_frontend(struct vcablk_bcknd_dev * bdev, int disk_id, bool force)
{
	int err = 0;
	struct vcablk_bcknd_disk *bckd = bdev->device_array[disk_id];
	pr_debug("%s\n", __func__);

	/* Check that device created */
	if (!bckd) {
		err = -ENODEV;
		goto exit;
	}

	if (vcablk_bcknd_disk_get_state(bckd) == DISK_STATE_OPEN) {
		/* Try stop frontend */
		if (bdev->dev_pages_remapped) {
			struct vcablk_device_ctrl *devp =
					&bdev->dev_pages_remapped->devs[disk_id];
			if (bdev->dev_pages_remapped->devs[disk_id].command)
				pr_warning("%s %s: TASK %u IN PROGRESS DISK_ID %i!!!\n",
						__func__, bdev->dev_name,
						bdev->dev_pages_remapped->devs[disk_id].command,
						disk_id);

			devp->response.status = 0;
			wmb();
			devp->command = force?CMD_STOP_FORCE:CMD_STOP_NORMAL;
			wmb();
			bdev->hw_ops->send_intr(bdev->mdev.parent,
					bdev->dev_pages_remapped->notify_frontend_db);

			/* Wait for answer frontend stop. */
			if (!wait_event_interruptible_timeout(
					*vcablk_bcknd_disk_get_probe_queue(bckd),
					devp->response.command_ack,
					msecs_to_jiffies(TIMEOUT_CLOSE_MS))) {
				pr_err("%s %s: Wait queue TIMEOUT DISK_ID %i!!!\n",
						__func__, bdev->dev_name, disk_id);
				err = -ENODEV;
			} else {
				err = devp->response.status,
				devp->response.command_ack = 0;
			}

		} else {
			pr_err("%s %s: dev_page is null, nothing to stop!!!\n",
					__func__, bdev->dev_name);
		}
	} else {
		pr_err("%s %s: frontend not working, nothing to stop!!!\n",
				__func__, bdev->dev_name);
	}

exit:
	pr_debug(KERN_INFO "%s err: %i\n", __func__, err);
	return err;

}

static int
vcablk_bcknd_try_destroy_frontend(struct vcablk_bcknd_dev * bdev, int disk_id)
{
	int err = 0;
	struct vcablk_bcknd_disk *bckd = bdev->device_array[disk_id];
	pr_debug("%s\n", __func__);

	/* Check that device created */
	if (!bckd) {
		err = -ENODEV;
		goto exit;
	}

	/* Try stop frontend */
	if (bdev->dev_pages_remapped) {
		struct vcablk_device_ctrl *devp = &bdev->dev_pages_remapped->devs[disk_id];
		if (bdev->dev_pages_remapped->devs[disk_id].command)
			pr_warning("%s %s: TASK %u IN PROGRESS DISK_ID %i!!!\n", __func__,
					bdev->dev_name,
					bdev->dev_pages_remapped->devs[disk_id].command, disk_id);

		pr_debug("%s %s %i\n", __func__, bdev->dev_name,__LINE__);
		devp->response.status = 0;
		devp->response.command_ack = 0;
		wmb();
		devp->command = CMD_DESTROY;
		wmb();
		bdev->hw_ops->send_intr(bdev->mdev.parent,
				bdev->dev_pages_remapped->notify_frontend_db);
		pr_debug("%s %i %s\n", __func__, __LINE__, bdev->dev_name);
		/* STOP ALL QUEUE */
		if (!wait_event_interruptible_timeout(
				*vcablk_bcknd_disk_get_probe_queue(bckd),
				devp->response.command_ack,
				msecs_to_jiffies(TIMEOUT_CLOSE_MS))) {
			pr_err("%s %s: Wait queue TIMEOUT DISK_ID %i!!!\n",
					__func__, bdev->dev_name, disk_id);
			err = -ENODEV;
		} else {
			err = devp->response.status,
			devp->response.command_ack = 0;
		}

		pr_debug("%s %i  %s\n", __func__,__LINE__, bdev->dev_name);
		if (vcablk_bcknd_disk_get_state(bckd) != DISK_STATE_CREATE) {
			err = -EIO;
			pr_err("%s %s: Disk %i state not closed!!!\n",
					__func__, bdev->dev_name, disk_id);

		}
	}

exit:
	pr_debug("%s err: %i\n", __func__, err);
	return err;

}

static int
vcablk_bcknd_destroy(struct vcablk_bcknd_dev * bdev, int dev_id, bool force)
{
	int err = 0;
	struct vcablk_bcknd_disk *bckd = bdev->device_array[dev_id];

	/* Check that device created */
	if (!bckd) {
		return -ENODEV;
	}

	pr_info("%s %s dev_id %i\n", __func__, bdev->dev_name, dev_id);

	if (DISK_STATE_OPEN == vcablk_bcknd_disk_get_state(bckd)) {
		err = vcablk_bcknd_try_stop_frontend(bdev, dev_id, force);
		/* TODO: For local, if backend BUSY host can crash*/
		if (!force && err == -EBUSY) {
			pr_err("%s %s: Device %i is in use!\n",
					__func__, bdev->dev_name, dev_id);
			return err;
		}
	}
	err = vcablk_bcknd_try_destroy_frontend(bdev, dev_id);

	vcablk_bcknd_disk_destroy(bckd);
	bckd = NULL;
	bdev->device_array[dev_id] = NULL;

	return 0;
}

static irqreturn_t
vcablk_bcknd_ftb_irq(int irq, void *data)
{
	struct vcablk_bcknd_dev *bdev = (struct vcablk_bcknd_dev *)data;
	pr_debug("%s %s: Probe ACK IRQ\n", __func__, bdev->dev_name);
	schedule_work(&bdev->work_ftb);

	bdev->hw_ops->ack_interrupt(bdev->mdev.parent, bdev->ftb_db);

	return IRQ_HANDLED;
}

/*
 * Set up our internal device.
 * size - Size of device in bytes
 */
int
vcablk_bcknd_create(struct vcablk_bcknd_dev *bdev,
		struct vcablk_disk_open_desc *desc)
{
	int err = 0;
	struct vcablk_bcknd_disk *bckd= NULL;
	int disk_id = desc->disk_id;

	pr_debug("%s  %s disk_id %i\n", __func__, bdev->dev_name, disk_id);

	if (disk_id < 0 || disk_id >= vcablk_bcknd_max_dev) {
		pr_err("%s %s: Wrong ID %i\n", __func__, bdev->dev_name, disk_id);
		err = -EINVAL;
		goto err;
	}

	if (bdev->device_array[disk_id]) {
		pr_err("%s %s: Disk with ID %i already exist\n",
				__func__, bdev->dev_name, disk_id);
		err = -EBUSY;
		goto err;
	}

	bckd = vcablk_bcknd_disk_create(bdev, desc);

	if (IS_ERR(bckd)) {
		err = PTR_ERR(bckd);
		pr_err("%s %s: device create id %i err  %i\n",
				__func__, bdev->dev_name, disk_id, err);
		goto err;
	}

	bdev->device_array[disk_id] = bckd;

	/* TODO: Add when frontend exist, need add mutex */
	if (bdev->dev_pages_remapped) {
		err = vcablk_bcknd_add_to_devpage(bdev, bdev->dev_pages_remapped, bckd);
		if (err) {
			goto err;
		}
		wmb();
		bdev->hw_ops->send_intr(bdev->mdev.parent,
				bdev->dev_pages_remapped->notify_frontend_db);
	} else {
		pr_err("%s %s: Wait for dev_page!!!\n", __func__, bdev->dev_name);
	}

err:
	return err;
}

static void
vcablk_bcknd_ftb_work(struct work_struct *work)
{
	struct vcablk_bcknd_dev *bdev =
			container_of(work, struct vcablk_bcknd_dev, work_ftb);
	struct vcablk_dev_page *dev_pages = bdev->dev_pages_remapped;
	int dev_id;
	u64 dp_addr = bdev->hw_ops->get_dp_addr(bdev->mdev.parent, false);
	u32 num_devices;

	if (bdev->dp_addr && dp_addr != bdev->dp_addr) {
		pr_info("%s %s: Changed physical dev paged\n", __func__, bdev->dev_name);
		vcablk_bcknd_set_dev_page(bdev, 0);
		dev_pages = NULL;
	} else if (dev_pages) {
		if (dev_pages->version_frontend != VCA_BLK_VERSION ||
			dev_pages->valid_magic_bcknd != VCA_BLK_BCKND_MAGIC ) {
			pr_err("%s %s: Dev page: INVALID 0x%p\n", __func__, bdev->dev_name, dev_pages);
			vcablk_bcknd_set_dev_page(bdev, 0);
			dev_pages = NULL;
			bdev->hw_ops->get_dp_addr(bdev->mdev.parent, true);
		} else {

			if (dev_pages->flags_events & VCA_FLAG_EVENTS_FRONTEND_DUMP_START) {
				dev_pages->flags_events &= ~(VCA_FLAG_EVENTS_FRONTEND_DUMP_START);
				pr_err("%s %s: Detect CRASH DUMP for \"%s\" start...\n", __func__,
						bdev->dev_name, bdev->frontend_name);
				/* Stop network communications! to protect dma_hang after reset node */
				if (bdev->hw_ops->stop_network_traffic) {
					bdev->hw_ops->stop_network_traffic(bdev->mdev.parent);
				}
			}

			if (dev_pages->flags_events & VCA_FLAG_EVENTS_FRONTEND_DUMP_END) {
				dev_pages->flags_events &= ~(VCA_FLAG_EVENTS_FRONTEND_DUMP_END);
				pr_err("%s %s: Detect CRASH DUMP for \"%s\" end [OK]\n", __func__,
						bdev->dev_name, bdev->frontend_name);
			}

			if (dev_pages->destroy_devpage == DESTROY_DEV_PAGE_INIT) {
				int ack_db = dev_pages->notify_frontend_db;
				pr_info("%s %s: Dev page -> DESTROY DEVPAGE=%d\n", __func__, bdev->dev_name,
						dev_pages->destroy_devpage);
				dev_pages->destroy_devpage = DESTROY_DEV_PAGE_END;
				vcablk_bcknd_set_dev_page(bdev, 0);
				dev_pages = NULL;
				bdev->hw_ops->get_dp_addr(bdev->mdev.parent, true);
				bdev->hw_ops->send_intr(bdev->mdev.parent, ack_db);
			}
		}
	}

	if (dp_addr) {
		/* dp will be reloaded if the adress changed */
		vcablk_bcknd_set_dev_page(bdev, dp_addr);
		/* Updated dev_pages */
		dev_pages = bdev->dev_pages_remapped;
		if (!dev_pages) {
			pr_err("%s %s: Can not open Dev_page 0x%llx\n", __func__, bdev->dev_name, dp_addr);
			bdev->hw_ops->get_dp_addr(bdev->mdev.parent, true);
		}
	}

	if (!dev_pages) {
		pr_debug("%s %s: Dev page is NULL\n", __func__, bdev->dev_name);
		return;
	}

	num_devices = dev_pages->num_devices;
	if (num_devices > vcablk_bcknd_max_dev) {
		pr_warning("%s %s: Dev page 0x%p, num of devices %u more that maximum %u\n"
				, __func__, bdev->dev_name, dev_pages, num_devices, vcablk_bcknd_max_dev);
		num_devices = vcablk_bcknd_max_dev;
	}

	for (dev_id=0; dev_id < num_devices; ++dev_id) {
		struct vcablk_device_response *response =
				&dev_pages->devs[dev_id].response;

		if (response->command_ack) {
			int err = response->status;

			if (err) {
				pr_err("%s %s: Probe ACK dev_id %i task %u err %i\n",
						__func__, bdev->dev_name, dev_id, response->command_ack, err);
			} else {
				pr_debug("%s %s: Probe ACK dev_id %i task %u err %i\n",
						__func__, bdev->dev_name, dev_id, response->command_ack, err);
			}

			if (bdev->device_array[dev_id]) {
				struct vcablk_bcknd_disk *bckd= bdev->device_array[dev_id];

				switch (response->command_ack) {
				case CMD_CREATE:
					response->command_ack = 0;

					if (!err)
						err = vcablk_bcknd_disk_start(bckd, response->done_db,
							response->request_ring, response->request_ring_size,
							response->completion_ring, response->completion_ring_size);

					if (!err) {
						/* Backend have to be ready, and can not wait on
						 *  ACK to start work */
						wmb();
						dev_pages->devs[dev_id].command = CMD_RUN;
						wmb();
						bdev->hw_ops->send_intr(bdev->mdev.parent,
								bdev->dev_pages_remapped->notify_frontend_db);
					} else {
						pr_err("%s %s: Can not create frontend device, "
								"err: %i\n", __func__, bdev->dev_name, err);
					}
					break;
				case CMD_RUN:
					response->command_ack = 0;

					if (err) {
						pr_err("%s %s: Can not RUN frontend device\n",
								__func__, bdev->dev_name);
						break;
					}
					break;

				case CMD_STOP_NORMAL:
				case CMD_STOP_FORCE:
					if (!bckd) {
						pr_err("%s %s: Backend not exist!!\n", __func__, bdev->dev_name);
						break;
					}
					wake_up_all(vcablk_bcknd_disk_get_probe_queue(bckd));
					break;

				case CMD_DESTROY:
					if (!bckd) {
						pr_err("%s %s: Backend not exist!!\n", __func__, bdev->dev_name);
						break;
					}
					wake_up_all(vcablk_bcknd_disk_get_probe_queue(bckd));
					break;
				default:
					pr_err("%s %s: Probe IRQ %i Unknown task %u\n",
							__func__, bdev->dev_name, dev_id, err);
					response->command_ack = 0;
					err = -EIO;

				}
			}
		}
	}
}

int vcablk_bcknd_add_to_devpage(struct vcablk_bcknd_dev *bdev,
		struct vcablk_dev_page *dev_page, struct vcablk_bcknd_disk *bckd)
{
	int disk_id;

	if (!dev_page) {
		pr_err("%s %s: Wait for dev_page!!!\n", __func__, bdev->dev_name);
		return 0;
	} else if (!bckd) {
		pr_err("%s %s: Backend is NULL!!!\n", __func__, bdev->dev_name);
		return 0;
	}

	disk_id = vcablk_bcknd_disk_get_id(bckd);
	pr_debug("%s %s: disk id %i!!!\n", __func__, bdev->dev_name, disk_id);

	if (disk_id >= dev_page->num_devices) {
		pr_err("%s %s: Can not create frontend device\n", __func__, bdev->dev_name);
		return -ENOSPC;
	}

	if (dev_page->devs[disk_id].command)
		pr_warning("%s %s: TASK %u IN PROGRESS DISK_ID %i!!!\n", __func__, bdev->dev_name,
				dev_page->devs[disk_id].command, disk_id);

	if (!bckd) {
		pr_err("%s %s: Backend device not exist %i device\n",
				__func__, bdev->dev_name, disk_id);
		return -ENODEV;
	}

	if (vcablk_bcknd_disk_get_state(bckd) != DISK_STATE_CREATE) {
		pr_err("%s %s:Disk %i not wait to add to dev_page!!!\n",
				__func__, bdev->dev_name, disk_id);
		return -EIO;
	}

	dev_page->devs[disk_id].read_only =
			vcablk_media_read_only(vcablk_bcknd_disk_get_media(bckd));
	dev_page->devs[disk_id].size_bytes =
			vcablk_media_size(vcablk_bcknd_disk_get_media(bckd));

	dev_page->devs[disk_id].request_db =
			vcablk_bcknd_disk_prepare_request_db(bckd);
	wmb();
	dev_page->devs[disk_id].command = CMD_CREATE;
	wmb();

	return 0;
}

/*
 * The ioctl() implementation
 */
static long vcablk_bcknd_ioctl (struct file *file, unsigned int cmd,
			unsigned long parm)
{
	int err = 0;
	struct vcablk_bcknd_dev *bdev =  container_of(file->private_data,
		struct vcablk_bcknd_dev, mdev);
	void __user *argp = (void __user *)parm;

	mutex_lock(&bdev->lock);
	switch(cmd) {
	case VCA_BLK_GET_VERSION: {
		__u32 ver = VCA_BLK_VERSION;
		if (copy_to_user(argp, &ver, sizeof(ver))) {
			err = -EFAULT;
		}
		break;
	}
	case VCA_BLK_GET_DISKS_MAX: {
		__u32 max = vcablk_bcknd_max_dev;
		if (copy_to_user(argp, &max, sizeof(max))) {
			err = -EFAULT;
		}
		break;
	}
	case VCA_BLK_GET_DISK_INFO: {
		struct vcablk_disk_info_desc *desc =
				vmalloc(sizeof (struct vcablk_disk_info_desc));
		if (!desc) {
			pr_warning("%s %s: Can not alloc memory\n", __func__, bdev->dev_name);
			err = -EFAULT;
			break;
		}

		if (copy_from_user(desc, argp, sizeof(struct vcablk_disk_info_desc))) {
			err = -EFAULT;
		} else if (desc->disk_id >= vcablk_bcknd_max_dev) {
			err = -EINVAL;
		} else {
			struct vcablk_bcknd_disk *bckd = bdev->device_array[desc->disk_id];
			if (!bckd) {
				desc->exist = 0;
			} else {
				desc->exist = 1;
				if (vcablk_media_read_only(
						vcablk_bcknd_disk_get_media(bckd))) {
					desc->mode = VCABLK_DISK_MODE_READ_ONLY;
				} else {
					desc->mode = VCABLK_DISK_MODE_READ_WRITE;
				}
				desc->size = vcablk_media_size(
						vcablk_bcknd_disk_get_media(bckd));
				desc->state =  vcablk_bcknd_disk_get_state(bckd);

				strncpy(desc->file_path,
						vcablk_media_file_path(vcablk_bcknd_disk_get_media(bckd)),
						sizeof(desc->file_path) - 1);
				desc->file_path[sizeof(desc->file_path) - 1] = '\0';
				desc->type = vcablk_bcknd_disk_get_media(bckd)->type;
			}
			if (copy_to_user(argp, desc,
					sizeof(struct vcablk_disk_info_desc))) {
				err = -EFAULT;
			}
		}
		vfree(desc);
		break;
	}
	case VCA_BLK_OPEN_DISC: {
		struct vcablk_disk_open_desc *desc =
				vmalloc(sizeof (struct vcablk_disk_open_desc));
		if (!desc) {
			pr_warning("%s %s: Can not alloc memory\n",__func__, bdev->dev_name);
			err = -EFAULT;
			break;
		}
		pr_warning("%s %s: VCA_BLK_OPEN_DISC...\n", __func__, bdev->dev_name);
		if (copy_from_user(desc, argp, sizeof(struct vcablk_disk_open_desc))) {
			err = -EFAULT;
		} else if (desc->disk_id >= vcablk_bcknd_max_dev) {
			err = -EINVAL;
		} else {
			pr_warning("%s  %s: Create device id %i, type %i,"
					" mode 0x%x, size: %llu, path %s\n",__func__, bdev->dev_name, desc->disk_id,
					desc->type, desc->mode, desc->size, desc->file_path);

			if (desc->type == VCABLK_DISK_TYPE_FILE ||
					desc->type == VCABLK_DISK_TYPE_MEMORY) {
				err = vcablk_bcknd_create(bdev, desc);
				if (err || !bdev->device_array[desc->disk_id]) {
					pr_err("%s %s: Can not create device "
						"%i error %i\n", __func__, bdev->dev_name, desc->disk_id, err);
				}
			} else {
				pr_warning("%s %s: Unknown type\n",__func__, bdev->dev_name);
				err = -EINVAL;
			}
		}
		vfree(desc);
		break;
	}
	case VCA_BLK_CLOSE_DISC: {
		__u32 id;
		if (copy_from_user(&id, argp, sizeof(id))) {
			err = -EFAULT;
		} else {
			pr_warning("%s %s: VCA_BLK_CLOSE_DISC %u\n", __func__, bdev->dev_name, id);
			if (id >= vcablk_bcknd_max_dev) {
				pr_err("%s %s: Wronk ID %i\n", __func__, bdev->dev_name, id);
				err = -EINVAL;
			} else {
				if (bdev->device_array[id]) {
					err = vcablk_bcknd_destroy(bdev, id, false);
				} else {
					err = -ENODEV;
				}
			}
		}
		break;
	}
	default: {
		pr_err("%s %s: UNKNOWN cmd %u parm %lu\n", __func__, bdev->dev_name, cmd, parm);
		err =  -ENOTTY; /* unknown command */
	}
	}

	mutex_unlock(&bdev->lock);
	return err;
}

static void vcablk_bcknd_close_dev_page(struct vcablk_bcknd_dev * bdev)
{
	int i;

	if (bdev->dp_addr) {
		pr_info("%s %s: Close dev_page 0x%llx for \"%s\".\n", __func__, bdev->dev_name,
				bdev->dp_addr, bdev->frontend_name);
		bdev->frontend_name[0] = '\0';
	}

	if (bdev->dev_pages_remapped) {
		bdev->hw_ops->iounmap(bdev->mdev.parent, bdev->dev_pages_remapped);
		bdev->dev_pages_remapped = NULL;
	}
	bdev->dp_addr = 0;

	/* All open devices should go to prepare mode !!!*/
	for (i = 0; i < vcablk_bcknd_max_dev; ++i) {
		if (bdev->device_array[i] &&
		    vcablk_bcknd_disk_get_state(bdev->device_array[i]) == DISK_STATE_OPEN) {
			vcablk_bcknd_disk_stop(bdev->device_array[i]);
		}
	}
}

static struct vcablk_dev_page *
vcablk_bcknd_open_dev_page(struct vcablk_bcknd_dev * bdev,
		pci_addr dev_pages_phy, char *frontend_name)
{
	struct vcablk_dev_page dev_pages_cpy;
	void *remmap;
	static struct vcablk_dev_page *dev_page_map;
	int i;

	pr_debug("%s %s: open 0x%llx\n", __func__, bdev->dev_name, dev_pages_phy);

	frontend_name[0] = '\0';

	remmap = bdev->hw_ops->ioremap(bdev->mdev.parent, dev_pages_phy, sizeof(dev_pages_cpy));
	if (!remmap) {
		pr_err("%s %s: dev_page can not remap addr 0x%llx\n",
				__func__, bdev->dev_name, dev_pages_phy);
		return NULL;
	}
	memcpy_fromio(&dev_pages_cpy, remmap, sizeof(dev_pages_cpy));
	bdev->hw_ops->iounmap(bdev->mdev.parent, remmap);
	remmap = NULL;

	if (dev_pages_cpy.version_frontend != VCA_BLK_VERSION) {
		pr_err("%s %s: Frontend version 0x%x unsupported, expected 0x%x "
				"addr 0x%llx\n", __func__, bdev->dev_name, dev_pages_cpy.version_frontend,
				VCA_BLK_VERSION, dev_pages_phy);
		return NULL;
	}
	if (dev_pages_cpy.dev_page_size < sizeof(dev_pages_cpy)) {
		pr_err("%s %s: Device page frontend too small %u addr 0x%llx\n",
				__func__, bdev->dev_name, dev_pages_cpy.dev_page_size, dev_pages_phy);
		return NULL;
	}
	if (!dev_pages_cpy.reinit_dev_page) {
		pr_err("%s %s: Reinit device page not set addr 0x%llx!\n",
				__func__, bdev->dev_name, dev_pages_phy);
		return NULL;
	}
	if (dev_pages_cpy.destroy_devpage) {
		pr_err("%s %s: Destroy device page is set addr 0x%llx!\n",
				__func__, bdev->dev_name, dev_pages_phy);
		return NULL;
	}
	if (dev_pages_cpy.num_devices > (dev_pages_cpy.dev_page_size
			- sizeof(struct vcablk_dev_page))
			/sizeof (struct vcablk_device_ctrl)) {
		pr_err("%s %s: device page num_devices %u is too big to keep in page "
				"size %u addr 0x%llx!\n", __func__, bdev->dev_name, dev_pages_cpy.num_devices,
				dev_pages_cpy.dev_page_size, dev_pages_phy);
		return NULL;
	}

	dev_page_map = bdev->hw_ops->ioremap(bdev->mdev.parent,
			dev_pages_phy, dev_pages_cpy.dev_page_size);
	if (!dev_page_map) {
		pr_err("%s %s: dev_page can not remap size %u addr 0x%llx\n",
				__func__, bdev->dev_name, dev_pages_cpy.dev_page_size, dev_pages_phy);
		return NULL;
	}

	if (dev_page_map->num_devices >= vcablk_bcknd_max_dev) {
		dev_page_map->num_devices = vcablk_bcknd_max_dev;
	} else {
		pr_debug("%s %s: devPage too small, reduce max blok devices from "
				"%i to %u addr 0x%llx\n", __func__, bdev->dev_name, vcablk_bcknd_max_dev,
				dev_page_map->num_devices, dev_pages_phy);
	}

	dev_page_map->notify_backend_db = bdev->ftb_db;
	dev_page_map->reinit_dev_page = 0;
	dev_page_map->valid_magic_bcknd = VCA_BLK_BCKND_MAGIC;
	wmb();

	/* Copy frontend name */
	strncpy(frontend_name, dev_page_map->name, VCA_FRONTEND_NAME_SIZE);
	frontend_name[VCA_FRONTEND_NAME_SIZE - 1] = '\0';

	pr_info("%s %s: Open dev_page 0x%llx, for \"%s\".\n",
			__func__, bdev->dev_name, dev_pages_phy, frontend_name);

	/* Add waiting devices */
	for (i = 0; i < dev_page_map->num_devices; ++i) {
		if (bdev->device_array[i] &&
			vcablk_bcknd_disk_get_state(bdev->device_array[i]) == DISK_STATE_CREATE) {
			vcablk_bcknd_add_to_devpage(bdev, dev_page_map, bdev->device_array[i]);
		}
	}
	return dev_page_map;
}

static void vcablk_bcknd_set_dev_page(struct vcablk_bcknd_dev * bdev,
		pci_addr dev_pages_phy)
{
	pr_debug("%s %s: dev_page 0x%lx\n", __func__, bdev->dev_name,
			(long unsigned int)dev_pages_phy);

	if (!dev_pages_phy) {
		pr_debug("%s %s: dev_page is NULL\n", __func__, bdev->dev_name);
		vcablk_bcknd_close_dev_page(bdev);
		return;
	}

	if (bdev->dev_pages_remapped) {
		if (dev_pages_phy == bdev->dp_addr &&
		    bdev->dev_pages_remapped->reinit_dev_page == 0) {
			pr_debug("%s %s: dp not changed \n", __func__, bdev->dev_name);
			return ;
		}
		pr_debug("%s %s: dev_pages exist!!\n", __func__, bdev->dev_name);
		vcablk_bcknd_close_dev_page(bdev);
	}

	bdev->dev_pages_remapped = vcablk_bcknd_open_dev_page(bdev, dev_pages_phy,
			bdev->frontend_name);
	if (bdev->dev_pages_remapped) {
		bdev->dp_addr = dev_pages_phy;
		bdev->hw_ops->send_intr(bdev->mdev.parent,
			bdev->dev_pages_remapped->notify_frontend_db);
	}
}

void vcablk_bcknd_unregister_f2b_callback(struct vcablk_bcknd_dev * bdev)
{
	pr_debug("%s %s %i\n", __func__, bdev->dev_name,__LINE__);

	vcablk_bcknd_set_dev_page(bdev, 0);

	mutex_lock(&bdev->lock);
	if (bdev->ftb_db >= 0) {
		bdev->hw_ops->free_irq(bdev->mdev.parent, bdev->ftb_db_cookie, bdev);
		bdev->ftb_db = -1;
		bdev->ftb_db_cookie = NULL;
	}
	mutex_unlock(&bdev->lock);
}

int vcablk_bcknd_register_f2b_callback(struct vcablk_bcknd_dev * bdev)
{
	int ret = 0;

	vcablk_bcknd_unregister_f2b_callback(bdev);

	mutex_lock(&bdev->lock);
	bdev->ftb_db = bdev->hw_ops->get_f2b_db(bdev->mdev.parent);
	if (bdev->ftb_db < 0) {
		pr_err("%s %s: failed to acquire probe ack doorbell\n",
				__func__, bdev->dev_name);
		ret = -ENODEV;
		goto err;
	}
	pr_debug("%s %s: f2b_db is %d\n", __func__, bdev->dev_name, bdev->ftb_db);

	bdev->ftb_db_cookie = bdev->hw_ops->request_irq(bdev->mdev.parent,
			vcablk_bcknd_ftb_irq, "IRQ_BLOCKIO_FTB", bdev,
			bdev->ftb_db);
	if (IS_ERR(bdev->ftb_db_cookie)) {
		bdev->ftb_db = -1;
		bdev->ftb_db_cookie = NULL;
		ret = -EIO;
	}

err:
	mutex_unlock(&bdev->lock);
	return ret;
}

static void vcablk_bcknd_clean(struct vcablk_bcknd_dev * bdev)
{
	pr_debug("%s %s %i\n", __func__, bdev->dev_name,__LINE__);

	vcablk_bcknd_unregister_f2b_callback(bdev);

	if (bdev->device_array) {
		int i;
		for (i = 0; i < vcablk_bcknd_max_dev; i++)
			vcablk_bcknd_destroy(bdev, i, true);

		flush_work(&bdev->work_ftb);
		kfree(bdev->device_array);
		bdev->device_array = NULL;
	}
}

struct miscdevice *
vcablk_bcknd_register(struct device* parent, const char *dev_name,
		struct plx_blockio_hw_ops* hw_ops, struct dma_chan *dma_ch)
{
	struct vcablk_bcknd_dev *bdev;
	int ret;

	pr_debug("%s %s: Max device_array %i\n", __func__,
			dev_name, vcablk_bcknd_max_dev);

	bdev = kmalloc(sizeof(struct vcablk_bcknd_dev), GFP_KERNEL);
	if (!bdev) {
		pr_err("%s %s: Allocation bdev is null\n", __func__, bdev->dev_name);
		return ERR_PTR(-ENOMEM);
	}
	memset (bdev, 0, sizeof(struct vcablk_bcknd_dev));
	strncpy(bdev->dev_name, dev_name, sizeof(bdev->dev_name)-1);
	bdev->dev_name[sizeof(bdev->dev_name)-1] = '\0';

	mutex_init(&bdev->lock);
	INIT_WORK(&bdev->work_ftb, vcablk_bcknd_ftb_work);
	bdev->magic = 12345;
	bdev->hw_ops = hw_ops;
	bdev->ftb_db = -1;
	bdev->dma_ch = dma_ch;

	bdev->mdev.minor = MISC_DYNAMIC_MINOR;
	bdev->mdev.name = bdev->dev_name;
	bdev->mdev.fops = &bcknd_ctl_fops;
	bdev->mdev.parent = parent;

	/* Allocate device_array array */
	bdev->device_array = kmalloc(vcablk_bcknd_max_dev *
			sizeof (struct vcablk_bcknd *), GFP_KERNEL);
	if (!bdev->device_array) {
		pr_err("%s %s: Allocation dev array is null\n", __func__, bdev->dev_name);
		ret = -ENOMEM;
		goto err;
	}
	memset (bdev->device_array, 0, vcablk_bcknd_max_dev *
			sizeof (struct vcablk_bcknd *));

	ret = vcablk_bcknd_register_f2b_callback(bdev);
	if (ret) {
		pr_err("%s %s: failed to set f2b callback\n", __func__, bdev->dev_name);
		goto err;
	}

	ret = misc_register(&bdev->mdev);
	// Workeround to run 8 cards (missing misc devices)
	if (ret) {
		unsigned char minor = 64; // see miscdevice.h
		do {
			bdev->mdev.minor = minor;
			ret = misc_register(&bdev->mdev);
			if( !ret)
				return &bdev->mdev;
		} while(MISC_DYNAMIC_MINOR != ++minor);
		pr_err( "%s failed misc_register %d\n", __func__, ret);
		goto err;
	}
	return &bdev->mdev;

err:
	vcablk_bcknd_clean(bdev);
	kfree(bdev);
	bdev = NULL;
	return ERR_PTR(-ENOMEM);
}

void vcablk_bcknd_unregister(struct miscdevice* md)
{
	struct vcablk_bcknd_dev *bdev =
			container_of(md, struct vcablk_bcknd_dev, mdev);
	pr_info("%s %s\n", __func__, bdev->dev_name);
	vcablk_bcknd_set_dev_page(bdev, 0);
	vcablk_bcknd_clean(bdev);
	misc_deregister(&bdev->mdev);
	kfree(bdev);
}


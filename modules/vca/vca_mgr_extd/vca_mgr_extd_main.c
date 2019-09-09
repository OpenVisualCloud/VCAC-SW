/*
 * Intel VCA Software Stack (VCASS)
 *
 * Copyright(c) 2015-2017 Intel Corporation.
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
 * Intel Visual Compute Accelerator secondary manager (vca_mgr_extd) driver.
 *
 */

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/suspend.h>
#include <linux/capability.h>

#include "vca_mgr_extd_main.h"
#include "vca_mgr_extd_ioctl.h"
#include "../plx87xx/plx_device.h"
#include "../include/vca_ioctl_impl.h"


static int vca_mgr_extd_open(struct inode *inode, struct file *f)
{
	return 0;
}

static int vca_mgr_extd_release(struct inode *inode, struct file *f)
{
	return 0;
}

static long vca_mgr_extd_ioctl(struct file *f,
	unsigned int cmd, unsigned long arg)
{
	int rc = 0;

	struct vca_mgr_extd_device *vca_dev =  container_of(f->private_data,
		struct vca_mgr_extd_device, misc_dev);

	void __user *argp = (void __user *)arg;

	switch (cmd) {
	case VCA_UPDATE_SECONDARY_EEPROM:
	{
		size_t total_size;
		struct vca_secondary_eeprom_desc __user  *usr_desc =
			(struct vca_secondary_eeprom_desc __user *)argp;
		struct vca_secondary_eeprom_desc *desc;

		if (!capable(CAP_SYS_ADMIN)) {
			rc = -EPERM;
			goto finish;
		}

		desc = vca_get_ioctl_data(usr_desc, VCA_MAX_EEPROM_BUF_SIZE, VCA_MAX_EEPROM_BUF_SIZE, &total_size);
		if (IS_ERR(desc)) {
			rc = PTR_ERR(desc);
			goto finish;
		}

		desc->ret = vca_dev->hw_ops->update_eeprom(vca_dev, desc->buf,
								desc->buf_size);

		/* not returning anything in data in buffer */
		desc->buf_size = 0;

		if (copy_to_user(usr_desc, desc, sizeof(*desc)))
			rc = -EFAULT;

		kfree(desc);
		break;
	}
	case VCA_READ_BOARD_ID:
	{
		__u32 boardId = 0;
		boardId = vca_dev->hw_ops->get_plx_straps(vca_dev);

		if (copy_to_user(argp, &boardId, sizeof(boardId))) {
			rc = -EFAULT;
		}
		break;
	}
	default:
	{
		rc = -EINVAL;
		break;
	}
	}
finish:
	return rc;
}

static const struct file_operations vca_mgr_extd_fops = {
	.open = vca_mgr_extd_open,
	.release = vca_mgr_extd_release,
	.unlocked_ioctl = vca_mgr_extd_ioctl,
	.owner = THIS_MODULE,
};

static int vca_mgr_extd_driver_probe(struct vca_mgr_extd_device *vca_dev)
{
	int ret;
	struct miscdevice *mdev = &vca_dev->misc_dev;
	dev_info(&vca_dev->dev, "buildinfo: build no "
		BUILD_NUMBER ", built on " BUILD_ONDATE ".\n");

	init_completion(&vca_dev->cpu_threads_compl);

	mdev->minor = MISC_DYNAMIC_MINOR;
	snprintf(vca_dev->misc_dev_name, sizeof(vca_dev->misc_dev_name),
		"vca_mgr_extd%d", vca_dev->hw_ops->get_card_id(vca_dev));
	mdev->name = vca_dev->misc_dev_name;
	mdev->fops = &vca_mgr_extd_fops;
	ret = misc_register(mdev);
	// Workeround to run 8 cards (missing misc devices)
	if (ret) {
		unsigned char minor = 64; // see miscdevice.h
		do {
			mdev->minor = minor;
			ret = misc_register(mdev);
			if( !ret)
				return 0; // success
		} while(MISC_DYNAMIC_MINOR != ++minor);
		dev_err(&vca_dev->dev, "%s failed misc_register %d\n", __func__, ret);
	}
	return ret;
}

static void vca_mgr_extd_driver_remove(struct vca_mgr_extd_device *vca_dev)
{
	complete_all(&vca_dev->cpu_threads_compl);
	misc_deregister(&vca_dev->misc_dev);
}

static struct vca_mgr_extd_device_id id_table[] = {
	{ VCA_MGR_EXTD_DEV_VCA, VCA_MGR_EXTD_DEV_ANY_ID },
	{ 0 },
};

static struct vca_mgr_extd_driver vca_mgr_extd_driver = {
	.driver.name =	KBUILD_MODNAME,
	.driver.owner =	THIS_MODULE,
	.id_table = id_table,
	.probe = vca_mgr_extd_driver_probe,
	.remove = vca_mgr_extd_driver_remove,
};

module_vca_mgr_extd_driver(vca_mgr_extd_driver);

MODULE_DEVICE_TABLE(mbus, id_table);
MODULE_AUTHOR("Intel Corporation");
MODULE_DESCRIPTION("Intel(R) VCA second manager(vca_mgr_extd) driver");
MODULE_LICENSE("GPL v2");

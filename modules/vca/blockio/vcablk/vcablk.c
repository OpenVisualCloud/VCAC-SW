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
#include <linux/moduleparam.h>
#include <linux/init.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/mutex.h>
#include <linux/version.h>

#ifdef TEST_BUILD
#include "vcablk_test_hw_ops.h"
#else
#include "plx_hw_ops_blockio.h"
#endif

#include "vcablk_disk.h"
#include "vcablk_common/vcablk_common.h"
#include "vcablk_hal.h"


/*
 * From unknown reason, when using function is_kdump_kernel(void)
 * loading module vcablkfe return error:
 * "vcablkfe: Unknown symbol elfcorehdr_addr"
 * Now detect crashdump and inform host side for DEBIAN is disabled or not CENTOS kernel.
 * */
#if defined RHEL_RELEASE
#if defined DEBIAN
#error "vcablkfe: Unknown symbol elfcorehdr_addr"
#endif /*defined DEBIAN*/
#define KERNEL_CRASH_INFO
#endif /*defined RHEL_RELEASE*/

#ifdef KERNEL_CRASH_INFO
#include <linux/crash_dump.h>
#endif /* KERNEL_CRASH_INFO */

MODULE_LICENSE("GPL");

#define TIMEOUT_REQUEST_MS 5000

static int vcablk_alloc_buff(struct vcablk_dev *fdev, size_t size, void **va, dma_addr_t *da)
{
	void *_va;
	dma_addr_t _da;
	int err;

	_va = kmalloc(size, GFP_KERNEL | GFP_DMA);
	if (!_va) {
		pr_info("%s allocation failure\n", __func__);
		return -ENOMEM;
	}

	err = vcablk_dma_map_single(fdev->parent, _va, size, &_da);
	if (err) {
		pr_info("%s dma mapping error %d\n", __func__, err);
		kfree(_va);
		return err;
	}

	*va = _va;
	*da = _da;

	return 0;
}

static void vcablk_dealloc_buff(struct vcablk_dev *fdev, size_t size, void *va, dma_addr_t da)
{
	vcablk_dma_unmap_single(fdev->parent, da, size);
	kfree(va);
}

static struct vcablk_ring *init_ring(struct vcablk_dev *fdev, __u32 size, __u32 elem_size, int queue_size)
{
	struct vcablk_ring *ring = NULL;
	dma_addr_t da = 0;
	int err;

	if (!is_power_of_2(queue_size)) {
		pr_err("%s: Try alloc ring with queue not power of 2 %i\n",
				__func__, queue_size);
		return ERR_PTR(-EPERM);
	}

	err = vcablk_alloc_buff(fdev, size, (void**)&ring, &da);

	if (err)
		return ERR_PTR(err);

	ring->size_alloc = size;
	ring->elem_size = elem_size;
	ring->num_elems = queue_size;
	ring->last_add = 0;
	ring->last_used = 0;

	ring->dma_addr = da;

	return ring;
}

static void destroy_ring(struct vcablk_dev *fdev, struct vcablk_ring * ring)
{
	if (ring && !IS_ERR(ring))
		vcablk_dealloc_buff(fdev, ring->size_alloc, ring, ring->dma_addr);
}

static struct vcablk_ring *init_request_ring(struct vcablk_dev *fdev, int queue_size)
{
	__u32 elem_size =  sizeof (struct vcablk_request);
	__u32 size = (__u32)(sizeof(struct vcablk_ring)
			+ queue_size * elem_size);
	return init_ring(fdev, size, elem_size, queue_size);
}

static struct vcablk_ring *init_completion_ring(struct vcablk_dev *fdev, int queue_size)
{
	__u32 elem_size = sizeof (struct vcablk_completion);
	__u32 size = (__u32)(sizeof(struct vcablk_ring)
			+ queue_size * elem_size);
	return init_ring(fdev, size, elem_size, queue_size);
}

static int vcablk_device_stop_id(struct vcablk_dev *fdev, int dev_id, bool force)
{
	int err = 0;
	/* TODO: Frontend should wait on bcknd to stop. */
	if (fdev->vcablk_disks) {
		err = vcablk_disk_stop(fdev->vcablk_disks[dev_id], force);
	} else {
		err = -ENODEV;
	}
	return err;
}

static int vcablk_device_destroy_id(struct vcablk_dev *fdev, int dev_id)
{
	int err;
	/* TODO: Frontend should wait on bcknd to stop. */

	if (fdev->vcablk_disks) {
		struct vcablk_ring *ring_req = vcablk_disk_get_rings_req(fdev->vcablk_disks[dev_id]);
		struct vcablk_ring *completion_ring = vcablk_disk_get_rings_ack(fdev->vcablk_disks[dev_id]);
		err = vcablk_disk_destroy(fdev->vcablk_disks[dev_id]);

		fdev->pages->devs[dev_id].response.request_ring = 0;
		fdev->pages->devs[dev_id].response.request_ring_size = 0;
		fdev->pages->devs[dev_id].response.completion_ring = 0;
		fdev->pages->devs[dev_id].response.completion_ring_size = 0;
		fdev->pages->devs[dev_id].response.done_db = -1;
		wmb();

		fdev->vcablk_disks[dev_id] = NULL;
		destroy_ring(fdev, ring_req);
		ring_req = NULL;

		destroy_ring(fdev, completion_ring);
		completion_ring = NULL;

	} else {
		err = -ENODEV;
	}
	return err;
}

static void vcablk_clean(struct vcablk_dev *fdev)
{
	if (fdev->vcablk_disks) {
		int i;

#ifdef KERNEL_CRASH_INFO
		if (is_kdump_kernel()) {
			fdev->pages->flags_events |= (VCA_FLAG_EVENTS_FRONTEND_DUMP_END);
			fdev->pages->flags_events &= ~(VCA_FLAG_EVENTS_FRONTEND_DUMP_START);
			barrier();
			fdev->f2b_db = fdev->hw_ops->get_f2b_db(fdev->parent);
		}
#endif /* KERNEL_CRASH_INFO */

		for (i = 0; i < fdev->max_dev; i++) {
			if (fdev->vcablk_disks[i]) {
				vcablk_disk_stop(fdev->vcablk_disks[i], true);
				vcablk_disk_destroy(fdev->vcablk_disks[i]);
				fdev->vcablk_disks[i] = NULL;
			}
		}
		kfree(fdev->vcablk_disks);
		fdev->vcablk_disks = NULL;
	}

	vcablk_disk_exit();

}

static int vcablk_task_create(struct vcablk_dev *fdev, int dev_id)
{
	struct vcablk_ring *ring_req = NULL, *completion_ring = NULL;
	int err;
	int done_db_local;
	static struct vcablk_disk *dev = NULL;

	if (dev_id < 0 || dev_id >= fdev->max_dev) {
		pr_err("%s: Wrong ID %i out of range [0-%i]\n", __func__,
				dev_id, fdev->max_dev -1);
		err = -EINVAL;
		goto exit;
	}

	if (fdev->vcablk_disks[dev_id]) {
		pr_err("%s: Disk with ID %i already exist\n", __func__, dev_id);
		err = -EBUSY;
		goto exit;
	}

	ring_req = init_request_ring(fdev, VCABLK_QUEUE_REQUESTS_NUMS);
	if (IS_ERR(ring_req)) {
		err = PTR_ERR(ring_req);
		goto deinit_rings;
	}

	completion_ring = init_completion_ring(fdev, VCABLK_QUEUE_ACK_NUMS);
	if (IS_ERR(completion_ring)) {
		err = PTR_ERR(completion_ring);
		goto deinit_rings;
	}

	done_db_local = fdev->hw_ops->next_db(fdev->parent);
	if (done_db_local < 0) {
		pr_err("%s: Can not get Doorbell\n", __func__);
		err = -ENODEV;
		goto deinit_rings;
	}

	pr_info("%s: start create id dev %i\n", __func__, dev_id);
	dev = vcablk_disk_create(
			fdev,
			dev_id,
			fdev->pages->devs[dev_id].size_bytes,
			fdev->pages->devs[dev_id].read_only,
			ring_req, completion_ring, done_db_local,
			fdev->pages->devs[dev_id].request_db);

	if (IS_ERR(dev)) {
		err = PTR_ERR(dev);
		pr_err("%s: device create id %i err  %i\n",
				__func__, dev_id, err);
		goto deinit_rings;
	}

	pr_info("%s: device create id %i\n", __func__, dev_id);
	fdev->vcablk_disks[dev_id] = dev;

	fdev->pages->devs[dev_id].response.request_ring = ring_req->dma_addr;
	fdev->pages->devs[dev_id].response.request_ring_size = ring_req->size_alloc;
	fdev->pages->devs[dev_id].response.completion_ring = completion_ring->dma_addr;
	fdev->pages->devs[dev_id].response.completion_ring_size = completion_ring->size_alloc;
	fdev->pages->devs[dev_id].response.done_db = done_db_local;
	wmb();

	return 0;
deinit_rings:
	fdev->pages->devs[dev_id].response.request_ring = 0;
	fdev->pages->devs[dev_id].response.request_ring_size = 0;
	fdev->pages->devs[dev_id].response.completion_ring = 0;
	fdev->pages->devs[dev_id].response.completion_ring_size = 0;
	fdev->pages->devs[dev_id].response.done_db = -1;
	wmb();

	destroy_ring(fdev, ring_req);
	ring_req = NULL;

	destroy_ring(fdev, completion_ring);
	completion_ring = NULL;
exit:
	pr_err("%s failed with status %d\n", __func__, err);
	return err;
}


static int vcablk_handle_task(struct vcablk_dev *fdev, struct vcablk_device_ctrl *dev, int dev_id)
{
	int err = 0;

	pr_err("%s: Probe IRQ %i task %u\n", __func__,
			dev_id, dev->command);

	switch (dev->command) {
	case CMD_CREATE: {
		err = vcablk_task_create(fdev, dev_id);
		break;
	}
	case CMD_RUN:
		err = vcablk_disk_start(fdev->vcablk_disks[dev_id]);
		if (err) {
			pr_err("%s: Start device %i failed err %i\n", __func__,
							dev_id, err);
			vcablk_device_destroy_id(fdev, dev_id);
		}
		break;

	case CMD_STOP_NORMAL:
	case CMD_STOP_FORCE:
		err = vcablk_device_stop_id(fdev, dev_id, dev->command == CMD_STOP_FORCE);
		break;

	case CMD_DESTROY:
		err = vcablk_device_destroy_id(fdev, dev_id);
		break;
	default:
		pr_err("%s: Probe IRQ %i Unknown task %u\n", __func__,
				dev_id, dev->command);
		err = -EIO;
	}

	return err;
}

static void vcablk_probe_work(struct work_struct *work)
{
	struct vcablk_dev *fdev = container_of(work, struct vcablk_dev, probe_work);
	int i;

	pr_err("%s: Probe IRQ\n", __func__);

	if (!fdev->pages) {
		pr_err("%s: Not expected IRQ\n", __func__);
		goto exit;
	}

	if (fdev->pages->destroy_devpage) {
		wake_up_all(&fdev->destroy_devpage_queue);
		goto exit;
	}

	if (!fdev->pages->num_devices) {
		pr_err("%s: Num devices is empty\n", __func__);
		goto exit;
	}

	for (i=0;i<fdev->max_dev; ++i) {
		if (fdev->pages->devs[i].command) {
			struct vcablk_device_ctrl *dev = &fdev->pages->devs[i];
			__u8 task = dev->command;
			int err = vcablk_handle_task(fdev, dev, i);
			dev->response.status = err;
			dev->response.command_ack = task;
			dev->command = 0;
		}
	}

	fdev->hw_ops->send_intr(fdev->parent, fdev->f2b_db);

exit:
	return;
}

static irqreturn_t vcablk_probe_irq(int irq, void *data)
{
	struct vcablk_dev *fdev = (struct vcablk_dev *)data;
	//vcablk_probe();
	pr_err("%s: Probe ACK IRQ\n", __func__);
	schedule_work(&fdev->probe_work);
	fdev->hw_ops->ack_interrupt(fdev->parent, fdev->probe_db);
	return IRQ_HANDLED;
}

static void vcablk_set_empty_devpage(struct vcablk_dev *fdev)
{
	if (!fdev->pages)
		return;

	fdev->pages->destroy_devpage = DESTROY_DEV_PAGE_INIT;
	barrier();
	fdev->hw_ops->send_intr(fdev->parent, fdev->f2b_db);

	/* wait for ack */
	if (!wait_event_interruptible_timeout(fdev->destroy_devpage_queue,
			fdev->pages->destroy_devpage == DESTROY_DEV_PAGE_END,
			msecs_to_jiffies(TIMEOUT_REQUEST_MS))) {
		pr_err("%s: DEV_PAGE DESTROY Wait request TIMEOUT!!!\n",
			__func__);
	}
}

struct vcablk_dev* vcablk_init(struct device *dev, void* dev_page, size_t dev_page_size, struct plx_blockio_hw_ops* hw_ops)
{
	struct vcablk_dev *fdev;
	int ret = 0;

	fdev = kzalloc(sizeof(*fdev), GFP_KERNEL);
	if (!fdev)
		return ERR_PTR(-ENOMEM);

	mutex_init(&fdev->lock);
	fdev->probe_db = -1;
	fdev->f2b_db = -1;


	fdev->hw_ops = hw_ops;
	fdev->parent = dev;
	fdev->f2b_db = fdev->hw_ops->get_f2b_db(fdev->parent);

	mutex_lock(&fdev->lock);

	ret = vcablk_disk_init(fdev);
	if (ret < 0) {
		pr_err("%s: unable to init disks\n", __func__);
		goto err;
	}

	fdev->pages = dev_page;
	fdev->pages->version_frontend = VCA_BLK_VERSION;
	fdev->pages->dev_page_size = dev_page_size;
	fdev->pages->reinit_dev_page = 1;
	fdev->pages->destroy_devpage = 0;

	snprintf(fdev->pages->name, VCA_FRONTEND_NAME_SIZE -1,
			"OS Linux, Kernel %u.%u.%u",
			(u8)(LINUX_VERSION_CODE >> 16),
			(u8)(LINUX_VERSION_CODE >> 8),
			(u16)(LINUX_VERSION_CODE & 0xff));
	fdev->pages->name[VCA_FRONTEND_NAME_SIZE -1] = '\0';

	fdev->probe_db = fdev->hw_ops->next_db(fdev->parent);
	fdev->probe_db_cookie = NULL;

	if (fdev->probe_db < 0) {
		ret = -ENODEV;
			goto err;
	}

	fdev->pages->notify_frontend_db = fdev->probe_db;

	fdev->probe_db_cookie = fdev->hw_ops->request_irq(fdev->parent,
			vcablk_probe_irq,
			"IRQ_PROBE", fdev, fdev->probe_db);

	if (IS_ERR(fdev->probe_db_cookie)) {
		fdev->probe_db_cookie = NULL;
		ret =-EIO;
		goto err;
	}

	fdev->pages->num_devices = (fdev->pages->dev_page_size
			- sizeof(struct vcablk_dev_page))
			/sizeof (struct vcablk_device_ctrl);

	/*
	 * Allocate the device array.
	 */
	fdev->max_dev = fdev->pages->num_devices;
	fdev->vcablk_disks = kzalloc(fdev->max_dev * sizeof(struct vcablk_disk *), GFP_KERNEL);
	if (!fdev->vcablk_disks) {
		pr_err("%s: Allocation dev array is null\n", __func__);
		ret = -ENOMEM;
		goto err;
	}

#ifdef KERNEL_CRASH_INFO
	if (is_kdump_kernel()) {
		fdev->pages->flags_events |= (VCA_FLAG_EVENTS_FRONTEND_DUMP_START);
		barrier();
		fdev->f2b_db = fdev->hw_ops->get_f2b_db(fdev->parent);
	}
#endif /* KERNEL_CRASH_INFO */

	INIT_WORK(&fdev->probe_work, vcablk_probe_work);

	mutex_unlock(&fdev->lock);

	init_waitqueue_head(&fdev->destroy_devpage_queue);

	ret = fdev->hw_ops->set_dev_page(fdev->parent);
	if (ret) {
		pr_err("error setting dev page: %d\n", ret);
		kfree(fdev->vcablk_disks);
		kfree(fdev);
		return ERR_PTR(-ENODEV);
	}

	return fdev;

err:
	vcablk_clean(fdev);
	mutex_unlock(&fdev->lock);
	kfree(fdev->parent);
	kfree(fdev);
	return ERR_PTR(ret);
}

void vcablk_deinit(struct vcablk_dev* fdev)
{
	pr_info("%s\n", __func__);
	vcablk_clean(fdev);

	mutex_lock(&fdev->lock);

	vcablk_set_empty_devpage(fdev);

	if (fdev->probe_db >= 0) {
		fdev->hw_ops->free_irq(fdev->parent, fdev->probe_db_cookie, NULL);
		fdev->pages->notify_frontend_db = -1;
		fdev->probe_db = -1;
		fdev->probe_db_cookie = NULL;
	}

	flush_work(&fdev->probe_work);
	fdev->pages = NULL;

	mutex_unlock(&fdev->lock);

	kfree(fdev);
}

#ifdef KERNEL_CRASH_INFO
#undef KERNEL_CRASH_INFO
#endif /* KERNEL_CRASH_INFO */

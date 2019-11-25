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
* Adapted from:
*
* virtio for kvm on s390
*
* Copyright IBM Corp. 2008
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License (version 2 only)
* as published by the Free Software Foundation.
*
*    Author(s): Christian Borntraeger <borntraeger@de.ibm.com>
*
* Intel Virtio Over PCIe (VOP) driver.
*
*/
#include <linux/version.h>
#include <linux/dma-mapping.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/kthread.h>
#include "../vca_virtio/uapi/vca_virtio_net.h"
#include <linux/kernel.h>
#include <linux/module.h>
#include "vop_main.h"
#include "vop_common.h"
#include "vop_kvec_buff.h"
#include "../common/vca_common.h"
#include "../plx87xx/plx_device.h"
#include "../plx87xx/plx_hw.h"

/* exclude first argument */
#define FIRST_ARG(first, ...) first

#define dev_err_once_dbg_later(...) \
	do { \
	static bool err_print = true; \
	if (err_print) { \
		err_print = false; \
		dev_err(__VA_ARGS__); \
		dev_err( FIRST_ARG(__VA_ARGS__), "[%s:%d Next occurrences will be logged at debug level]\n", __func__, __LINE__); \
	}  else { \
		dev_dbg(__VA_ARGS__); \
	} } while (0)


#define VOP_RING_SIZE_MASK (VOP_RING_SIZE - 1)

#define TIMEOUT_SEND_MS 3000
#define TIMEOUT_ASYNC_DMA_MS 2000

#define KVEC_INDEX_UPDATE_PERIOD 16

#define PLX_DMA_ALIGN_BYTES	64
#define DMA_MAX_OFFSET 127

//#define FORCE_USE_MEMCPY

#define SEND_ALIGN_OVERHEAD_DMA_SIZE ((DMA_MAX_OFFSET) + (PLX_DMA_ALIGN_BYTES))
#define SEND_ALIGN_OVERHEAD_MEMCPY_SIZE 64

static inline unsigned long get_time_jiff_not_zero(void)
{
	unsigned long ret = jiffies;
	return ret ? ret : ULONG_MAX;
}

#define get_time_diff_ms(start, end) jiffies_to_msecs(end - start)

void transfer_done_callback(void *data);
static void transfer_done(struct buffer_dma_item *item);

#ifdef DEBUG_PERF

#define DEBUG_PERF_INIT(prefix) \
		prefix##_time_wait_us = 0; \
		prefix##_time_wait_us_last = 0; \
		prefix##_time_diff_max_us = 0; \
		prefix##_time_last_rep_jiff = 0;

#define DEBUG_PERF_LOCAL() \
		unsigned long int time_diff_us;
		ktime_t  time_start;

#define DEBUG_PERF_START() time_start = ktime_get();

#define DEBUG_PERF_STOP(prefix) \
		time_diff_us = ktime_to_us(ktime_sub(ktime_get(), time_start)); \
		prefix##_time_wait_us += ktime_to_us(ktime_sub(ktime_get(), time_start)); \
		if (time_diff_us > prefix##_time_diff_max_us) { \
			prefix##_time_diff_max_us = time_diff_us; \
		} \
		if (get_time_diff_ms(prefix##_time_last_rep_jiff, get_time_jiff_not_zero()) > 8000) { \
			dev_err(&vdev->dev, "%s time %s wait_ms: " \
					"%lu time_wait_diff: %lu " \
					"time_range_ms: %u   " \
					"time_max_once_us: %lu\n",__func__, #prefix, \
					prefix##_time_wait_us/1000, \
					(prefix##_time_wait_us - prefix##_time_wait_us_last)/1000, \
					get_time_diff_ms(prefix##_time_last_rep_jiff, get_time_jiff_not_zero()), \
					prefix##_time_diff_max_us); \
			prefix##_time_wait_us_last = prefix##_time_wait_us; \
			prefix##_time_last_rep_jiff = get_time_jiff_not_zero(); \
			prefix##_time_diff_max_us = 0; \
		}

#else /* DEBUG_PERF */
#define DEBUG_PERF_INIT(prefix)
#define DEBUG_PERF_LOCAL()
#define DEBUG_PERF_START()
#define DEBUG_PERF_STOP(prefix)
#endif /* DEBUG_PERF*/

/* send heads up for used descriptors */
static inline void common_dev_notify_used(struct vop_dev_common *cdev)
{
	vop_send_heads_up(cdev, &cdev->heads_up_used_irq);
}

/* send heads up for available descriptors */
void common_dev_notify_avail(struct vop_dev_common *cdev)
{
	vop_send_heads_up(cdev, &cdev->heads_up_avail_irq);
}

/* handle heads up irq for used descriptors */
void common_dev_heads_up_used_irq(struct vop_dev_common *cdev)
{
	dev_dbg(&cdev->vdev->dev, "%s received heads up IRQ for used descriptors\n", __func__);
	if (cdev->ready) {
		cdev->heads_up_used_irq.rcv_ts = jiffies;
		wake_up_interruptible_all(&cdev->heads_up_used_irq.wq);
	}
}

/* handle heads up urq for available descriptors */
void common_dev_heads_up_avail_irq(struct vop_dev_common *cdev)
{
	dev_dbg(&cdev->vdev->dev, "%s received heads up IRQ for used descriptors\n", __func__);
	if (cdev->ready) {
		cdev->heads_up_avail_irq.rcv_ts = jiffies;
		wake_up_interruptible_all(&cdev->heads_up_avail_irq.wq);
	}
}

/**
 * kthread_run_on_node - Create and run thread only on node's cpu.
 *
 * @threadfn: thread function
 * @data: data passed to function
 * @node: node ID to be run on
 * @namefmt: thread name
 *
 */
#define kthread_run_on_node(threadfn, data, node, namefmt, ...)	\
({								\
    const struct cpumask *nodecpumask;				\
	struct task_struct *__k					\
		= kthread_create(threadfn,			\
			data, 					\
			namefmt, 				\
			## __VA_ARGS__); 			\
    nodecpumask = cpumask_of_node(node);			\
    set_cpus_allowed_ptr(__k, nodecpumask);			\
	if (!IS_ERR(__k))					\
		wake_up_process(__k);				\
	__k;							\
})

/**
 * vop_wait_for_completion: - waits for completion of a task or shutdown
 *
 * Return: 0 if done, ENODEV or EINTR if task should shut down
 * and EBUSY if timeout.
 */
int vop_wait_for_completion(struct vop_dev_common *cdev, struct completion *x,
	    int timeout_ms)
{
	long res;

	while (READ_ONCE(cdev->ready)) {
		res = wait_for_completion_interruptible_timeout(x,
			    msecs_to_jiffies(300));

		if (res < 0)
			return -EINTR;
		else if (res > 0)
			return READ_ONCE(cdev->ready) ? 0 : -ENODEV;

		timeout_ms -= 300;
		if (timeout_ms <= 0) {
			return -EBUSY;
		}
	}
	return -ENODEV;
}


static u8 vop_status(struct vop_dev_common *cdev)
{
	return cdev->dd_self->status;
}

static u8 vop_peer_status(struct vop_dev_common *cdev)
{
	return cdev->dd_peer->status;
}


static bool device_ready(struct vop_dev_common *cdev)
{
	return (vop_status(cdev) & VIRTIO_CONFIG_S_DRIVER_OK);
}

static bool peer_device_ready(struct vop_dev_common *cdev)
{
	return (vop_peer_status(cdev) & VIRTIO_CONFIG_S_DRIVER_OK);
}

void descriptor_read_notification(struct vop_dev_common *cdev)
{
	complete(&cdev->sync_desc_read);
}

/*
 * vop_async_dma - Wrapper for asynchronous DMAs.
 *
 * @dev - The address of the pointer to the device instance used
 * for DMA registration.
 * @dst - destination DMA address.
 * @src - source DMA address.
 * @len - size of the transfer.
 * @callback - routine to call after this operation is complete
 * @callback_param - general parameter to pass to the callback routine
 * @out_tx - dma tx structure to disable callback when canceled.
 *
 * Return dma_cookie_t, check error by dma_submit_error(cookie)
 */
dma_cookie_t vop_async_dma(struct vop_device *vpdev, dma_addr_t dst,
		dma_addr_t src, size_t len, dma_async_tx_callback callback,
		void *callback_param, struct dma_async_tx_descriptor **out_tx)
{
	dma_cookie_t cookie;
	struct dma_device *ddev;
	struct dma_async_tx_descriptor *tx;
	struct vop_info *vi = vpdev->priv;
	struct dma_chan *vop_ch = vi->dma_ch;
	unsigned long flags = DMA_PREP_INTERRUPT | DMA_PREP_FENCE;

	if (!vop_ch) {
		pr_err("no DMA channel available\n");
		cookie = -EBUSY;
		goto error;
	}
	ddev = vop_ch->device;
	tx = ddev->device_prep_dma_memcpy(vop_ch, dst, src, len, flags);

	if (!tx) {
		cookie = -ENOMEM;
		goto error;
	} else {
		tx->callback = callback;
		tx->callback_param = callback_param;
		if (out_tx) {
			/* It have to be before submitt because callback can be called
			 * in tx_submit() time. */
			*out_tx = tx;
		}
		cookie = tx->tx_submit(tx);
		if (dma_submit_error(cookie)) {
			if (out_tx) {
				*out_tx = NULL;
			}
			goto error;
		}
		dma_async_issue_pending(vop_ch);

		dev_dbg(&vi->vpdev->dev, "%s %d cookie %d, src 0x%llx, dst 0x%llx, "
				"len %lu\n", __func__, __LINE__, cookie, src, dst, len);
	}
error:
	if (dma_submit_error(cookie)) {
		dev_err_once_dbg_later(&vi->vpdev->dev, "%s %d err %d\n",
				__func__, __LINE__, cookie);
	}

	return cookie;
}

/*
 * vop_sync_dma - Wrapper for synchronous DMAs.
 *
 * @dev - The address of the pointer to the device instance used
 * for DMA registration.
 * @dst - destination DMA address.
 * @src - source DMA address.
 * @len - size of the transfer.
 *
 * Return DMA_SUCCESS on success
 */
int vop_sync_dma(struct vop_device *vpdev, dma_addr_t dst,
		dma_addr_t src, size_t len)
{
	int err = 0;
	struct dma_device *ddev;
	struct dma_async_tx_descriptor *tx;
	struct vop_info *vi = vpdev->priv;
	struct dma_chan *vop_ch = vi->dma_ch;

	if (!vop_ch) {
		pr_err("no DMA channel available\n");
		err = -EBUSY;
		goto error;
	}
	ddev = vop_ch->device;
	tx = ddev->device_prep_dma_memcpy(vop_ch, dst, src, len,
		DMA_PREP_FENCE);
	if (!tx) {
		err = -ENOMEM;
		goto error;
	} else {
		dma_cookie_t cookie;

		cookie = tx->tx_submit(tx);
		if (dma_submit_error(cookie)) {
			err = -ENOMEM;
			goto error;
		}
		err = dma_sync_wait(vop_ch, cookie);
	}
error:
	if (err)
		dev_err_once_dbg_later(&vi->vpdev->dev, "%s %d err %d\n",
			__func__, __LINE__, err);
	return err;
}

static void
buffer_dma_ring_item_reset(struct buffer_dma_item *item)
{
	BUG_ON(item->ring == NULL);

	if (item->tx) {
		/* Disable DMA Async Callbacks from cleanup DMA, when transfer
		 * not finished correct (dma hang etc.) */
		item->tx->callback = NULL;
		item->tx->callback_param = NULL;
		item->tx = NULL;
	}

	item->data_size = 0;
	item->vringh_tx = NULL;
	item->head_from = USHRT_MAX;
	item->bytes_read = 0;
	item->jiffies = 0;

	item->kvec_buff_id = -1;
	item->head_to = USHRT_MAX;
	item->kvec_to = NULL;
	item->num_kvecs_to = 0;
	item->bytes_written = 0;

	/* Reset should be call for all items during shutting down device
	 * to release all mapped resources */
	if (item->remapped) {
		struct vop_dev_common *cdev = item->ring->cdev;
		cdev->vdev->hw_ops->iounmap(cdev->vdev, item->remapped);
		wake_up_all(&cdev->remap_free_queue);
		item->remapped = NULL;
	}

	if (item->src_phys_da) {
		dev_dbg(item->ring->dev, "%s dma unmap addr_da %llx size %lu\n",
			    __func__, item->src_phys_da, item->src_phys_sz);
		if (item->ring->dma_dev) {
			dma_unmap_single(item->ring->dma_dev->dev, item->src_phys_da,
					item->src_phys_sz, DMA_TO_DEVICE);
		}
		item->src_phys_da = 0;
	}

	item->src_phys = 0;
	item->src_phys_sz = 0;

	if (item->k_from.used) {
		vringh_kiov_reset(&item->k_from);
	}

	item->status_ready = true;

	complete(&item->ring->wait_avail_read);
}

static struct buffer_dma_item *
buffer_dma_ring_get_read(struct buffers_dma_ring *ring)
{
	struct buffer_dma_item *item =
			&(ring->items[ring->indicator_rcv & VOP_RING_SIZE_MASK]);
	BUILD_BUG_ON(VOP_RING_SIZE > (1ULL<<(sizeof(ring->indicator_rcv)*8)));

	if (item->status_ready) {
		dev_dbg(ring->dev, "%s get buffer ID %u \n", __func__, item->id);
		item->status_ready = false;
		++ring->indicator_rcv;
	} else {
		dev_dbg(ring->dev, "%s item %i is in use!\n", __func__, item->id);
		item = NULL;
	}
	return item;
}

static void
buffer_dma_ring_deinit(struct buffers_dma_ring *ring)
{
	int i;

	if (!ring)
		return;

	if (ring->items) {
		for (i = 0; i < VOP_RING_SIZE; ++i) {
			struct buffer_dma_item *item = &(ring->items[i]);

			buffer_dma_ring_item_reset(item);

			item->status_ready = false;
			vringh_kiov_cleanup(&item->k_from);
			if (item->buf_da) {
				if (ring->dma_dev) {
					dma_unmap_single(ring->dma_dev->dev,
						item->buf_da,
						1 << (PAGE_SHIFT + ring->buf_pages),
						DMA_TO_DEVICE);
				}
				item->buf_da = 0;
			}

			if (item->buf) {
				free_pages((unsigned long)item->buf,
					ring->buf_pages);
				item->buf = 0;
			}
		}
		kfree(ring->items);
		ring->items = NULL;
	}

	kfree(ring);
}

static int
buffer_dma_ring_items_alloc(struct vop_dev_common *cdev)
{
	struct buffers_dma_ring *ring = NULL;
	int err;
	int i;

	BUG_ON(!cdev->buffers_ring);
	ring = cdev->buffers_ring;

	for (i = 0; i < VOP_RING_SIZE; ++i) {
		struct buffer_dma_item *item = &(ring->items[i]);

		if (item->buf)
			continue;

		item->buf = (void *)__get_free_pages(GFP_KERNEL | GFP_DMA,
						     ring->buf_pages );

		if (!item->buf) {
			pr_err("%s:%d Can not allocate memory\n", __func__,
					__LINE__);
			err = -ENOMEM;
			goto error;
		}

		if (ring->dma_dev) {
			item->buf_da = dma_map_single(ring->dma_dev->dev,
					item->buf, 1 << (PAGE_SHIFT + ring->buf_pages),
					DMA_TO_DEVICE);
			if ((err = dma_mapping_error(ring->dma_dev->dev, item->buf_da))) {
				item->buf_da = 0;
				goto error;
			}
		}

		CHECK_DMA_ZONE(ring->dev, item->buf_da);
	}

	return 0;

error:
	buffer_dma_ring_deinit(ring);
	cdev->buffers_ring = NULL;
	return err;
}

static int
buffer_dma_ring_init(struct vop_dev_common *cdev)
{
	struct buffers_dma_ring *ring = NULL;
	int err;
	int i;

	ring = kzalloc(sizeof(struct buffers_dma_ring), GFP_KERNEL);
	if (!ring) {
		pr_err("%s:%d Can not allocate memory\n", __func__, __LINE__);
		err = -ENOMEM;
		goto error;
	}

	ring->cdev = cdev;
	ring->vdev = cdev->vdev;
	ring->dev = &cdev->vdev->dev;

#ifdef FORCE_USE_MEMCPY
#pragma message "Transfer mode MEMCPY is a development feature only!"
	ring->dma_dev = NULL;
#else /* FORCE_USE_MEMCPY */
	if( cdev->vdev->dma_ch) {
		ring->dma_dev = cdev->vdev->dma_ch->device;
	} else {
		ring->dma_dev = NULL;
	}
#endif /* FORCE_USE_MEMCPY*/

	if(ring->dma_dev ) {
		ring->send_aligment_overhead = SEND_ALIGN_OVERHEAD_DMA_SIZE;
	} else {
		ring->send_aligment_overhead = SEND_ALIGN_OVERHEAD_MEMCPY_SIZE;
	}

	ring->buf_size = PAGE_ALIGN(VOP_INT_DMA_BUF_SIZE);
	ring->buf_pages = get_order(ring->buf_size);

	dev_dbg(ring->dev, "%s ring num_items %u buff_size %lu buff_pages %lu\n",
			__func__, VOP_RING_SIZE, ring->buf_size, ring->buf_pages);

	ring->items = 0;
	ring->indicator_rcv = 0;
	ring->counter_dma_send = 0;
	ring->counter_done_transfer = 0;

	DEBUG_PERF_INIT(ring->wait)
	DEBUG_PERF_INIT(ring->item)
	DEBUG_PERF_INIT(ring->ioremap_busy)

	ring->items = kzalloc(sizeof (struct buffer_dma_item) * VOP_RING_SIZE,
				GFP_KERNEL);
	if (!ring->items) {
		pr_err("%s:%d Can not allocate memory %luKB\n", __func__, __LINE__,
				(sizeof (struct buffer_dma_item) * VOP_RING_SIZE)/1024);
		err = -ENOMEM;
		goto error;
	}

	init_completion(&ring->wait_avail_read);
	init_completion(&ring->wait_dma_send);

	for (i = 0; i < VOP_RING_SIZE; ++i) {
		struct buffer_dma_item *item = &(ring->items[i]);
		item->ring = ring;
		item->id = i;
		BUILD_BUG_ON(VOP_RING_SIZE > (1ULL<<(sizeof(item->id)*8)));

		vringh_kiov_init(&item->k_from, NULL, 0);
		item->head_from = USHRT_MAX;
		item->head_to = USHRT_MAX;
		item->src_phys_da = 0;
		item->remapped = NULL;
		item->tx = NULL;

		buffer_dma_ring_item_reset(item);

		item->buf = NULL;
		item->buf_da = 0;
	}

	complete(&ring->wait_avail_read);
	cdev->buffers_ring = ring;

	if (!cdev->feature_desc_alignment) {
		err = buffer_dma_ring_items_alloc(cdev);
		if (err) {
			dev_err(ring->dev, "%s Cannot allocate memory for intermediate "
				"buffers\n", __func__);
			goto error;
		}
	}

	return 0;

error:
	buffer_dma_ring_deinit(ring);
	cdev->buffers_ring = NULL;
	return err;
}

int vop_common_get_descriptors(struct vop_device *vdev, struct vop_vringh* vr,
		u16 *head, struct vringh_kiov *kiov, bool read)
{
	struct vringh *vrh = &vr->vrh;
	int ret;

	spin_lock(&vr->vr_spinlock);
	if (*head == USHRT_MAX) {
		if (read)
			ret = vca_vringh_getdesc_kern(vrh, kiov, NULL, head,
					GFP_NOFS);
		else
			ret = vca_vringh_getdesc_kern(vrh, NULL, kiov, head,
					GFP_NOFS);

		if (*head != USHRT_MAX)
			pr_debug("%s getdesc: %s descs vring head %d\n",
					__func__, read ? "READ" : "WRITE",
					 *head);

		if (ret <= 0) {
			if (ret < 0) {
				dev_err(&vdev->dev, "%s error fetching read"
					"descriptors %x\n", __func__, ret);
				vringh_kiov_cleanup(kiov);
			}
			*head = USHRT_MAX;
		}
	}
	else {
		ret = 1;
	}

	spin_unlock(&vr->vr_spinlock);
	return ret;
}

/* Mark descriptors as completed. This needs to be synchronized by the caller
 * so that no other function using this vr->vrh is executed in parallel */
static void put_descriptors(struct vop_vringh* vr, u16 head, size_t size)
{
	pr_debug("%s complete descs vring no %d head %d size %d\n",
		__func__ ,
		vr->index, head, (u32)size);

	spin_lock(&vr->vr_spinlock);
	vca_vringh_complete_kern(&vr->vrh, head, size);

	if (vr->vrh.notify && vca_vringh_need_notify_kern(&vr->vrh) > 0)
		vringh_notify(&vr->vrh);

	spin_unlock(&vr->vr_spinlock);
}

static int
transfer_read(struct buffer_dma_item *item, struct vop_device *vdev)
{
	struct vringh_kiov* k_from = &item->k_from;
	int err = 0;

	dev_dbg(&vdev->dev, "%s read head_from %i\n", __func__, item->head_from);
	BUG_ON(k_from->i >= k_from->used);

	// TODO: fix buffer wrapping logic here
	if (k_from->used != 2) {
		dev_err(&vdev->dev, "%s unsupported number of descriptors %i\n",
				__func__, k_from->used);
		err = -EIO;
		goto end;
	}

	for (; k_from->i < k_from->used; k_from->i++) {
		struct kvec *v_from = &k_from->iov[k_from->i];
		size_t src_size = v_from->iov_len;
		dev_dbg(&vdev->dev, "%s buff: %p FROM vector %u base %p len %llx\n",
				__func__, item, k_from->i, v_from->iov_base,
				(u64)v_from->iov_len);
		if (k_from->i == 1) {
			dma_addr_t src = (dma_addr_t)(v_from->iov_base);
			dev_dbg(&vdev->dev, "%s buff: %p TRANSLATED SRC:%llx src_size %lu\n",
					__func__, item, src, src_size);
			item->data_size = src_size;
			if (item->ring->dma_dev) {
				if (item->ring->cdev->feature_desc_alignment) {
					item->src_phys = src;
				} else {
					if (src_size > VOP_INT_DMA_BUF_SIZE) {
						item->data_size = VOP_INT_DMA_BUF_SIZE;
						dev_warn(&vdev->dev, "%s buff: %p is too big for internal "
								"buffer src_size %lu\n", __func__, item, src_size);
					}
					memcpy(item->buf, phys_to_virt(src), item->data_size);
				}
			} else {
				/* MEMECPY path */
				item->src_phys = src;
			}
		}
		item->bytes_read += src_size;
	}

end:
	if (item->ring->dma_dev) {
		if (!item->ring->cdev->feature_desc_alignment) {
			if (item->vringh_tx && item->head_from != USHRT_MAX) {
				put_descriptors(item->vringh_tx, item->head_from,
						item->bytes_read);
				item->head_from = USHRT_MAX;
			}
		}
	}

	return err;
}

static int
transfer_wait(struct buffer_dma_item *item, struct vop_device *vdev)
{
	int err = 0;
	struct vop_dev_common *cdev = item->ring->cdev;
	struct vop_heads_up_irq *avail_hu_irq = &cdev->heads_up_avail_irq;

	item->kvec_buff_id =
		vop_kvec_size_to_ring_id(cdev, item->data_size +
				item->ring->send_aligment_overhead );
	BUG_ON(item->kvec_buff_id < 0);

	dev_dbg(&vdev->dev, "%s item id: %u ring id: %u\n", __func__, item->id,
			item->kvec_buff_id);

	/* Get descriptor to write */
	if (vop_kvec_get(cdev, vdev, item)) {
		DEBUG_PERF_LOCAL()
		DEBUG_PERF_START()

		err = vop_wait_for_avail_desc(cdev, avail_hu_irq, item->kvec_buff_id);

		DEBUG_PERF_STOP(item->ring->wait)

		if (!err && vop_kvec_get(cdev, vdev, item)) {
			err = -EBUSY;
			dev_warn(&vdev->dev,"%s Can't get expected write descriptor item "
				"%p (%u)\n", __func__, item, item->id);
		}
	}

	return err;
}

/*
 * transfer_memcpy_send - Send data to PCI through memcpy.
 *
 * @item - item with data to copy
 * @vdev - address of the vdev
 * @size - size of the transfer
 *
 */
void
transfer_memcpy_send(struct buffer_dma_item *item, struct vop_device *vdev,
	    size_t size)
{
	BUG_ON(item->src_phys == 0);
	if (!size)
		return;

	if (item->ring->cdev->feature_desc_alignment) {
		/* Align to word */
		const size_t align = sizeof(int);
		void *src_virt = phys_to_virt(item->src_phys);
		unsigned offset = (u64)src_virt & (align - 1);
		if (!offset)
			offset += align;

		dev_dbg(&vdev->dev, "%s memcpy use feature alignment src: %llx "
				"size:%llx offset %x\n", __func__, (u64)src_virt,
				(u64)size, offset);
		BUG_ON(offset > item->ring->send_aligment_overhead);

		/* Offset sent separately by PCI write. */
		iowrite8(offset, item->remapped);
		memcpy_toio(item->remapped + offset, src_virt, size);
		wmb();
		/* Wait for finish write */
		ioread8(item->remapped);
		ioread8((void *)((uintptr_t)item->remapped + offset + size - 1));
	} else {
		memcpy_toio(item->remapped ,phys_to_virt(item->src_phys), size);
		wmb();
		/* Wait for finish write */
		ioread8((void *)((uintptr_t)item->remapped + size - 1));
	}
}

/*
 * transfer_dma_send - Send data to PCI through DMA.
 *
 * @item - item with data to copy
 * @vdev - address of the vdev
 * @dst - destination DMA address
 * @size - size of the transfer
 *
 */
int
transfer_dma_send(struct buffer_dma_item *item, struct vop_device *vdev,
	    dma_addr_t dst, size_t size)
{
	int err = 0;
	dma_cookie_t cookie;

	/* Member jiffies in item have to be set before call vop_async_dma().
	 * Callback transfer_done_callback() who use jiffies to check that item
	 * is valid can be called before end of this function */
	item->jiffies = get_time_jiff_not_zero();

	if (item->ring->cdev->feature_desc_alignment) {
		dma_addr_t new_dst = ALIGN(dst, PLX_DMA_ALIGN_BYTES);
		dma_addr_t offset_dst = new_dst - dst;
		dma_addr_t new_src = (item->src_phys & ~(PLX_DMA_ALIGN_BYTES - 1));
		dma_addr_t offset_src = item->src_phys - new_src;
		dma_addr_t offset = offset_dst + offset_src;
		item->src_phys_sz = ALIGN(size + offset_src, PLX_DMA_ALIGN_BYTES);

		BUG_ON(item->src_phys == 0);

		dev_dbg(&vdev->dev, "%s use feature alignment src: %llx size:%llx "
			    "new_src:%llx new_size:%lx offset_src %llx offset_dst %llx "
			    "offset %llx dst: %llx new_dst: %llx data %llx\n", __func__,
			    item->src_phys, (u64)size, new_src, item->src_phys_sz,
			    offset_src, offset_dst, offset, dst, new_dst,
			    *(u64*)(phys_to_virt(item->src_phys)));

		/* Offset sent separately by PCI write. */
		if (offset_dst < 1) {
			offset += PLX_DMA_ALIGN_BYTES;
			new_dst += PLX_DMA_ALIGN_BYTES;
		}

		BUG_ON(offset > DMA_MAX_OFFSET);
		iowrite8(offset, item->remapped);

		/* DMA map after set offset in mapped memory */
		item->src_phys_da = dma_map_single(item->ring->dma_dev->dev,
			    phys_to_virt(new_src), item->src_phys_sz,
			    DMA_TO_DEVICE);

		dev_dbg(&vdev->dev, "%s dma map addr_phys: %llx addr_da %llx size %lu\n",
			    __func__, new_src, item->src_phys_da, item->src_phys_sz);

		BUG_ON(item->tx != 0);
		cookie = vop_async_dma(vdev, new_dst, item->src_phys_da,
				item->src_phys_sz, transfer_done_callback, (void *)item, &item->tx);
	} else {
		BUG_ON(item->src_phys != 0);
		BUG_ON(item->tx != 0);
		dev_dbg(&vdev->dev, "%s use intermediate buffer item->buf_da: %llx "
			    "size: %lx dst: %llx\n", __func__, item->buf_da, size, dst);

		if (item->src_phys & (PLX_DMA_ALIGN_BYTES - 1))
			dev_warn(&vdev->dev, "%s destination buffer not alignment to 64 "
				"bytes, dst: %llx\n", __func__, dst);

		cookie = vop_async_dma(vdev, dst, item->buf_da, ALIGN(size,
			    PLX_DMA_ALIGN_BYTES), transfer_done_callback, (void *)item, &item->tx);
	}

	if (dma_submit_error(cookie)) {
		item->jiffies = 0;
		err = cookie;
		dev_err_once_dbg_later(&vdev->dev, "dma error %d\n", err);
	}

	return err;
}

static void *
transfer_ioremap(struct buffer_dma_item *item, struct vop_device *vdev,
		dma_addr_t pa, size_t len)
{
	DEBUG_PERF_LOCAL()
	item->remapped = vdev->hw_ops->ioremap(vdev, pa, len);

	/* Wait for free resources to remap memory */
	if (!item->remapped) {
		DEBUG_PERF_START()
		wait_event_interruptible_timeout(item->ring->cdev->remap_free_queue,
				!item->ring->cdev->ready ||
				(item->remapped = vdev->hw_ops->ioremap(vdev, pa, len)) != NULL,
				msecs_to_jiffies(TIMEOUT_SEND_MS));
		DEBUG_PERF_STOP(item->ring->ioremap_busy)
	}
	return item->remapped;
}

static int
transfer_write(struct buffer_dma_item *item, struct vop_device *vdev)
{
	int err = 0;
	int i;

	// TODO: fix buffer wrapping logic here
	if (item->num_kvecs_to != 2) {
		dev_err(&vdev->dev, "%s unsupported number of descriptors %i\n",
				__func__, item->num_kvecs_to);
		err = -EIO;
		goto end;
	}

	dev_dbg(&vdev->dev, "%s read head_to %i\n", __func__, item->head_to);

	// send heads up interrupt to the peer if needed
	common_dev_notify_used(item->ring->cdev);

	for (i = 0; i < item->num_kvecs_to; i++) {
		struct kvec *v_to = &(item->kvec_to + i)->iov;
		size_t dst_size = v_to->iov_len;
		if (i == 0) {
			/* Copying header is skipped as it is always 0 in our use case. */
			item->bytes_written += dst_size;
		} else if (i == 1) {
			if (dst_size < item->data_size + item->ring->send_aligment_overhead) {
				dev_err(&vdev->dev,
						"%s Write buffer is too small to align size and "
						"addr i: %i v_to->iov_len %llu dst_size %lu "
						"item->data_size %lu\n", __func__, i,
						(u64)v_to->iov_len, dst_size, item->data_size);
				err = -ENOSPC;
				item->bytes_written = 0;
				break;
			}

			item->bytes_written += item->data_size;

			if (!transfer_ioremap(item, vdev, (u64)v_to->iov_base,
					item->data_size + item->ring->send_aligment_overhead)) {
				dev_err(&vdev->dev,
					"%s ioremap error, size %lu\n",
					__func__, item->data_size);
				err = -ENOMEM;
				break;
			}

			dev_dbg(&vdev->dev, "%s transfer TO VEC_DST:%x v:%p remapped: %p "
					"item->data_size %lu\n", __func__, (u32)v_to->iov_len,
					v_to->iov_base, item->remapped, item->data_size);

			if (item->ring->dma_dev) {
				/* Callback transfer_finish_callback() will call transfer_done() */
				err = transfer_dma_send(item, vdev,
						/* Destination dma address */
						(u64)vdev->aper->pa + (item->remapped - vdev->aper->va),
						item->data_size);
				if (err) {
					/* MEMCPY path RECOVERY*/
					/* For issues in DMA driver module switch on memcpy mode.
					 * To reduce dmesg messages, there will be only one
					 * notification about run this path.*/
					dev_err_once_dbg_later(&vdev->dev,
									"%s Transfer DMA error %i, Transfer memcpy, size %lu\n",
									__func__, err, item->data_size);
					transfer_memcpy_send(item, vdev, item->data_size);
					transfer_done(item);
					item->ring->counter_done_transfer++;
					err = 0;
				}
			} else {
				/* MEMCPY path */
				transfer_memcpy_send(item, vdev, item->data_size);
				transfer_done(item);
			}
		}
	}

end:
	return err;
}

static void
transfer_done(struct buffer_dma_item *item)
{
	dev_dbg(item->ring->dev, "%s transfer finish item begin %p \n",
		__func__, item);

	if (!item->status_ready) {

		if (item->head_to != USHRT_MAX) {
			vop_kvec_used(item);
			item->head_to = USHRT_MAX;
			// send heads up interrupt to the peer if needed
			common_dev_notify_used(item->ring->cdev);
		}

		if (item->vringh_tx && item->head_from != USHRT_MAX) {
			put_descriptors(item->vringh_tx, item->head_from,
					item->bytes_read);
			item->head_from = USHRT_MAX;
		}

	} else {
		dev_err(item->ring->dev, "%s item deinitialized: %p\n", __func__, item);
	}

	/* clear IN_USE flag */
	item->kvec_to->flags = 0;

	buffer_dma_ring_item_reset(item);

	dev_dbg(item->ring->dev, "%s transfer finish item end %p \n", __func__,
			item);

}

void transfer_done_callback(void *data)
{
	struct buffer_dma_item *item = (struct buffer_dma_item *)data;
	struct buffers_dma_ring *ring = item->ring;
	struct vop_dev_common *cdev = ring->cdev;
	u16 ind_dma_next = (ring->counter_done_transfer) & VOP_RING_SIZE_MASK;
	u16 ind_dma_end = (item->id + 1) & VOP_RING_SIZE_MASK;

	struct vop_device *vdev = ring->vdev;
	struct vop_info *vi = vdev->priv;
	struct dma_chan *vop_ch = vi->dma_ch;

	enum dma_status status  = dma_async_is_tx_complete(vop_ch,
			item->tx->cookie , NULL, NULL);

	if (status == DMA_ERROR) {
		/* MEMCPY path RECOVERY*/
		dev_warn(&vdev->dev,
						"%s  Failed DMA transfer error %i, "
						"Attempting again by using memcpy, size %lu\n",
						__func__, status, item->data_size);
		transfer_memcpy_send(item, vdev, item->data_size);
	}


	item->tx = NULL;

	BUILD_BUG_ON(VOP_RING_SIZE >
		(1ULL<<(sizeof(ring->counter_done_transfer)*8)));
	BUILD_BUG_ON(VOP_RING_SIZE > (1ULL<<(sizeof(ind_dma_next)*8)));
	BUILD_BUG_ON(VOP_RING_SIZE > (1ULL<<(sizeof(ind_dma_end)*8)));

	dev_dbg(item->ring->dev, "%s transfer %p idlast: %u, id: %u\n",
			__func__, data, ring->counter_done_transfer, item->id);

	if (ind_dma_next == ind_dma_end) {
		printk(KERN_ERR "%s Wrong id %u callback ind_dma_next %u\n",
					__func__, item->id, ind_dma_next);
	}

	if (ind_dma_next != item->id) {
		unsigned missed = (item->id + VOP_RING_SIZE - ind_dma_next)
				& VOP_RING_SIZE_MASK;
		printk(KERN_ERR
			"%s missing dma callbacks %u itemId %u-%u \n",
			__func__, missed, ind_dma_next, item->id);
	}

	while(READ_ONCE(cdev->ready) &&  ind_dma_next != ind_dma_end ) {
		struct buffer_dma_item *item = &ring->items[ind_dma_next];
		if (item->jiffies) {
			dev_dbg(ring->dev, "%s transfer for id: %u to %u item: %p\n",
					__func__, ring->counter_done_transfer, ind_dma_next, item);
			item->jiffies = 0;
			transfer_done(item);
		} else {
			dev_err(ring->dev, "%s Try to finish not started item %u "
					"ind_dma_next %u ind_dma_end %u\n",
					__func__, item->id, ind_dma_next, ind_dma_end);
		}
		ind_dma_next = (ind_dma_next + 1) & VOP_RING_SIZE_MASK;
		item->ring->counter_done_transfer++;
	}
}

/* move data from one vring to another */
static int
transfer(void *data)
{
	struct buffer_dma_item *item = (struct buffer_dma_item *)data;
	int err = 0;
	struct vop_device *vdev;
	struct buffers_dma_ring *ring;
	BUG_ON(!item);

	ring = item->ring;
	vdev = ring->vdev;
	dev_dbg(&vdev->dev, "%s item %p\n", __func__, item);

	err = transfer_read(item, vdev);
	if (err) {
		transfer_done(item);
		goto end;
	}

	do {
		err = transfer_wait(item, vdev);
	} while (-EBUSY == err);

	if (err) {
		dev_err(&vdev->dev,"%s transfer_wait error %i , item->id %u\n",
			__func__, err, item->id);
		goto end;
	}

	if (!ring->cdev->write_in_thread) {
		err = transfer_write(item, vdev);
		if (err) {
			transfer_done(item);
		}
	} else {
		dev_dbg(&vdev->dev, "DMA send task item->id %u, "
			"uuid_dma_send %u\n", item->id, ring->counter_dma_send);
		BUG_ON(item->id != (ring->counter_dma_send & VOP_RING_SIZE_MASK));
		BUILD_BUG_ON(VOP_RING_SIZE > (1ULL<<(sizeof(ring->counter_dma_send)*8)));
		ring->counter_dma_send++;
		complete(&ring->wait_dma_send);
	}

end:
	if (err)
		dev_err(&vdev->dev,"%s global error %i , item->id %u\n", __func__,
			err, item->id);

	return err;
}

static int sync_descr_wait_for_ready(struct vop_dev_common *cdev)
{
	struct vop_device *vdev = cdev->vdev;
	int ret = 0;

	if (!READ_ONCE(cdev->ready)) {
		dev_dbg(&vdev->dev, "%s not ready\n", __func__);
		ret = -ENODEV;
		goto end;
	}


	if (!device_ready(cdev) || !peer_device_ready(cdev))
		dev_dbg(&vdev->dev, "%s devices not ready self:%d peer:%d \n",
			__func__, device_ready(cdev), peer_device_ready(cdev));

	do {
		msleep(100);
	} while ((!device_ready(cdev) || !peer_device_ready(cdev)) &&
		READ_ONCE(cdev->ready));

	if (!READ_ONCE(cdev->ready)) {
		dev_dbg(&vdev->dev, "%s device removed\n", __func__);
		ret = -ENODEV;
	}

end:
	dev_dbg(&vdev->dev, "%s wait end ret = %i\n", __func__, ret);
	return ret;
}

int sync_descriptors_dma_task(void *data)
{
	struct vop_dev_common *cdev = (struct vop_dev_common *)data;
	struct vop_device *vdev = cdev->vdev;
	struct buffers_dma_ring *ring = cdev->buffers_ring;
	u16 counter_dma_last = 0;
	struct buffer_dma_item *item;
	int err;

	dev_dbg(ring->dev, "%s ready \n", __func__);

	BUILD_BUG_ON(VOP_RING_SIZE > (1ULL<<(sizeof(counter_dma_last)*8)));
	BUILD_BUG_ON(VOP_RING_SIZE > (1ULL<<(sizeof(ring->counter_dma_send)*8)));


	while (READ_ONCE(cdev->ready)) {
		err = vop_wait_for_completion(cdev, &ring->wait_dma_send, TIMEOUT_SEND_MS);

		if (-EBUSY == err && counter_dma_last != ring->counter_dma_send) {
				dev_err(&cdev->vdev->dev,"%s Timeout pool on transfer_finish "
					"uuid_dma_last %u, counter_dma_send %u\n", __func__,
					counter_dma_last, ring->counter_dma_send);
		}

		while(READ_ONCE(cdev->ready) && counter_dma_last != ring->counter_dma_send) {
			item = &ring->items[counter_dma_last & VOP_RING_SIZE_MASK];
			dev_dbg(ring->dev, "%s transfer for uuid_dma_last %u, "
				"counter_dma_send %u, item:%p item->id %u \n", __func__,
				counter_dma_last, ring->counter_dma_send, item, item->id);

			if (!item->status_ready) {
				err = transfer_write(item, vdev);
				if (err) {
					transfer_done(item);
				}
			}

			++counter_dma_last;
		}
	}

	complete(&cdev->vdm_complete);
	do_exit(0);
	return 0;
}

void sync_descriptors_read_task_step(struct vop_dev_common *cdev)
{
	struct vop_device *vdev = cdev->vdev;
	struct vop_vringh *vringh_tx = cdev->vringh_tx;
	struct buffers_dma_ring *ring = cdev->buffers_ring;
	struct buffer_dma_item *item = NULL;
	int err;

	dev_dbg(&vdev->dev, "%s wait on buff\n", __func__);

	item = buffer_dma_ring_get_read(ring);
	dev_dbg(&vdev->dev, "%s wait on buff item %p\n", __func__, item);
	if (!item) {
		DEBUG_PERF_LOCAL()
		DEBUG_PERF_START()

		/* wait for an item for read descriptor */
		while (-EBUSY == vop_wait_for_completion(cdev,
					&ring->wait_avail_read,
					TIMEOUT_SEND_MS))
		{
			dev_warn(&cdev->vdev->dev,
				"%s timeout waiting for free read item (%u ms)",
				__func__, TIMEOUT_SEND_MS);
		}

		DEBUG_PERF_STOP(ring->item)
	}

	while (READ_ONCE(cdev->ready) && item) {

		if (item->head_from == USHRT_MAX && !item->vringh_tx) {
			vop_common_get_descriptors(vdev, vringh_tx,
					&item->head_from, &item->k_from, true);
		}

		if (item->head_from != USHRT_MAX) {
			dev_dbg(&vdev->dev, ">>>>>>>>>>>>>>>>>>>>>>>>>>>B\n");
			dev_dbg(&vdev->dev, "%s Get descriptor %i\n", __func__,
						item->head_from);

			item->vringh_tx = vringh_tx;
			if (item->k_from.used > 2) {
				dev_err(&vdev->dev, "%s too many descriptors, "
					"k_from->used %i\n", __func__,
					item->k_from.used);
			}

			transfer(item);

			item = NULL;
			dev_dbg(&vdev->dev, ">>>>>>>>>>>>>>>>>>>>>>>>>>>E\n");
		} else {
			err = vop_wait_for_completion(cdev, &cdev->sync_desc_read, TIMEOUT_SEND_MS);
			if (-EBUSY == err) {
				err = vop_common_get_descriptors(vdev, vringh_tx, &item->head_from,
						&item->k_from, true);
				if (err) {
					dev_err(&cdev->vdev->dev,"%s Timeout pool on "
							"get descriptions to send! err: %i, head: %i\n", __func__, err, item->head_from);
					continue;
				}
			}
		}
	}
}

static int sync_descriptors_read_task(void *data)
{
	struct vop_dev_common *cdev = (struct vop_dev_common *)data;
	struct vop_vringh *vringh_tx = cdev->vringh_tx;
	BUG_ON(!vringh_tx->vq);

	while (READ_ONCE(cdev->ready)) {
		sync_descriptors_read_task_step(cdev);
	}

	complete(&cdev->vrd_complete);
	do_exit(0);
	return 0;
}

static int spin_for_used_descriptors_task(void *data)
{
	struct vop_dev_common *cdev = (struct vop_dev_common *)data;
	vop_spin_for_used_descriptors(cdev);
	complete(&cdev->vsd_complete);
	do_exit(0);
	return 0;
}

/*
 * common_dev_init_mode - Configure transfer mode, depending of active features.
 *
 * @cdev - dev_common structure
 *
 */
static void common_dev_init_mode(struct vop_dev_common *cdev)
{
	struct vop_device *vdev = cdev->vdev;
	u8 *features;
	u8 *features_peer;
	struct vca_device_desc *desc = cdev->dd_self;
	struct vca_device_desc *desc_peer = cdev->dd_peer;
	unsigned int bits, bits_peer;
	bool use_dma = false;
#ifdef VIRTIO_NET_F_OFFSET_RXBUF
	const u8 test_bit = VIRTIO_NET_F_OFFSET_RXBUF;
#else /* VIRTIO_NET_F_OFFSET_RXBUF */
	const u8 test_bit = __VIRTIO_NET_F_OFFSET_RXBUF;
	dev_warn(&vdev->dev, "%s Transfer mode: Feature VIRTIO_NET_F_OFFSET_RXBUF "
		    "not implemented in kernel\n", __func__);
#pragma message "Feature VIRTIO_NET_F_OFFSET_RXBUF not implemented in kernel"
#endif /* VIRTIO_NET_F_OFFSET_RXBUF */

	features = vca_vq_features(desc) + desc->feature_len;
	bits = min_t(unsigned, desc->feature_len, sizeof(unsigned long)) * 8;
	features_peer = vca_vq_features(desc_peer) + desc_peer->feature_len;
	bits_peer = min_t(unsigned, desc_peer->feature_len, sizeof(unsigned long)) * 8;

	cdev->feature_desc_alignment = VOP_CHECK_FEATURE(features_peer, bits_peer,
						test_bit);

	if (cdev->vdev->dma_ch) {
		cdev->write_in_thread = !cdev->feature_desc_alignment;
		use_dma = true;
	} else {
		cdev->write_in_thread = true;
		use_dma = false;
	}

	dev_info(&vdev->dev, "%s Transfer mode: %s%s%s\n", __func__,
			use_dma?"DMA":"memcpy",
			cdev->feature_desc_alignment? " with feature fix alignment":
			" with intermediate buffer",
			cdev->write_in_thread? ", write in thread":"");
}

static int common_dev_init_task(void *data)
{
	struct vop_dev_common *cdev = (struct vop_dev_common *)data;
	struct vop_device *vdev = cdev->vdev;
	int ret = 0;
	char name_task[16];
	struct plx_device *xdev = dev_get_drvdata(cdev->vdev->dev.parent);
	unsigned char card_id = xdev->card_id;
	unsigned char bus_number = xdev->pdev->bus->number;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,1,0)
	unsigned char nr_nodes = max(1U, nr_online_nodes);
#else
	unsigned char nr_nodes = max(1, nr_online_nodes);
#endif
	int node = bus_number / ( 256 / nr_nodes);

	dev_dbg(&vdev->dev,"%s card %u, bus number %u\n",
					__func__, card_id, bus_number);

	if (sync_descr_wait_for_ready(cdev)) {
		goto err;
	}

	common_dev_init_mode(cdev);

	ret = buffer_dma_ring_init(cdev);
	if (ret) {
		dev_err(&vdev->dev, "%s request init buffers dma failed ret = %i\n",
			__func__, ret);
		goto end;
	}

	snprintf(name_task, sizeof(name_task), "vrd%u_%u", card_id, bus_number);
	kthread_run_on_node(sync_descriptors_read_task, cdev, node, name_task);

	if (cdev->write_in_thread) {
		snprintf(name_task, sizeof(name_task), "vdm%u_%u", card_id, bus_number);
		kthread_run_on_node(sync_descriptors_dma_task, cdev, node, name_task);
	}

	snprintf(name_task, sizeof(name_task), "vsd%u_%u", card_id, bus_number);
	kthread_run_on_node(spin_for_used_descriptors_task, cdev, node, name_task);

	cdev->ready = VOP_DEV_READY_STATE_WORK;

	goto end;

err:
	complete(&cdev->vrd_complete);
	if (cdev->write_in_thread) {
		complete(&cdev->vdm_complete);
	}
	complete(&cdev->vsd_complete);

end:
	do_exit(ret);
	return ret;
}

int common_dev_start(struct vop_dev_common *cdev)
{
	char name_task[16];
	struct plx_device *xdev = dev_get_drvdata(cdev->vdev->dev.parent);
	unsigned char card_id = xdev->card_id;
	unsigned char bus_number = xdev->pdev->bus->number;

	if (cdev->ready)
		return 0;

	dev_dbg(&cdev->vdev->dev,"%s card %u, bus number %u\n",
				__func__, card_id, bus_number);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 13, 0)
	reinit_completion(&cdev->sync_desc_read);
	reinit_completion(&cdev->vrd_complete);
	reinit_completion(&cdev->vdm_complete);
	reinit_completion(&cdev->vsd_complete);
#else
	init_completion(&cdev->sync_desc_read);
	init_completion(&cdev->vrd_complete);
	init_completion(&cdev->vdm_complete);
	init_completion(&cdev->vsd_complete);
#endif

	cdev->feature_desc_alignment = false;

	dev_dbg(&cdev->vdev->dev,"%s card %u, bus number %u\n",
				__func__, card_id, bus_number);

	cdev->ready = VOP_DEV_READY_STATE_STARTING;

	snprintf(name_task, sizeof(name_task), "vinit%u_%u", card_id, bus_number);
	kthread_run(common_dev_init_task, cdev, name_task);

	return 0;
}

void common_dev_stop(struct vop_dev_common *cdev)
{
	long res;
	u8 ready_old;

	if (!cdev || !cdev->ready)
		return;

	dev_dbg(&cdev->vdev->dev, "%s stoping device\n", __func__);

	ready_old = cdev->ready;
	cdev->ready = VOP_DEV_READY_STATE_STOP;
	wmb();

	vop_kvec_buff_reset(&cdev->kvec_buff);

	complete(&cdev->sync_desc_read);
	wake_up_all(&cdev->remap_free_queue);

	/*
	 * To protect that task common_dev_init_task() not started threads
	 * in error case then code can stack on wait.
	 * It can happens example in out of memory case.
	*/
	if (ready_old == VOP_DEV_READY_STATE_WORK) {
		res = wait_for_completion_interruptible(&cdev->vrd_complete);
		dev_dbg(&cdev->vdev->dev, "vrd_finished\n");
		if (res < 0)
			goto end;
		if (cdev->write_in_thread) {
			res = wait_for_completion_interruptible(&cdev->vdm_complete);
			dev_dbg(&cdev->vdev->dev, "vdm finished\n");
			if (res < 0)
				goto end;
		}
		res = wait_for_completion_interruptible(&cdev->vsd_complete);
		dev_dbg(&cdev->vdev->dev, "vsd finished\n");
		if (res < 0)
			goto end;
	}

end:
	buffer_dma_ring_deinit(cdev->buffers_ring);
	cdev->buffers_ring = NULL;
}

int common_dev_init(
		struct vop_dev_common *cdev,
		struct vop_device *vdev,
		struct vop_vringh *vringh_tx,
		struct vop_vringh *vringh_rcv,
		int num_write_descriptors,
		vop_send_heads_up_pfn send_heads_up_pfn,
		struct vca_device_desc *dd_self,
		struct vca_device_desc *dd_peer)
{
	int ret = 0;

	dev_dbg(&vdev->dev, "%s:%i init cdev %p, vdev %p, vringh_tx %p, "
		" num_desc %d\n", __func__, __LINE__, cdev, vdev, vringh_tx,
		num_write_descriptors);

	cdev->ready = VOP_DEV_READY_STATE_STOP;
	cdev->buffers_ring = NULL;
	cdev->vringh_tx = vringh_tx;
	cdev->vringh_rcv = vringh_rcv;
	cdev->vdev = vdev;

	cdev->dd_self = dd_self;
	cdev->dd_peer = dd_peer;

	cdev->send_heads_up = send_heads_up_pfn;

	init_completion(&cdev->sync_desc_read);
	init_completion(&cdev->vrd_complete);
	init_completion(&cdev->vdm_complete);
	init_completion(&cdev->vsd_complete);

	init_waitqueue_head(&cdev->heads_up_used_irq.wq);
	init_waitqueue_head(&cdev->heads_up_avail_irq.wq);

	cdev->heads_up_avail_irq.op = vop_notify_available;
	cdev->heads_up_used_irq.op = vop_notify_used;

	init_waitqueue_head(&cdev->remap_free_queue);

	ret = vop_kvec_buff_init(&cdev->kvec_buff, vdev, num_write_descriptors);
	if (ret) {
		dev_err(&vdev->dev, "%s failed to init kvecs buffer\n",
			__func__);
		goto err;
	}
	return 0;

err:
	common_dev_deinit(cdev, vdev);
	return ret;
}

void common_dev_deinit(struct vop_dev_common *cdev, struct vop_device *vdev)
{
	common_dev_stop(cdev);
	vop_kvec_unmap_buf(&cdev->kvec_buff, vdev);
	vop_kvec_buff_deinit(&cdev->kvec_buff, vdev);
}


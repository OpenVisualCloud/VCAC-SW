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
#include <linux/delay.h>
#include <linux/kthread.h>
#include "vop_common.h"
#include "vop_kvec_buff.h"
#include "../common/vca_common.h"
#include "vop_main.h"

#define PLX_DMA_ALIGN_BYTES	64
#define DMA_MAX_OFFSET 127
#define DMA_ALIGN_OVERHEAD_SIZE ((DMA_MAX_OFFSET) + (PLX_DMA_ALIGN_BYTES))

#define MTU_DEFAULT		(1518 + DMA_ALIGN_OVERHEAD_SIZE)
#define MTU_JUMBO		(9018 + DMA_ALIGN_OVERHEAD_SIZE)
#define MTU_MAX		(65540 + DMA_ALIGN_OVERHEAD_SIZE)

#define KVEC_BUF_SMALL 0
#define KVEC_BUF_JUMBO 1
#define KVEC_BUF_BIG (KVEC_BUF_NUM -1)

#define KVEC_INDEX_UPDATE_PERIOD 16

/* timeout for waiting for heads up IRQ */
#define HEADS_UP_WAIT_TIMEMOUT_MS 500

/* minimum time period between sending two heads up interrupts */
#define HEADS_UP_NOTIFICATION_TIMEOUT_MS 2

/* how long to stay in heads up state after receiving interrupt */
#define HEADS_UP_TIMEOUT_MS (2 * HEADS_UP_NOTIFICATION_TIMEOUT_MS)

/*
 * get_ring_id_write - returns best match for kvec ring buffer id to store write
 * descriptor of given size
 */
static inline int get_ring_id_write(size_t size)
{
	int ret = KVEC_BUF_SMALL;

#if KVEC_BUF_NUM == 2
	if (size < MTU_DEFAULT)
		ret = -EDOM;
	else if (size < MTU_MAX)
		ret = KVEC_BUF_SMALL;
	else
		ret = KVEC_BUF_BIG;
#elif KVEC_BUF_NUM == 3
	if (size < MTU_DEFAULT)
		ret = -EDOM;
	else if (size < MTU_JUMBO)
		ret = KVEC_BUF_SMALL;
	else if (size < MTU_MAX)
		ret = KVEC_BUF_JUMBO;
	else
		ret = KVEC_BUF_BIG;
#else
#error "KVEC_BUF_NUM Out of range [2,3]"
#endif

	return ret;
}

/*
 * vop_kvec_size_to_ring_id - returns best match for kvec ring buffer id
 * to look for write descriptor capable of storing data from read descriptor
 * of given size.
 */
int vop_kvec_size_to_ring_id(struct vop_dev_common *cdev, size_t size)
{
	struct vop_kvec_buf_local *kvec_buf_to = &cdev->kvec_buff.local_write_kvecs;
	int ring_id;
	for (ring_id = 0; ring_id < KVEC_BUF_NUM; ++ring_id) {
		if (size <= *kvec_buf_to->rings[ring_id].send_max_size) {
			return ring_id;
		}
	}

	/* Until send_max_size are known for rings, wait for the largest buffer */
	return KVEC_BUF_NUM - 1;
}

/* check whether heads up condition is fulfilled for given irq*/
static inline bool heads_up(struct vop_heads_up_irq* irq)
{
	u64 irq_ts;
	bool ret;
	irq_ts = irq->rcv_ts;
	ret = jiffies_to_msecs(jiffies - irq_ts) <= HEADS_UP_TIMEOUT_MS;
	return ret;
}

/* Check whether heads up irq had been send within HEADS_UP_NOTIFICATION_TIMEOUT_MS
 * and send one if not */
void vop_send_heads_up(struct vop_dev_common *cdev, struct vop_heads_up_irq* irq)
{
	u64 irq_ts;
	bool send;

	irq_ts = irq->sent_ts;
	send = jiffies_to_msecs(jiffies - irq_ts) >=  HEADS_UP_NOTIFICATION_TIMEOUT_MS;

	if (send) {
		irq->sent_ts = jiffies;
		cdev->send_heads_up(cdev, irq->op);
	}
}


/* returns true if there are some new used descriptors */
static inline bool check_new_used_desc(struct vop_dev_common *cdev, void *a)
{
	struct vop_used_kiov_ring *used_ring = &cdev->kvec_buff.local_write_kvecs.used_ring;
	return READ_ONCE(*used_ring->cnt) != used_ring->last_cnt;
}

/* returns true if there are new write descriptors available */
static inline bool check_new_avail_desc_ring_id(struct vop_dev_common *cdev, int id)
{
	struct vop_kvec_ring *ring = cdev->kvec_buff.local_write_kvecs.rings + id;
	return READ_ONCE(*ring->cnt) != ring->last_cnt;
}


static inline int wait_for_used_desc(struct vop_dev_common *cdev, struct vop_heads_up_irq *irq, void* data)
{
    struct device *dev = &cdev->vdev->dev;
    bool dbg = false;
    dev_dbg( dev, "%s start waiting for used_desc heads up? %d\n", __func__, heads_up(irq));
    do {
        int ret;
        if (dbg) dev_dbg(dev, "%s try polling\n", __func__);
        /* poll for data if heads up irq received */
        while (cdev->ready &&
               !check_new_used_desc(cdev, data) && heads_up(irq))
            usleep_range(5,10);
        if (!check_new_used_desc(cdev, data)) {
            if (dbg) dev_dbg(dev, "%s heads up expired\n",
                             __func__);
            ret = wait_event_interruptible_timeout(
                        irq->wq,
                        !cdev->ready || check_new_used_desc(cdev, data) ||
                        heads_up(irq),
                        msecs_to_jiffies(HEADS_UP_WAIT_TIMEMOUT_MS));
            if (ret < 0)
                return ret;
            else if (ret == 0 && check_new_used_desc(cdev, data))
                dev_warn(dev,
                         "%s missing heads up irq\n",
                         __func__);
            else if (dbg) dev_dbg(dev, "%s finish waiting "
                                       "heads_up? %d coniditon? %d\n", __func__,
                                  heads_up(irq), check_new_used_desc(cdev, data));
            vop_send_heads_up(cdev,irq); // VCASS-3044
        }
    } while (!check_new_used_desc(cdev, data) && cdev->ready);
    if (!cdev->ready)
        return -ENODEV;
    else {
        if (dbg) dev_dbg(dev, "%s condition is true\n", __func__);
        return 0;
    }
}


static inline int wait_for_avail_desc_ring_id(struct vop_dev_common *cdev, struct vop_heads_up_irq *irq, int data)
{
    struct device *dev = &cdev->vdev->dev;
    bool dbg = false;
    dev_dbg(dev, "%s start waiting for avail_desc_ring_id heads up? %d\n", __func__, heads_up(irq));
    do {
        int ret;
        if (dbg) dev_dbg(dev, "%s try polling\n", __func__);
        /* poll for data if heads up irq received */
        while (cdev->ready &&
               !check_new_avail_desc_ring_id(cdev, data) && heads_up(irq))
            usleep_range(5,10);
        if (!check_new_avail_desc_ring_id(cdev, data)) {
            if (dbg) dev_dbg(dev, "%s heads up expired\n",
                             __func__);
            ret = wait_event_interruptible_timeout(
                        irq->wq,
                        !cdev->ready || check_new_avail_desc_ring_id(cdev, data) ||
                        heads_up(irq),
                        msecs_to_jiffies(HEADS_UP_WAIT_TIMEMOUT_MS));
            if (ret < 0)
                return ret;
            else if (ret == 0 && check_new_avail_desc_ring_id(cdev, data))
                dev_warn(dev,
                         "%s missing heads up irq\n",
                         __func__);
            else if (dbg) dev_dbg(dev, "%s finish waiting "
                                       "heads_up? %d coniditon? %d\n", __func__,
                                  heads_up(irq), check_new_avail_desc_ring_id(cdev, data));
        }
    } while (!check_new_avail_desc_ring_id(cdev, data) && cdev->ready);
    if (!cdev->ready)
        return -ENODEV;
    else {
        if (dbg) dev_dbg(dev, "%s condition is true\n", __func__);
        return 0;
    }
}


int vop_wait_for_avail_desc(struct vop_dev_common *cdev,
		struct vop_heads_up_irq *avail_hu_irq, int ring_id)
{
	return wait_for_avail_desc_ring_id(cdev, avail_hu_irq, ring_id);
}

static inline size_t get_write_kvecs_buf_size(int num)
{
	return sizeof(struct vop_peer_kvec_buf_header) +
		KVEC_BUF_NUM * num * sizeof(struct vop_peer_kvec) +
		num * sizeof(struct vop_peer_used_kiov);
}

void vop_kvec_buff_reset(struct vop_kvec_buff *kvec_buff)
{
	int ring_id;

	*kvec_buff->local_write_kvecs.used_ring.cnt = 0;
	kvec_buff->local_write_kvecs.used_ring.last_cnt = 0;

	for (ring_id = 0; ring_id < KVEC_BUF_NUM; ++ring_id) {
		*kvec_buff->local_write_kvecs.rings[ring_id].cnt = 0;
		kvec_buff->local_write_kvecs.rings[ring_id].last_cnt = 0;
	}
}

int vop_kvec_buff_init(struct vop_kvec_buff *kvec_buff,
		struct vop_device *vdev, int num_write_descriptors)
{
	struct vop_peer_kvec_buf_header *hdr;
	size_t write_kvecs_buf_size;
	int ret = 0;
	int ring_id;
	unsigned buff_start_offset; /* start offset of current kvec ring */

	write_kvecs_buf_size =
			PAGE_ALIGN(get_write_kvecs_buf_size(num_write_descriptors));

	dev_dbg(&vdev->dev, "%s:%i init kvec_buff %p, num_desc %d "
			"write_kvecs_buf_size %lu\n", __func__, __LINE__, kvec_buff,
			num_write_descriptors, write_kvecs_buf_size);

	kvec_buff->local_write_kvecs.pa = 0;

	kvec_buff->local_write_kvecs.pages =
		__get_free_pages(GFP_KERNEL | __GFP_ZERO | GFP_DMA,
				 get_order(write_kvecs_buf_size));

	if (!kvec_buff->local_write_kvecs.pages) {
		dev_err(&vdev->dev, "%s failed to alloc write kvecs buffer\n",
			__func__);
		ret = -ENOMEM;
		goto err;
	}

	kvec_buff->local_write_kvecs.pa = dma_map_single(&vdev->dev,
					(void*)kvec_buff->local_write_kvecs.pages,
					write_kvecs_buf_size,
					DMA_BIDIRECTIONAL);
	ret = dma_mapping_error(&vdev->dev, kvec_buff->local_write_kvecs.pa);
	if (ret) {
		dev_err(&vdev->dev, "%s failed to map write kvecs buffer %d\n",
			__func__, ret);
		kvec_buff->local_write_kvecs.pa = 0;
		free_pages(kvec_buff->local_write_kvecs.pages,
			   get_order(write_kvecs_buf_size));
		kvec_buff->local_write_kvecs.pages = 0;
		goto err;
	}

	CHECK_DMA_ZONE(&vdev->dev, kvec_buff->local_write_kvecs.pa);

	kvec_buff->remote_write_kvecs.is_update = false;
	kvec_buff->remote_write_kvecs.mapped = false;
	kvec_buff->local_write_kvecs.size = write_kvecs_buf_size;
	hdr = (struct vop_peer_kvec_buf_header *)kvec_buff->local_write_kvecs.pages;

	mutex_init(&kvec_buff->local_write_kvecs.mutex);

	buff_start_offset = 0;
	for (ring_id = 0; ring_id < KVEC_BUF_NUM; ++ring_id) {
		kvec_buff->local_write_kvecs.rings[ring_id].num = num_write_descriptors;
		kvec_buff->local_write_kvecs.rings[ring_id].cnt =
				&hdr->idx[ring_id];
		kvec_buff->local_write_kvecs.rings[ring_id].buf =
				&hdr->buff[buff_start_offset];
		buff_start_offset += kvec_buff->local_write_kvecs.rings[ring_id].num;

		kvec_buff->local_write_kvecs.rings[ring_id].send_max_size_local = 0;
		kvec_buff->local_write_kvecs.rings[ring_id].send_max_size =
				&hdr->send_max_size[ring_id];
		kvec_buff->remote_write_kvecs.rings[ring_id].send_max_size_local = 0;
		kvec_buff->remote_write_kvecs.rings[ring_id].send_max_size = NULL;

		kvec_buff->local_write_kvecs.rings[ring_id].stats_num = 0;
		kvec_buff->remote_write_kvecs.rings[ring_id].stats_num = 0;
	}

	/* right after available kvecs rings is the used kvec ring */
	kvec_buff->local_write_kvecs.used_ring.buf = (struct vop_peer_used_kiov*)&hdr->buff[buff_start_offset];
	kvec_buff->local_write_kvecs.used_ring.num = num_write_descriptors;
	kvec_buff->local_write_kvecs.used_ring.cnt = &hdr->used_idx;
	spin_lock_init(&kvec_buff->local_write_kvecs.used_ring.lock);

	vop_kvec_buff_reset(kvec_buff);

	vringh_kiov_init(&kvec_buff->remote_write_kvecs.kiov, NULL, 0);

err:
	return ret;
}

void vop_kvec_buff_deinit(struct vop_kvec_buff *kvec_buff,
		struct vop_device *vdev)
{
	vop_kvec_unmap_buf(kvec_buff, vdev);

	vringh_kiov_cleanup(&kvec_buff->remote_write_kvecs.kiov);

	if (kvec_buff->local_write_kvecs.pa) {
		dma_unmap_single(&vdev->dev, kvec_buff->local_write_kvecs.pa,
				kvec_buff->local_write_kvecs.size, DMA_BIDIRECTIONAL);
		kvec_buff->local_write_kvecs.pa = 0;
	}

	if (kvec_buff->local_write_kvecs.pages) {
		free_pages(kvec_buff->local_write_kvecs.pages,
			    get_order(kvec_buff->local_write_kvecs.size));
		kvec_buff->local_write_kvecs.pages = 0;
	}
}


void vop_kvec_unmap_buf(
		struct vop_kvec_buff *kvec_buff,
		struct vop_device *vdev)
{
	int ring_id;
	int try_wait = 5000;

	kvec_buff->remote_write_kvecs.mapped = false;
	while (READ_ONCE(kvec_buff->remote_write_kvecs.is_update) && --try_wait) {
		msleep(1);
	}

	if (!try_wait) {
		dev_err(&vdev->dev, "%s TIMEOUT Wait to finish "
				"vop_kvec_buf_update() !\n", __func__);
	}

	if (kvec_buff->remote_write_kvecs.va) {
		vdev->hw_ops->iounmap(vdev, kvec_buff->remote_write_kvecs.va);
		kvec_buff->remote_write_kvecs.va = NULL;
		for (ring_id = 0; ring_id < KVEC_BUF_NUM; ++ring_id) {
			kvec_buff->remote_write_kvecs.rings[ring_id].cnt = NULL;
			kvec_buff->remote_write_kvecs.rings[ring_id].buf = NULL;
			kvec_buff->remote_write_kvecs.rings[ring_id].send_max_size = NULL;
		}
	}
}

int vop_kvec_map_buf(
		struct vop_kvec_buff *kvec_buff,
		struct vop_device *vdev,
		int num,
		dma_addr_t pa)
{
	size_t size = PAGE_ALIGN(get_write_kvecs_buf_size(num));
	void *va;
	struct vop_peer_kvec_buf_header *hdr;
	int ring_id;
	unsigned buff_start_offset; /* start offset of current kvec ring */

	if (kvec_buff->remote_write_kvecs.va) {
		dev_dbg(&vdev->dev, "%s freeing old KVEC buff mapping\n", __func__);
		vop_kvec_unmap_buf(kvec_buff, vdev);
		kvec_buff->remote_write_kvecs.va = NULL;
	}

	va = vdev->hw_ops->ioremap(vdev, pa, size);
	if (!va) {
		dev_err(&vdev->dev, "%s failure mapping kvec buffer\n",
			__func__);
		return -ENOMEM;
	}

	hdr = (struct vop_peer_kvec_buf_header *)va;

	kvec_buff->remote_write_kvecs.pa = pa;
	kvec_buff->remote_write_kvecs.va = va;

	buff_start_offset = 0;
	for (ring_id = 0; ring_id < KVEC_BUF_NUM; ++ring_id) {
		kvec_buff->remote_write_kvecs.rings[ring_id].cnt =
				&hdr->idx[ring_id];

		kvec_buff->remote_write_kvecs.rings[ring_id].last_cnt =
				*kvec_buff->remote_write_kvecs.rings[ring_id].cnt;

		kvec_buff->remote_write_kvecs.rings[ring_id].num = num;

		kvec_buff->remote_write_kvecs.rings[ring_id].buf =
				&hdr->buff[buff_start_offset];
		buff_start_offset += kvec_buff->remote_write_kvecs.rings[ring_id].num;

		kvec_buff->remote_write_kvecs.rings[ring_id].send_max_size =
				&hdr->send_max_size[ring_id];
	}

	/* used kiovs buffer */
	spin_lock_init(&kvec_buff->remote_write_kvecs.used_ring.lock);
	kvec_buff->remote_write_kvecs.used_ring.cnt =  &hdr->used_idx;
	kvec_buff->remote_write_kvecs.used_ring.last_cnt = *kvec_buff->remote_write_kvecs.used_ring.cnt;
	kvec_buff->remote_write_kvecs.used_ring.num = num;
	/* right after available kvecs rings is the used kvec ring */
	kvec_buff->remote_write_kvecs.used_ring.buf = (struct vop_peer_used_kiov*) &hdr->buff[buff_start_offset];

	kvec_buff->remote_write_kvecs.mapped = true;

	return 0;
}

/**
 * vop_kvec_put - put decriptors from local vring to a ring buffer
 * allocated by the other host. Detect big or small ring buffer.
 * Return buffer index.
 */
static inline int vop_kvec_put(struct vop_dev_common *cdev,
		struct vringh_kiov* wiov, u16 *head)
{
	struct vop_device *vdev = cdev->vdev;
	struct vop_kvec_buff *kvec_buff = &cdev->kvec_buff;
	u16 idx;
	int i;
	struct vop_peer_kvec peer_kvec[2];
	struct vop_kvec_ring *ring = NULL;
	size_t size = 0;
	int ring_id;

	if (wiov->used > 2) {
		dev_warn(&vdev->dev, "%s num descriptors: %i\n",
			__func__, wiov->used);
	}

	for (i=0; i < wiov->used && i < 2; i++) {
		peer_kvec[i].iov  = wiov->iov[i];
		peer_kvec[i].head = *head;
		peer_kvec[i].flags = 0;
		if (i != 0) {
			size =  peer_kvec[i].iov.iov_len;
		}
	}

	ring_id = get_ring_id_write(size);
	if (ring_id < 0) {
		dev_err(&vdev->dev, "%s Write desc head %i miss, too small for any ring "
				"size: %lu, wiov->used: %u\n", __func__, *head, size, wiov->used);
	} else {
		ring = &kvec_buff->remote_write_kvecs.rings[ring_id];

		if (!ring->send_max_size_local || ring->send_max_size_local > size) {
			if (ring->send_max_size_local > size) {
				dev_warn(&vdev->dev,"%s For kvec_buff ring %i New descriptor "
						"size %lu is less that old size %u\n", __func__, ring_id,
						size, ring->send_max_size_local);
			}
			dev_dbg(&vdev->dev,"%s kvec_buff ring %i size %lu\n", __func__,
					ring_id, size);
			ring->send_max_size_local = size;
			wmb();
			iowrite32(ring->send_max_size_local, ring->send_max_size);
		}

		idx = KVEC_COUNTER_TO_IDX(ring->last_cnt, ring->num);
		ring->last_cnt = KVEC_COUNTER_ADD(ring->last_cnt, 2, ring->num);

		dev_dbg(&vdev->dev,"%s putting descriptor "
				"ring_idx %i, idx %i, ring->last_cnt %i, ring->num %i\n",
				__func__, ring_id, idx, ring->last_cnt, ring->num);

		memcpy_toio(ring->buf + idx, &peer_kvec, sizeof(peer_kvec));
	}

	*head = USHRT_MAX;
	return ring_id;
}

static void vop_kvec_buff_update_idx(struct vop_dev_common *cdev,
		unsigned cnt_size[KVEC_BUF_NUM])
{
	struct vop_kvec_buff *kvec_buff = &cdev->kvec_buff;
	int ring_id;
	u16* last_write = NULL;

	wmb();
	for (ring_id = 0; ring_id < KVEC_BUF_NUM; ++ring_id) {
		if (cnt_size[ring_id]) {
			last_write = kvec_buff->remote_write_kvecs.rings[ring_id].cnt;
			iowrite16(kvec_buff->remote_write_kvecs.rings[ring_id].last_cnt,
					last_write);

			cnt_size[ring_id] = 0;
		}
	}

	if (last_write) {
		wmb();
		ioread16(last_write);
	}

	vop_send_heads_up(cdev, &cdev->heads_up_avail_irq);
}

/**
 * vop_kvec_update_buf - put decriptors from local vring to a ring buffer
 * allocated by the other host. Ring buffer is used instead of sharing
 * vring memory directly to reduce number of transactions on the shared
 * memory - this function issues writes only eliminating reads.
 * */
void vop_kvec_buf_update(struct vop_dev_common *cdev)
{
	struct vop_device *vdev;
	struct vop_kvec_buff *kvec_buff;
	struct vop_vringh* vringh;
	struct vringh_kiov* wiov;
	u16 head = USHRT_MAX;
	int ring_id;
	unsigned cnt_size[KVEC_BUF_NUM], cnt_total = 0;

	if (!cdev || !cdev->ready) {
		return;
	}

	kvec_buff = &cdev->kvec_buff;
	kvec_buff->remote_write_kvecs.is_update = true;

	vdev = cdev->vdev;
	vringh = cdev->vringh_rcv;
	wiov = &kvec_buff->remote_write_kvecs.kiov;
	memset(cnt_size, 0, sizeof (cnt_size));

	if (!kvec_buff->remote_write_kvecs.mapped) {
		dev_err(&vdev->dev, "%s not ready remote rings\n", __func__);
		kvec_buff->remote_write_kvecs.is_update = false;
		return;
	}

	common_dev_notify_avail(cdev);

	while(vop_common_get_descriptors(cdev->vdev, vringh, &head, wiov, false) > 0) {
		dev_dbg(&vdev->dev,
			"%s fetched %d descriptors from local queue\n",
			__func__, wiov->used);

		ring_id = vop_kvec_put(cdev, wiov, &head);
		if (ring_id >= 0) {
			++cnt_size[ring_id];
			++cnt_total;
			++kvec_buff->remote_write_kvecs.rings[ring_id].stats_num;
			head = USHRT_MAX;

			if ( !((cnt_total) % KVEC_INDEX_UPDATE_PERIOD) && cnt_total) {
				vop_kvec_buff_update_idx(cdev, cnt_size);
			}
		}
	}

	vop_kvec_buff_update_idx(cdev, cnt_size);
	kvec_buff->remote_write_kvecs.is_update = false;
}

void vop_kvec_buf_consume(struct vop_dev_common *cdev)
{
	struct vop_used_kiov_ring *used_ring = &cdev->kvec_buff.local_write_kvecs.used_ring;
	struct vop_peer_used_kiov *kiov;
	unsigned long flags;

	while(READ_ONCE(cdev->ready) &&
	      READ_ONCE(*used_ring->cnt) != used_ring->last_cnt) {
		kiov = used_ring->buf + KVEC_COUNTER_TO_IDX(used_ring->last_cnt, used_ring->num);
		/* vr_spinlock is used also by vop_kvec_buf_update() in softirq context*/
		spin_lock_irqsave(&cdev->vringh_rcv->vr_spinlock, flags);
		vca_vringh_complete_kern(&cdev->vringh_rcv->vrh, kiov->head, kiov->len);
		spin_unlock_irqrestore(&cdev->vringh_rcv->vr_spinlock, flags);
		used_ring->last_cnt = KVEC_COUNTER_ADD(used_ring->last_cnt, 1, used_ring->num);
	}
}

int vop_spin_for_used_descriptors(struct vop_dev_common *cdev)
{
	struct vop_device *vdev = cdev->vdev;
	struct vop_vringh *vringh = cdev->vringh_rcv;
	struct vop_heads_up_irq *used_hu_irq = &cdev->heads_up_used_irq;

	if (!vringh->vring.vr.used) {
		dev_err(&vdev->dev, "%s vring not initialized\n", __func__);
		return -EBUSY;
	}

	while (0 == wait_for_used_desc(cdev, used_hu_irq, NULL)) {
			dev_dbg(&vdev->dev, "%s used_idx incremented\n", __func__);
			vop_kvec_buf_consume(cdev);
			rmb();
			vca_vring_interrupt(0, vringh->vq);
	}

	return 0;
}

/* fetch descriptors from kvec_buf ring buffer */
int vop_kvec_get(
	struct vop_dev_common *cdev,
	struct vop_device *vdev,
	struct buffer_dma_item *item)
{
	u16 cur_idx, next_idx, num, first;
	u16 avail_cnt;
	size_t num_kvec;
	u16 head;
	struct vop_kvec_ring *ring = NULL;

	BUG_ON(item->kvec_buff_id < 0);

	ring = &cdev->kvec_buff.local_write_kvecs.rings[item->kvec_buff_id];
	num_kvec =  ring->num;

	mutex_lock(&cdev->kvec_buff.local_write_kvecs.mutex);

	/* skip cancelled requests */
	while (ring->last_cnt != *ring->cnt) {
		cur_idx = KVEC_COUNTER_TO_IDX(ring->last_cnt, num_kvec);

		if (!(ring->buf[cur_idx].flags & VOP_KVEC_FLAG_CANCELLED))
			/* Check first and non-cancelled descriptor found */
			break;
		head = ring->buf[cur_idx].head;
		dev_dbg(&vdev->dev, "%s skipping cancelled request descriptor no %u ring no %u\n",
			__func__, cur_idx, item->kvec_buff_id);
		ring->buf[cur_idx].flags = 0;
		ring->buf[cur_idx].head = 0;
		ring->last_cnt = KVEC_COUNTER_ADD(ring->last_cnt, 1, ring->num);
	}

	if (ring->last_cnt == *ring->cnt) {
		// no data available
		item->head_to = USHRT_MAX;
		mutex_unlock(&cdev->kvec_buff.local_write_kvecs.mutex);
		return -EAGAIN;
	}

	/* fetch descriptors */
	avail_cnt = *ring->cnt;
	rmb();
	first = ring->last_cnt % num_kvec;
	cur_idx = first;
	next_idx = KVEC_COUNTER_TO_IDX(ring->last_cnt + 1, num_kvec);
	head = ring->buf[cur_idx].head;
	num = 1;
	++ring->stats_num;

	dev_dbg(&vdev->dev,"%s peer_kvec[%d]: "
		"desc:(%p, %x) head:%x\n", __func__,
		cur_idx,
		ring->buf[cur_idx].iov.iov_base,
		(u32)ring->buf[cur_idx].iov.iov_len,
		ring->buf[cur_idx].head);

	ring->buf[cur_idx].head = 0;

	/* fetch all adjecent iovs with the same head value */
	while (!KVEC_COUNTER_EQ(ring->last_cnt + num, avail_cnt, ring->num) &&
	       head == ring->buf[next_idx].head &&
	       num < ring->num) {
			num++;
			cur_idx = next_idx;
			next_idx = KVEC_COUNTER_TO_IDX(next_idx + 1, num_kvec);
			dev_dbg(&vdev->dev,
				"%s peer_kvec[%d]: desc"
				":(%p, %x) head:%x\n",
				__func__,
				cur_idx,
				ring->buf[cur_idx].iov.iov_base,
				(u32)ring->buf[cur_idx].iov.iov_len,
				ring->buf[cur_idx].head);
			ring->buf[cur_idx].head = 0;
	}

	item->head_to = head;
	item->num_kvecs_to = num;
	item->kvec_to = ring->buf + first;

	/* mark avail ring buffer item as used */
	item->kvec_to->flags = VOP_KVEC_FLAG_IN_USE;

	dev_dbg(&vdev->dev,
		"%s dma_item head_to:%x num_kvecs_to:%x first_kiov: (%p %x)\n",
		__func__, item->head_to, item->num_kvecs_to,
		item->kvec_to->iov.iov_base,
		(u32)item->kvec_to->iov.iov_len);

	dev_dbg(&vdev->dev,
		"%s avail_cnt is%x (last seen: %x)\n", __func__,
		avail_cnt, ring->last_cnt);

	ring->last_cnt = KVEC_COUNTER_ADD(ring->last_cnt, num, ring->num);

	mutex_unlock(&cdev->kvec_buff.local_write_kvecs.mutex);
	return 0;
}

void vop_kvec_used(struct buffer_dma_item *item)
{
	struct vop_used_kiov_ring *used_ring = &item->ring->cdev->kvec_buff.remote_write_kvecs.used_ring;
	struct vop_peer_used_kiov used_kiov;
	u16 idx;

	used_kiov.head = item->head_to;
	used_kiov.len = item->bytes_written;

	spin_lock(&used_ring->lock);
	idx = KVEC_COUNTER_TO_IDX(used_ring->last_cnt, used_ring->num);
	memcpy_toio(used_ring->buf + idx, &used_kiov, sizeof(used_kiov));
	wmb();
	used_ring->last_cnt = KVEC_COUNTER_ADD(used_ring->last_cnt, 1, used_ring->num);
	iowrite16(used_ring->last_cnt, used_ring->cnt);
	spin_unlock(&used_ring->lock);
	wmb();
	ioread16(used_ring->cnt);
}

void vop_kvec_check_cancel(struct vop_dev_common *cdev)
{
	struct vop_device *vdev = cdev->vdev;
	struct vop_peer_kvec_buf_header *hdr = (struct vop_peer_kvec_buf_header *) cdev->kvec_buff.local_write_kvecs.pages;
	u32 request_idx;
	u16 ring_idx;
	struct vop_peer_kvec *kvec;
	bool wait_for_finish = false;
	VOP_REQUEST_CANCEL_STATUS status = VOP_REQUEST_CANCEL_ERROR;
	struct vop_kvec_ring *ring = NULL;
	u16 head;
	u16 idx_start, idx_end;

	if (!hdr->request_cancellation)
		return;

	rmb();
	ring_idx = hdr->cancelled_request_ring_id;
	request_idx = hdr->cancelled_request_idx;
	if (ring_idx >= KVEC_BUF_NUM) {
		dev_err(&vdev->dev, "%s invalid ring id: %u\n", __func__, ring_idx);
		status =  VOP_REQUEST_CANCEL_INVALID_REQUEST;
		goto cancel_done;
	}

	ring = &cdev->kvec_buff.local_write_kvecs.rings[ring_idx];
	idx_start = KVEC_COUNTER_TO_IDX(ring->last_cnt, ring->num);
	idx_end = KVEC_COUNTER_TO_IDX(*ring->cnt, ring->num);

	if (request_idx >= ring->num) {
		dev_err(&vdev->dev, "%s invalid request index: %u\n", __func__, request_idx);
		status =  VOP_REQUEST_CANCEL_INVALID_REQUEST;
		goto cancel_done;
	}

	if (ring->last_cnt == *ring->cnt) {
		/* Ring is empty */
		status =  VOP_REQUEST_CANCEL_INVALID_REQUEST;
		goto cancel_done;
	}

	kvec = &ring->buf[request_idx];

	mutex_lock(&cdev->kvec_buff.local_write_kvecs.mutex);
	if (kvec->flags & VOP_KVEC_FLAG_IN_USE) {
		dev_info(&vdev->dev, "%s descriptor no %u ring %u processing already started\n",
			__func__, request_idx, ring_idx);
		wait_for_finish = true;
	} else {
		dev_dbg(&vdev->dev, "%s marking descriptor no %u ring %u as cancelled\n",
			__func__, request_idx, ring_idx);
		kvec->flags |= VOP_KVEC_FLAG_CANCELLED;

		/* Current descriptor is cancelled so we're going to skip it.
		   Need to iterate over the following ring items to find descriptors with the same head value -
			these should also be skipped as they come from the same  scatter-gather list */
		head = kvec->head;
		request_idx  = KVEC_COUNTER_TO_IDX(request_idx +1, ring->num);

		while (ring->buf[request_idx].head == head &&
			(idx_start == idx_end || /*When ring is full!*/
			(idx_start < idx_end && idx_start <= request_idx && request_idx <= idx_end) ||
			(idx_start > idx_end && (idx_start >= request_idx || request_idx >= idx_end)))) {
			dev_dbg(&vdev->dev, "%s skipping cancelled request descriptor no %u ring no %u\n",
				__func__, request_idx, ring_idx);
			ring->buf[request_idx].flags |= VOP_KVEC_FLAG_CANCELLED;
			request_idx  = KVEC_COUNTER_TO_IDX(request_idx +1, ring->num);
		}

		/* we can assert noone is going to write to this descriptor - it is going
		   to be ignored by the routine fetching descriptors from ring */
		status = VOP_REQUEST_CANCEL_OK;
	}
	mutex_unlock(&cdev->kvec_buff.local_write_kvecs.mutex);

	if (wait_for_finish) {
		int err = wait_event_interruptible_timeout(
				cdev->remap_free_queue,
				!(READ_ONCE(kvec->flags) & VOP_KVEC_FLAG_IN_USE),
				msecs_to_jiffies(3000));	// 3 seconds
		if (err > 0)
			status = VOP_REQUEST_CANCEL_OK;
		else
			status = (err  ? VOP_REQUEST_CANCEL_ERROR : VOP_REQUEST_CANCEL_TIMEOUT);
	}
cancel_done:
	hdr->cancellation_status = status;
	hdr->request_cancellation = 0;
}


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
 * Adapted from:
 *
 * sbull examples, the code comes from the book "Linux Device
 * Drivers" by Alessandro Rubini and Jonathan Corbet, published
 * by O'Reilly & Associates.
 *
 * Copyright (C) 2001 Alessandro Rubini and Jonathan Corbet
 * Copyright (C) 2001 O'Reilly & Associates
 *
 *
 * Intel VCA Block IO driver.
 *
 */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/hdreg.h>
#include <linux/genhd.h>
#include <linux/blkdev.h>
#include <linux/kthread.h>
#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,1,16) // the blk_status_t actually appeared in 4.13.x
	#include <linux/blk-mq.h>
#endif

#include "vcablk_common/vcablk_common.h"
#include "vcablk_hal.h"
#include "vcablk_pool.h"
#include "vcablk_disk.h"

#ifdef TEST_BUILD
#include "vcablk_test_hw_ops.h"
#else
#include "plx_hw_ops_blockio.h"
#endif

#ifdef RHEL_RELEASE_CODE
#else /* RHEL_RELEASE_CODE */
#	if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
#		define BIO_FORMAT_SECTOR
#	endif
#	if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)
#		define BIO_ERROR_SEPARATE
#	endif
#	if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0)
#		define BIO_OP_SYNC
#	endif

#endif /* RHEL_RELEASE_CODE */

static int vcablk_major = 0;	/* Registered device number */
#define VCA_BLK_DISK_NAME "vcablk"

#define TIMEOUT_SEC 120
/*
 * Minor number and partition management.
 */
#define VCA_BLK_MINORS	16	/* Get registered */

struct bvec_context {
	dma_addr_t dma_addr;
	size_t size;
	enum dma_data_direction dir;
};

struct vcablk_bio_context {
	__u16 id;
	struct request *req;
	unsigned bvec_ctxs_idx;
	struct bvec_context bvec_ctxs[VCABLK_BIO_SPLIT_SIZE];
};

#define REQ_RING_SIZE (2048)
#define REQ_RING_MASK ((REQ_RING_SIZE)-1)

#if CHECK_NOT_POWER_OF_2(REQ_RING_SIZE)
#error "REQ_RING_SIZE SIZE HAVE TO BE POWER OF 2"
#endif

struct request_ring {
	int head;
	int tail;
	/*  Max request limit BLKDEV_MAX_RQ for read and write and once left empty for ring.
	 * To be safe add biggest array, because can be some sync requests. */
	struct request* ring[REQ_RING_SIZE];
};

/*
 * The internal representation of our device.
 */
struct vcablk_disk {
	size_t size_bytes;                  /* Device size in bytes */
	unsigned sectors_num;               /* Number of linear sectors */
	int hardsect_size;                  /* Size of disk sector */
	struct hd_geometry geo;             /* Geometry of disk */
	spinlock_t lock;                    /* For mutual exclusion */
	short ref_cnt;                      /* How many users */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,1,16) // the blk_status_t actually appeared in 4.13.x
	struct request_queue *queue;        /* The device request queue */
	struct gendisk *gdisk;              /* The gendisk structure */
	struct blk_mq_tag_set tag_set;
#else
	struct request_queue *queue;    /* The device request queue */
	struct gendisk *gdisk;             /* The gendisk structure */
#endif
	bool gdisk_started;
	bool read_only;
	int dev_id;

	struct task_struct	*request_thread;   /* Request task */
	wait_queue_head_t	request_event;     /* wait queue for incoming requests */
	wait_queue_head_t	request_ring_wait; /* wait queue for release request_ring */
	int bio_done_db;
	void *bio_done_irq;

	int request_db;
	struct vcablk_ring *request_ring;
	__u16 request_ring_alloc;
	struct vcablk_ring *completion_ring;

	struct vcablk_dev* fdev;

	struct work_struct request_completion_work;

	vcablk_pool_t *bio_context_pool;
	spinlock_t bio_context_pool_lock;
	wait_queue_head_t bio_context_pool_wait;

	__u16 split_request_error;          /* Remember error of part split request */

	struct request_ring req_ring;
	spinlock_t req_ring_lock;
};

/**
 *	vcablk_format_disk_name - format disk name
 */
static int
vcablk_disk_format_name(char *prefix, int index, char *buf, int buflen)
{
	/* device index should have at most 2 decimal digits */
	if (index < 0 || index > 999)
		return -EINVAL;

	/* make sure two decmal digits will fit into buf */
	if (buflen <= strlen(prefix) + 3)
		return -EINVAL;

	snprintf(buf, buflen, "%s%d", prefix, index);

	return 0;
}

static void
vcablk_disk_request_clean(struct vcablk_disk *dev,
		struct vcablk_bio_context *bio_context)
{
	struct vcablk_dev* fdev = dev->fdev;
	unsigned int i;

	for (i = 0; i <bio_context->bvec_ctxs_idx; ++i) {
		struct bvec_context *map = bio_context->bvec_ctxs + i;
				vcablk_dma_unmap_page(fdev->parent,
						map->dma_addr,
						map->size,
						map->dir);
	}
	/* Clear .id, to notify that request ended */
	bio_context->id = 0;
	bio_context->bvec_ctxs_idx = 0;
	spin_lock(&dev->bio_context_pool_lock);
	vcablk_pool_push(dev->bio_context_pool, bio_context);
	spin_unlock(&dev->bio_context_pool_lock);
	wake_up_all(&dev->bio_context_pool_wait);
}

static void
vcablk_disk_request_end(struct vcablk_disk *dev, struct request *req, int ret)
{
	if (ret)
		pr_err("%s: ret %i\n", __func__, ret);
	spin_lock_irq(&dev->lock);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,1,16) // the blk_status_t actually appeared in 4.13.x
	blk_mq_end_request(req, ret);
#else
	__blk_end_request_all(req, ret);
#endif
	spin_unlock_irq(&dev->lock);
}

static void
vcablk_disk_request_done(struct vcablk_disk *dev, __u16 id, int ret)
{
	if (id) {
		/* Cookie id increment because 0 is invalid */
		struct vcablk_bio_context *bio_context = (struct vcablk_bio_context *)
				vcablk_pool_get_by_id(dev->bio_context_pool, id - 1);
		if (bio_context) {
			struct request *req = bio_context->req;
			bio_context->req = NULL;
			vcablk_disk_request_clean(dev, bio_context);
			bio_context = NULL;
			if (req) {
				/* Last part of split for BIO request. */
				if (dev->split_request_error) {
					ret = dev->split_request_error;
					dev->split_request_error = 0;
				}
				vcablk_disk_request_end(dev, req, ret);
			} else {
				if (ret) {
					/* Remember error for no last split request */
					dev->split_request_error = ret;
				}
			}
		} else {
			pr_err("%s: Receive not expected bio_context %s %i\n", __func__,
					dev->gdisk->disk_name, id);
		}
	} else {
		pr_err("%s: Receive invalid bio_context %s\n", __func__,
				dev->gdisk->disk_name);
	}
}

static struct vcablk_request *
vcablk_disk_request_alloc_next(struct vcablk_disk *dev,
		int (*stop_f)(struct vcablk_disk *))
{
	struct vcablk_ring *ring = dev->request_ring;
	struct vcablk_request *req = NULL;

	while (VCA_RB_BUFF_FULL(dev->request_ring_alloc,
			ring->last_used, ring->num_elems)
			&& (!stop_f || !stop_f(dev))) {
		/* Pooling 1 sec on space in ring request.*/
		int ret = wait_event_interruptible_timeout(dev->request_ring_wait,
					!VCA_RB_BUFF_FULL(dev->request_ring_alloc,
					READ_ONCE(ring->last_used), ring->num_elems)
					|| (stop_f && stop_f(dev)),
					msecs_to_jiffies(TIMEOUT_SEC * 1000));
		if (ret >0) {
			break;
		}
		pr_warn("%s: Wait for ring request TIMEOUT!!!  %i second last_alloc %i, "
					"last_add %i num_elems %i. Pooling again.\n", __func__,
					TIMEOUT_SEC, dev->request_ring_alloc, ring->last_add,
					(int)ring->num_elems);
	}

	rmb();
	if (!VCA_RB_BUFF_FULL(dev->request_ring_alloc, ring->last_used, ring->num_elems)) {
		req = VCABLK_RB_GET_REQUEST(dev->request_ring_alloc, ring->num_elems, ring->elems);
		dev->request_ring_alloc = VCA_RB_COUNTER_ADD(dev->request_ring_alloc, 1, ring->num_elems);
	} else {
		pr_err("%s: request_ring_alloc full!!! last_alloc %i, "
				"last_add %i last_used %i num_elems %i\n", __func__, dev->request_ring_alloc,
				ring->last_add, ring->last_used, (int)ring->num_elems);
	}
	return req;
}

static int
vcablk_disk_request_start(struct vcablk_disk *dev)
{
	struct vcablk_ring *ring = dev->request_ring;
	struct vcablk_dev* fdev = dev->fdev;

	if (ring->last_add == dev->request_ring_alloc) {
		pr_err("%s: ERROR No requests to start, last_alloc %i,"
			"last_add %i\n", __func__,dev->request_ring_alloc ,ring->last_add);
		return -EINVAL;
	}

	wmb();
	ring->last_add = dev->request_ring_alloc;
	wmb();

	/* Send IRQ request */
	fdev->hw_ops->send_intr(fdev->parent, dev->request_db);
	return 0;
}

static void
vcablk_disk_request_break(struct vcablk_disk *dev,
		struct vcablk_bio_context *bio_context)
{
	struct vcablk_ring *ring = dev->request_ring;
	pr_debug("%s: last_alloc %i, last_add %i, id %i\n",
		__func__, dev->request_ring_alloc, ring->last_add,
		bio_context?bio_context->id:0);
	dev->request_ring_alloc = ring->last_add;

	if (bio_context)
		vcablk_disk_request_clean(dev, bio_context);

	wake_up(&dev->request_ring_wait);
}

static dma_addr_t
vcablk_disk_map_page(struct vcablk_disk *dev, struct vcablk_bio_context *bio_context,
		struct bio_vec *bvec, enum dma_data_direction dir)
{
	dma_addr_t da;
	int err;
	struct bvec_context *bvctx = NULL;

	if (bio_context->bvec_ctxs_idx >= VCABLK_BIO_SPLIT_SIZE) {
		pr_err("%s: bvec context alloc error\n", __func__);
		return 0;
	}

	bvctx = &bio_context->bvec_ctxs[bio_context->bvec_ctxs_idx];

	err = vcablk_dma_map_page(dev->fdev->parent, bvec->bv_page,
			bvec->bv_offset, bvec->bv_len, dir, &da);
	if (err) {
		pr_err("%s: map page alloc error %i\n",
				__func__, err);
		bvctx = NULL;
		return 0;
	}

	/* Add map page to de mapping list */
	bio_context->bvec_ctxs_idx++;
	bvctx->dma_addr = da;
	bvctx->size = bvec->bv_len;
	bvctx->dir = dir;

	return da;
}

static int
vcablk_disk_request_step(struct vcablk_disk *dev, struct bio_vec *bvec,
		struct vcablk_bio_context *bio_context, __u8 request,
		unsigned long sector, unsigned long sectors_num,
		int (*stop_f)(struct vcablk_disk *))
{
	struct vcablk_request *req = vcablk_disk_request_alloc_next(dev, stop_f);
	if (!req) {
		pr_err("%s: No available free request ring %s\n",
				__func__, dev->gdisk->disk_name);

		return -EBUSY;
	}
	req->request_type = request;
	req->cookie = bio_context->id;

	pr_debug("%s: Create request: cookie %u, request %u, "
			"sectors_num %lu, sector %lu\n",
			__func__, bio_context->id, request, sectors_num, sector);

	if (bvec && (request == REQUEST_READ || request == REQUEST_WRITE)) {
		enum dma_data_direction dir =
				(request == REQUEST_WRITE) ? DMA_TO_DEVICE : DMA_FROM_DEVICE;
		dma_addr_t da = vcablk_disk_map_page(dev, bio_context, bvec, dir);
		if (!da) {
			pr_err("%s: Invalid MAP memory!!\n", __func__);
			return -ENOMEM;
		}

		req->sectors_num = sectors_num;
		req->sector = sector;
		req->phys_buff = da;
	}
	return 0;
}

static struct vcablk_bio_context *
vcablk_disk_get_bio_context(struct vcablk_disk *dev,
		int (*stop_f)(struct vcablk_disk *))
{
	struct vcablk_bio_context *bio_context = NULL;
	__u16 uninitialized_var(id);

	while (!bio_context) {
		/* Pooling TIMEOUT_SEC on free bio context */
		int ret = wait_event_interruptible_timeout(dev->bio_context_pool_wait,
				vcablk_is_available(dev->bio_context_pool) || (stop_f && stop_f(dev)),
				msecs_to_jiffies(TIMEOUT_SEC * 1000));
		rmb();
		if (ret <= 0) {
			WARN_ONCE(!ret, "%s: Still waiting for ring request after more than %i seconds.\n", __func__, TIMEOUT_SEC);
			continue;
		}
		if (stop_f && stop_f(dev))
			return NULL;
		spin_lock(&dev->bio_context_pool_lock);
		bio_context = vcablk_pool_pop(dev->bio_context_pool, &id);
		spin_unlock(&dev->bio_context_pool_lock);
	}

	if (bio_context) {
		/*Cookie id 0 is invalid*/
		bio_context->id = id + 1;
		bio_context->req = NULL;
		bio_context->bvec_ctxs_idx = 0;
	} else {
		pr_err("%s: Get  bio_context Error!!!\n", __func__);
	}
	return bio_context;
}

/*
 * Transfer a single BIO.
 */
static int
vcablk_disk_async_request(struct vcablk_disk *dev, struct request *req,
		int (*stop_f)(struct vcablk_disk *))
{
	struct req_iterator iter;
#ifdef BIO_FORMAT_SECTOR
	struct bio_vec bvec;
#define GBIOVP(bvec) (&bvec)
#define GBIOSEC(bio) (bio->bi_iter.bi_sector)
#else /* BIO_FORMAT_SECTOR */
	struct bio_vec *bvec;
	#define GBIOVP(bvec) (bvec)
	#define GBIOSEC(bio) (bio->bi_sector)
#endif /* BIO_FORMAT_SECTOR */
	unsigned long do_sync = false;
	int ret = 0;
	__u8 request;
	__u16 uninitialized_var(prev_bio_cookie);
	struct vcablk_bio_context *bio_context_previous = NULL;
	struct vcablk_bio_context *bio_context;

	if (rq_data_dir(req)) {
		request = REQUEST_WRITE;
	} else {
		request = REQUEST_READ;
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 13, 0)
	if (req_op(req) & (REQ_OP_FLUSH | REQ_SYNC | REQ_PREFLUSH | REQ_FUA)) {
		do_sync = true;
	}
#else
	if ( req->cmd_flags & (REQ_FLUSH | REQ_FUA | REQ_SYNC)) {
		do_sync = true;
	}
#endif

	bio_context = vcablk_disk_get_bio_context(dev, stop_f);
	if (!bio_context) {
		ret = -EIO;
		goto end;
	}

	if (do_sync) {
		ret = vcablk_disk_request_step(dev, NULL, bio_context, REQUEST_SYNC, 0, 0, stop_f);
		if (ret) {
			ret = -EIO;
			goto end;
		}
	}

	/* Do each segment independently. */
	rq_for_each_segment(bvec, req, iter) {
		unsigned long sectors_num = GBIOVP(bvec)->bv_len >> SECTOR_SHIFT;
		if (bio_context->bvec_ctxs_idx >= VCABLK_BIO_SPLIT_SIZE) {
			/* Split BIO request on parts. Before get new part, start last prepared part.
			 * Once request can be bigger that possible to execute without split. */
			ret = vcablk_disk_request_start(dev);
			if (ret) {
				ret = -EIO;
				goto end;
			}
			bio_context_previous = bio_context;
			prev_bio_cookie = READ_ONCE(bio_context->id);
			bio_context = vcablk_disk_get_bio_context(dev, stop_f);
			if (!bio_context) {
				ret = -EIO;
				goto end;
			}
		}

		ret = vcablk_disk_request_step(dev, GBIOVP(bvec), bio_context,
				request, GBIOSEC(req->bio), sectors_num, stop_f);

		if (ret)
			goto end;
		GBIOSEC(req->bio) += sectors_num;
	}

	if (do_sync) {
		ret = vcablk_disk_request_step(dev, NULL, bio_context, REQUEST_SYNC, 0, 0, stop_f);
		if (ret) {
			ret = -EIO;
			goto end;
		}
	}

	/* Set req pointer only for last bio_context who will call vcablk_disk_request_end()*/
	bio_context->req = req;
	ret = vcablk_disk_request_start(dev);

end:
	if (ret) {
		vcablk_disk_request_break(dev, bio_context);
		/* Before end bio, ensure that last scheduled split part of request finished */
		if (bio_context_previous && prev_bio_cookie)
			while (wait_event_interruptible(
					dev->bio_context_pool_wait,
					READ_ONCE(bio_context_previous->id) != prev_bio_cookie))
				/* empty */;
		vcablk_disk_request_end(dev, req, -EIO);
	}
	return ret;

#undef GBIOSEC
#undef GBIOVP
}


static int
vcablk_disk_thread_stop(struct vcablk_disk *dev)
{
	int ret = kthread_should_stop();
	if (ret) {
		pr_warn("%s: %s Thread should stop\n", dev->gdisk->disk_name, __func__);
	}
	return ret;
}

static int
vcablk_disk_make_request_thread(void *data)
{
	struct vcablk_disk *dev = data;
	struct request *req;

	if (dev->gdisk) {
		pr_debug("%s: %s Thread start\n", dev->gdisk->disk_name, __func__);
	}

	while (!vcablk_disk_thread_stop(dev)) {
		wait_event_interruptible(dev->request_event,
				READ_ONCE(dev->req_ring.head) != READ_ONCE(dev->req_ring.tail) ||
				vcablk_disk_thread_stop(dev));

		if (READ_ONCE(dev->req_ring.head) == READ_ONCE(dev->req_ring.tail))
			continue;
		spin_lock_irq(&dev->req_ring_lock);
		rmb();
		req = dev->req_ring.ring[dev->req_ring.tail];
		dev->req_ring.tail = (dev->req_ring.tail +1)&REQ_RING_MASK;
		wmb();
		spin_unlock_irq(&dev->req_ring_lock);

		BUG_ON(!req);
		vcablk_disk_async_request(dev, req, vcablk_disk_thread_stop);
	}
	pr_debug("%s: Thread stop\n", __func__);
	do_exit(0);
	return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,1,16) // the blk_status_t actually appeared in 4.13.x
	static blk_status_t vcablk_disk_request_fn(struct blk_mq_hw_ctx *hctx, const struct blk_mq_queue_data* bd)
	{
		struct request *rq = bd->rq;
		struct vcablk_disk *dev = rq->q->queuedata;

		spin_lock_irq(&dev->req_ring_lock);
		rmb();
		{
			int new_head;
			blk_status_t status = BLK_STS_OK;
			blk_mq_start_request(rq);

			new_head = (dev->req_ring.head +1 )&REQ_RING_MASK;
			if (new_head == dev->req_ring.tail) {
				pr_err("%s: Ring request is too small!!! Ignore request.\n", __func__);
				status = BLK_STS_IOERR;
			} else {
				dev->req_ring.ring[dev->req_ring.head] = rq;
				dev->req_ring.head = new_head;
			}
			blk_mq_end_request(rq, status);
		}
		wmb();
		spin_unlock_irq(&dev->req_ring_lock);
		wake_up(&dev->request_event);
		return BLK_STS_OK;
	}
#else
	static void vcablk_disk_request_fn(struct request_queue *q)
	{
		   struct vcablk_disk *dev = q->queuedata;
		   struct request *req;
		   int new_head;
			spin_lock_irq(&dev->req_ring_lock);
		  rmb();
		   while ((req = blk_peek_request(q)) != NULL) {
				   blk_start_request(req);
				   new_head = (dev->req_ring.head +1 )&REQ_RING_MASK;
				   if (new_head == dev->req_ring.tail) {
						   /* Ring is too small. Ignore request. */
						   pr_err("%s: Ring request is too small!!! Ignore request.\n", __func__);
						   __blk_end_request_all(req, -EIO);
						   break;
				   }
				   dev->req_ring.ring[dev->req_ring.head] = req;
				   dev->req_ring.head = new_head;

		   }
		   wmb();
		   spin_unlock_irq(&dev->req_ring_lock);
		   wake_up(&dev->request_event);
	}
#endif
static void
vcablk_disk_request_completion_handler(struct work_struct *work)
{
	struct vcablk_disk *dev =
			container_of(work, struct vcablk_disk, request_completion_work);
	struct vcablk_ring *ring = dev->completion_ring;
	__u16 last_used = ring->last_used;
	for (; last_used != ring->last_add;) {
		struct vcablk_completion *ack =
				VCABLK_RB_GET_COMPLETION(last_used, ring->num_elems, ring->elems);
		__u16 request_id = ack->cookie;
		__u8 ret = ack->ret;
		ack->cookie = 0;
		ack->ret = 0;
		last_used = VCA_RB_COUNTER_ADD(last_used, 1, ring->num_elems);
		/*
		 * First clean ack ring, then use response values,
		 * to not block ring ack for backend.
		 */
		ring->last_used = last_used;
		vcablk_disk_request_done(dev, request_id, ret);
		wake_up(&dev->request_ring_wait);
	}
}

static irqreturn_t
vcablk_disk_ack_irq(int irq, void *data)
{
	struct vcablk_disk *dev = (struct vcablk_disk *)data;
	struct vcablk_dev* fdev = dev->fdev;
	schedule_work(&dev->request_completion_work);

	fdev->hw_ops->ack_interrupt(fdev->parent, dev->bio_done_db);
	return IRQ_HANDLED;
}

/*
 * Open and close.
 */

static int
vcablk_disk_open(struct block_device *bdev, fmode_t mode)
{
	struct vcablk_disk *dev = bdev->bd_disk->private_data;
	pr_debug("%s:\n", __func__);
	spin_lock_irq(&dev->lock);
	dev->ref_cnt++;
	spin_unlock_irq(&dev->lock);
	return 0;
}

static void
vcablk_disk_release(struct gendisk *disk, fmode_t mode)
{
	struct vcablk_disk *dev = disk->private_data;
	pr_debug("%s:\n", __func__);
	spin_lock_irq(&dev->lock);
	dev->ref_cnt--;
	spin_unlock_irq(&dev->lock);
}

static int
vcablk_disk_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
	struct vcablk_disk *dev = bdev->bd_disk->private_data;
	if (dev->gdisk) {
		pr_debug("%s: %s size %lu\n",
				__func__, dev->gdisk->disk_name, dev->size_bytes);
	}
	*geo = dev->geo;
	return 0;
}

int
vcablk_disk_stop(struct vcablk_disk *dev, bool force)
{
	int err = 0;

	/* Check that device created a nd not stopped*/
	if (!dev)
		return 0;

	if (!dev->gdisk)
		return 0;

	pr_err("%s: dstroy device id %i\n", __func__, dev->dev_id);
	if (!force) {
		spin_lock_irq(&dev->lock);
		if (dev->ref_cnt > 0) {
			err = -EBUSY;
		}
		spin_unlock_irq(&dev->lock);
		if (err) {
			pr_err("%s: device BUSY %i\n", __func__, err);
			return err;
		}
	}

	if (dev->gdisk) {
		pr_err("%s: deldisk %i\n", __func__, dev->dev_id);
		if (dev->gdisk_started) {
			del_gendisk(dev->gdisk);
			dev->gdisk_started = false;
		}

		if (dev->bio_context_pool) {
			void *ptr = NULL;
			int itr;
			/* Clean all memory mapping */
			vcablk_pool_foreach_used(ptr, dev->bio_context_pool, itr) {
				struct vcablk_bio_context *bio_context = (struct vcablk_bio_context *)ptr;
				pr_err("%s: NOT CLEAN REQUEST dev %i clean bio_context %p "
						"id %u\n", __func__, dev->dev_id, bio_context,
						bio_context->id);
				vcablk_disk_request_clean(dev, bio_context);
				wake_up(&dev->request_ring_wait);
				BUG_ON(1);
			}
		}

		put_disk(dev->gdisk);
		dev->gdisk = NULL;
	}
	flush_work(&dev->request_completion_work);

	return err;
}

struct vcablk_ring *
vcablk_disk_get_rings_req(struct vcablk_disk *dev)
{
	if (!dev)
		return NULL;
	return dev->request_ring;
}

struct vcablk_ring *
vcablk_disk_get_rings_ack(struct vcablk_disk *dev)
{
	if (!dev)
		return NULL;
	return dev->completion_ring;
}

int
vcablk_disk_destroy(struct vcablk_disk *dev)
{
	struct vcablk_dev* fdev;
	int err = 0;
	int dev_id;

	/* Check that device created */
	if (!dev)
		return -ENODEV;

	fdev = dev->fdev;
	dev_id = dev->dev_id;

	err = vcablk_disk_stop(dev, true);

	if (err)
		return err;

	pr_debug("%s: destroy device id %i\n", __func__, dev->dev_id);

	if (dev->request_thread) {
		kthread_stop(dev->request_thread);
		dev->request_thread = NULL;
	}

	vcablk_pool_deinit(dev->bio_context_pool);
	dev->bio_context_pool = NULL;

	if (dev->bio_done_db >= 0) {
		fdev->hw_ops->free_irq(fdev->parent,dev->bio_done_irq, dev);
		dev->bio_done_db = -1;
		dev->bio_done_irq = NULL;
	}

	if (dev->queue) {
		dev->queue = NULL;
	}
	wmb();

	dev->request_ring = NULL;
	dev->completion_ring = NULL;
	kfree(dev);

	return err;
}

/*
 * The ioctl() implementation
 */

int
vcablk_disk_ioctl (struct block_device *bdev, fmode_t mode,
		unsigned int cmd, unsigned long arg)
{
	int err = 0;
	void __user *argp = (void __user *)arg;

	switch(cmd) {
	case HDIO_GETGEO: {
		struct hd_geometry geo;
		vcablk_disk_getgeo(bdev, &geo);
		if (copy_to_user(argp, &geo, sizeof(geo))) {
			err = -EFAULT;
		}
		break;
	}
	default: {
		err =  -ENOTTY;
	}
	}

	return err;
}

/*
 * The device operations structure.
 */
static struct block_device_operations vcablk_ops = {
	.owner		= THIS_MODULE,
	.open		= vcablk_disk_open,
	.release	= vcablk_disk_release,
	.ioctl		= vcablk_disk_ioctl,
	.getgeo		= vcablk_disk_getgeo
};

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,1,16) // the blk_status_t actually appeared in 4.13.x
	static struct blk_mq_ops _mq_ops = {
		.queue_rq = vcablk_disk_request_fn,
	};
#endif
/*
 * Set up our internal device.
 * size - Size of device in bytes
 */
struct vcablk_disk *
vcablk_disk_create(struct vcablk_dev* fdev, int uniq_id, size_t size, bool read_only,
		struct vcablk_ring *ring_req, struct vcablk_ring *completion_ring,
		int bio_done_db, int request_db)
{
	int err = 0;
	struct gendisk *disk = NULL;
	struct vcablk_disk *dev = NULL;
	unsigned sectors_num;
	int hardsect_size;

	if (!ring_req) {
		pr_err("%s: Disk with ID %i ring_req is NULL\n", __func__, uniq_id);
		err = -EFAULT;
		goto exit;
	}

	if (!completion_ring) {
		pr_err("%s: Disk with ID %i completion_ring is NULL\n", __func__, uniq_id);
		err = -EFAULT;
		goto exit;
	}

	if (request_db < 0) {
		pr_err("%s: Disk with ID %i irq_request is NULL\n", __func__, uniq_id);
		err = -EFAULT;
		goto exit;
	}

	hardsect_size = SECTOR_SIZE;
	sectors_num = size / hardsect_size;
	if (size % hardsect_size) {
		pr_warn("%s: size %lu is not multiple of sector size %i\n",
				__func__, size, hardsect_size);
		err = -ENOTBLK;
		goto exit;
	}

	dev = kmalloc(sizeof (struct vcablk_disk), GFP_KERNEL);
	if (dev == NULL) {
		pr_err("%s: Can not allocate device size %lu\n", __func__,
				sizeof (struct vcablk_disk));
		err = -ENOMEM;
		goto exit;
	}
	memset (dev, 0, sizeof (struct vcablk_disk));
	spin_lock_init(&dev->lock);
	dev->fdev = fdev;

	dev->bio_done_db = bio_done_db;

	if (dev->bio_done_db < 0) {
		pr_err("%s: Can not get Doorbell\n", __func__);
		err = -ENODEV;
		goto err;
	}

	init_waitqueue_head(&dev->request_ring_wait);

	init_waitqueue_head(&dev->bio_context_pool_wait);
	dev->bio_context_pool = vcablk_pool_init(VCABLK_BIO_CTX_POOL_SIZE,
			sizeof (struct vcablk_bio_context));
	if (!dev->bio_context_pool) {
		err =-ENOMEM;
		goto err;
	}
	spin_lock_init(&dev->bio_context_pool_lock);

	dev->req_ring.head = 0;
	dev->req_ring.tail = 0;
	spin_lock_init(&dev->req_ring_lock);


	dev->split_request_error = 0;

	dev->bio_done_irq =
			fdev->hw_ops->request_irq(fdev->parent, vcablk_disk_ack_irq,
					"IRQ_DONE_ACK", dev, dev->bio_done_db);

	if (IS_ERR(dev->bio_done_irq)) {
		dev->bio_done_irq = NULL;
		err =-EIO;
		goto err;
	}

	/* Set size of disk */
	dev->hardsect_size = hardsect_size; /* Base size of block */
	dev->sectors_num = sectors_num;
	dev->size_bytes = (size_t)dev->sectors_num * dev->hardsect_size;

	/* Set geometry of disk,some standard values, multiple of 512 */
	dev->geo.start = 0;
	dev->geo.heads = 1<<4;
	dev->geo.sectors = 1<<5;
	dev->geo.cylinders = dev->size_bytes>>9;

	pr_err("%s: Create disk dev_id %i size  %lu sectors_num %u "
			"hardsect_size %i heads %u sectors %u cylinders %u\n", __func__,
			uniq_id, dev->size_bytes, dev->sectors_num, dev->hardsect_size,
			dev->geo.heads, dev->geo.sectors, dev->geo.cylinders);

	dev->read_only = read_only;
	dev->dev_id = uniq_id;

	init_waitqueue_head(&dev->request_event);

	disk = dev->gdisk = alloc_disk(VCA_BLK_MINORS);
	if (!disk) {
		pr_err("%s: alloc_disk failure\n", __func__);
		err = -ENOMEM;
		goto err;
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,1,16) // the blk_status_t actually appeared in 4.13.x
	disk->queue = dev->queue = blk_mq_init_sq_queue(&dev->tag_set, &_mq_ops,
											BLKDEV_MAX_RQ,
											BLK_MQ_F_SHOULD_MERGE); // BLKDEV_MAX_RQ is not a true limitation; quepe_depth may be set bigger here
#else
	disk->queue = dev->queue = blk_init_queue(vcablk_disk_request_fn, &dev->lock);
#endif
	if (dev->queue == NULL) {
		pr_err("%s: Can not allocate queue!\n", __func__);
		err = -ENOMEM;
		goto err;
	}

	blk_queue_logical_block_size(dev->queue, dev->hardsect_size);
	blk_queue_dma_alignment(dev->queue, 511);
	dev->queue->queuedata = dev;

	disk->major = vcablk_major;
	disk->first_minor = uniq_id * VCA_BLK_MINORS;
	disk->fops = &vcablk_ops;
	disk->private_data = dev;
	set_capacity(disk, dev->sectors_num*(dev->hardsect_size>>SECTOR_SHIFT));
	set_disk_ro(disk, read_only);

	err = vcablk_disk_format_name(VCA_BLK_DISK_NAME, uniq_id, disk->disk_name,
			DISK_NAME_LEN);
	if (err) {
		pr_err("%s: disk (vcablk) name length exceeded\n", __func__);
		goto err;
	}

	INIT_WORK(&dev->request_completion_work, vcablk_disk_request_completion_handler);

	dev->request_ring = ring_req;
	dev->request_ring_alloc = ring_req->last_add;
	dev->completion_ring = completion_ring;
	dev->request_db = request_db;

	dev->gdisk_started = false;

	//MOVED TO START
	dev->request_thread = kthread_create(vcablk_disk_make_request_thread, dev,
			disk->disk_name);
	if (IS_ERR(dev->request_thread)) {
		pr_err("%s: Can not create thread %i\n", __func__,
				(int)PTR_ERR(dev->request_thread));
		dev->request_thread = NULL;
		err = -EAGAIN;
		goto err;
	}

	wake_up_process(dev->request_thread);
	return dev;
err:
	vcablk_disk_destroy(dev);
exit:
	return ERR_PTR(err);
}

/*
 * Set up our internal device.
 * size - Size of device in bytes
 */
int
vcablk_disk_start(struct vcablk_disk *dev)
{
	int err = 0;
	struct gendisk *disk;

	if (!dev) {
		pr_err("%s: Bdev is NULL\n", __func__);
		err = -EINVAL;
		goto exit;
	}

	if (!dev->request_thread) {
		pr_err("%s: Bdev id %i thread not created\n", __func__, dev->dev_id);
			err = -EINVAL;
			goto err;
	}

	if (dev->gdisk_started) {
		pr_err("%s: Bdev id %i working\n", __func__, dev->dev_id);
		err = -EINVAL;
		goto err;
	}

	disk = dev->gdisk;

	pr_err("%s: Create disk ADD dev_id %i size  %lu sectors_num %u "
		"hardsect_size %i heads %u sectors %u cylinders %u\n", __func__,
		dev->dev_id, dev->size_bytes, dev->sectors_num, dev->hardsect_size,
		dev->geo.heads, dev->geo.sectors, dev->geo.cylinders);

	add_disk(disk); /* THIS FUNCTION BLOCK context for few REQUESTS*/
	dev->gdisk_started = true;

	pr_err("%s: Create disk END dev_id %i size  %lu sectors_num %u "
		"hardsect_size %i heads %u sectors %u cylinders %u\n", __func__,
		dev->dev_id, dev->size_bytes, dev->sectors_num, dev->hardsect_size,
		dev->geo.heads, dev->geo.sectors, dev->geo.cylinders);

	return err;

err:
	vcablk_disk_stop(dev, false);
exit:
	return err;
}

int
vcablk_disk_init(struct vcablk_dev* fdev)
{
	/* Get registered */
	if (vcablk_major <= 0) {
		vcablk_major = register_blkdev(vcablk_major, VCA_BLK_DISK_NAME);
		if (vcablk_major <= 0) {
			pr_err("%s: unable to get major number\n", __func__);
			return -EBUSY;
		}
	}
	pr_info("%s: vcablk_major %i\n", __func__, vcablk_major);
	return 0;
}

void
vcablk_disk_exit(void)
{
	if (vcablk_major > 0)
		unregister_blkdev(vcablk_major, VCA_BLK_DISK_NAME);
}

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
#ifndef _VOP_KVEC_BUFF_H_
#define _VOP_KVEC_BUFF_H_

#include "../vca_virtio/include/vca_virtio_ring.h"

#define KVEC_BUF_NUM 3

struct buffer_dma_item;
struct vop_device;
struct vop_dev_common;

/**
 * enum vop_heads_up_notification
 *
 * types of notification sent out via heads up irq mechanism
 */
enum vop_heads_up_notification {
	vop_notify_available,
	vop_notify_used,
};

/**
 * struct vop_heads_up_irq - heads up irq data
 *     Data is used to manage sending and receiving heads up irq.
 *     These interrupts are sent only when previous notification has
 *     expired.
 *
 * @wq - queue to wait for heads up interrupt interrupt
 * @rcv_ts - timestamp of last received interrupt
 * @sent_ts - timestamp of last sent interrupt
 * @op - type of interrupt notification - when sending irq lower layer translates
 *      this to doorbell number
 */
struct vop_heads_up_irq
{
	wait_queue_head_t wq;
	u64 rcv_ts;
	u64 sent_ts;
	enum vop_heads_up_notification op;
};

#define VOP_KVEC_ELEM_ALIGNMENT 8

/*
 * KVEC ring buffer size might not to be power of 2. Because of this we need to
 * take free running counter modulo multiplicity of buffer size. By "counter"
 * we mean a value incremented each time something is inserted to the buffer -
 * - it is likely greater than buffer size. By index we mean a position in the
 * ring buffer (always < ring buffer size).
 */
#define KVEC_COUNTER_EQ(cnt1, cnt2, size) (((cnt1) % (2 * (size))) == \
					   ((cnt2) % (2 * (size))))
#define KVEC_COUNTER_ADD(cnt, val, size) ((cnt) + (val)) % (2 * (size))
#define KVEC_COUNTER_TO_IDX(cnt, size) ((cnt) % (size))

#define KVEC_COUNTER_USED(last_cnt, cnt, size) ((cnt >= last_cnt)? \
		(cnt - last_cnt):(cnt + ((size) *2) - last_cnt))

/* Avail ring item flags - used to synchronize collecting items from ring and
  cancellation procedure (to avoid marking empty descriptors as cancelled). Accessing
  flags is done in critical section (common for entire ring).
  If IN_USE is set, cancel request is ignored as the transfer is already in progress
  and is expected to finish soon.
*/
/* write started/scheduled */
#define VOP_KVEC_FLAG_IN_USE (1 << 0)
/* peer read request cancelled - skip this descriptor */
#define VOP_KVEC_FLAG_CANCELLED (1 << 1)

/**
 * struct vop_peer_kvec - information about peer kvec.
 *
 * Receive kvecs are taken out of local receive queue and provided to
 * the other side of PCI bridge via ring buffer of struct vop_peer_kvec
 * elements,
 *
 * @iov - remote side kvec
 * @head - head value identifying kiov (kvec mangler) for this kvec
 * @flags - info fags used to synchronize collecting items from ring and
           cancellation procedure (to avoid cancelling already cancelled
	   request).
 */
struct vop_peer_kvec {
	struct kvec iov;
	u16 head;
	u8 flags;
} __attribute__((aligned(VOP_KVEC_ELEM_ALIGNMENT)));

/**
 * struct vop_peer_used_kiov - information about used peer kvec
 *
 * @len - amount of data used in kvec
 * @head - head value identifying kiov (kvec mangler)
 */
struct vop_peer_used_kiov {
	u32 len;
	u16 head;
} __attribute__((aligned(VOP_KVEC_ELEM_ALIGNMENT)));


/*status codes for request cancelation */
typedef enum
{
	VOP_REQUEST_CANCEL_OK = 0x1,
	VOP_REQUEST_CANCEL_INVALID_REQUEST,
	VOP_REQUEST_CANCEL_TIMEOUT,
	VOP_REQUEST_CANCEL_ERROR,
} VOP_REQUEST_CANCEL_STATUS;

/**
 * struct vop_peer_kvec - header for peer kiovs shared memory
 *
 * Receive kvecs ara obtained from the local receive queue and provided to
 * the other side of PCI bridge via ring buffer. There are a few ring buffers, each
 * storing kvecs of different size. This structure specifies common header layout for
 * the shared memory containing peer kvec ring buffers.
 *
 * @idx - array of indexes for peer kvec ring buffers. Each time new element is inserted
 *        to one of peer kvec ring buffers, corresponding index is incremented
 * @used_idx - head index in used kvecs ring buffer
 * @send_max_size - max size of kvec for each peer kvec ring.
 * @cancelled_request_idx - index of cancelled request in ring
 * @cancelled_request_ring_id - cancelled request Available ring ID
 * @request_cancellation - set when read request is cancelled
 * @cancellation_status - operation status set by the peer
 * @vop_peer_kvec - this is where the first peer kvec ring buffer starts
 */
struct vop_peer_kvec_buf_header {
	u16 idx[KVEC_BUF_NUM] __attribute__((aligned(VOP_KVEC_ELEM_ALIGNMENT)));
        u16 used_idx;
	u32 send_max_size[KVEC_BUF_NUM] __attribute__((aligned(VOP_KVEC_ELEM_ALIGNMENT)));
        u32 cancelled_request_idx;
        u16 cancelled_request_ring_id;
        u8  request_cancellation;
	u8  cancellation_status;

	struct vop_peer_kvec buff[];
}__attribute__((aligned(VOP_KVEC_ELEM_ALIGNMENT)));

/**
 * struct vop_kvec_ring - kvec ring description
 *
 * @send_max_size: Size of data who can be send by kvec buffer.
 */
struct vop_kvec_ring {
	u16 last_cnt;
	u16 *cnt;
	int num;
	struct vop_peer_kvec *buf;
	u32 *send_max_size;
	u32 send_max_size_local;
	u32 stats_num;
};

struct vop_used_kiov_ring {
	u16 *cnt;
	int num;
	struct vop_peer_used_kiov* buf;
	spinlock_t lock;
	u16 last_cnt;
};

/**
 * struct vop_kvec_buf_local - info about kvec ring buffers allocated locally
 *
 * Each side allocates kvec ring buffers for the remote side to write its writeable
 * kvecs there.
 *
 * @rings - kvec rings descriptions
 * @pa - device accessible address of kvec ring buffer shared memory
 * @pages - virtual address of kvec ring buffer shared memory
 * @size - kvec ring buffer shared memory size
 * @mutex - mutex for this buffer
 */
struct vop_kvec_buf_local {
	struct vop_kvec_ring rings[KVEC_BUF_NUM];
	struct vop_used_kiov_ring used_ring;

	dma_addr_t pa;
	unsigned long pages;
	size_t size;
	struct mutex mutex;
};

/**
 * struct vop_kvec_buf_remote - info about kvec ring buffer allocated by remote host
 *
 * @pa - device accessible address of kvec ring buffer shared memory allocated by the peer
 * @va - kvec ring buffer shared memory mapped to virtual adress space
 * @vringh - pointer to vringh used to acces peer receive vring
 * @mapped - true if kvec buffer memory has been mapped
 * @is_update - buffer is being written to
 */
struct vop_kvec_buf_remote {
	struct vop_kvec_ring rings[KVEC_BUF_NUM];
	struct vop_used_kiov_ring used_ring;

	dma_addr_t pa;
	void* va;
	struct vringh_kiov kiov;
	bool mapped;
	bool is_update;
};

struct vop_kvec_buff {
	struct vop_kvec_buf_local  local_write_kvecs;
	struct vop_kvec_buf_remote remote_write_kvecs;
};

void vop_send_heads_up(struct vop_dev_common *cdev,
		struct vop_heads_up_irq* irq);

int vop_kvec_buff_init(struct vop_kvec_buff *kvec_buff,
		struct vop_device *vdev, int num_write_descriptors);

void vop_kvec_buff_deinit(struct vop_kvec_buff *kvec_buff,
		struct vop_device *vdev);

void vop_kvec_buff_reset(struct vop_kvec_buff *kvec_buff);

int vop_kvec_map_buf(struct vop_kvec_buff *kvec_buff, struct vop_device *vdev,
		int num, dma_addr_t pa);

void vop_kvec_unmap_buf(struct vop_kvec_buff *kvec_buff,
		struct vop_device *vdev);

void vop_kvec_buf_update(struct vop_dev_common *cdev);
void vop_kvec_buf_consume(struct vop_dev_common *cdev);

int vop_spin_for_used_descriptors(struct vop_dev_common *cdev);

int vop_kvec_size_to_ring_id(struct vop_dev_common *cdev, size_t size);

int vop_wait_for_avail_desc(struct vop_dev_common *cdev,
		struct vop_heads_up_irq *avail_hu_irq, int ring_id);

int vop_kvec_get(
	struct vop_dev_common *cdev,
	struct vop_device *vdev,
	struct buffer_dma_item *item);

void vop_kvec_used(struct buffer_dma_item *item);

void vop_kvec_check_cancel(struct vop_dev_common *cdev);

#endif

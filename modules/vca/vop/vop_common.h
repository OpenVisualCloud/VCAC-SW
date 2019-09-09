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
#ifndef _VOP_COMMON_H_
#define _VOP_COMMON_H_

#ifdef VCA_IN_KERNEL_BUILD
#include <linux/vca_dev_common.h>
#else
#include "../common/vca_dev_common.h"
#endif

#include "../vca_virtio/include/vca_vringh.h"
#include "vop_kvec_buff.h"

#ifndef VIRTIO_NET_F_OFFSET_RXBUF
/*
 * Copy of flag from kernel used when kernel do not have implemented feature
 * for alignment destination DMA buffer.
 */
#define __VIRTIO_NET_F_OFFSET_RXBUF	24	/* Set offset in receive buffer */
#endif

#define VOP_CHECK_FEATURE(features, bits, test_bit) \
	((test_bit) < (bits) && ((features)[(test_bit) / 8] & BIT((test_bit) % 8)))

//#define DEBUG_PERF

#ifdef DEBUG_PERF
#define DEBUG_PERF_VARIABLES(prefix) \
			unsigned long int prefix##_time_wait_us; \
			unsigned long int prefix##_time_wait_us_last; \
			unsigned long int prefix##_time_diff_max_us; \
			unsigned long prefix##_time_last_rep_jiff;
#else /* DEBUG_PERF */
#define DEBUG_PERF_VARIABLES(prefix)
#endif /* DEBUG_PERF*/

#ifndef READ_ONCE
#define READ_ONCE(x) ACCESS_ONCE(x)
#endif
#ifndef WRITE_ONCE
#define WRITE_ONCE(x, val) ( ACCESS_ONCE(x) = (val) )
#endif

/*
 * Ring buffer entries (power of 2) can't be less that VCA_MAX_VRING_ENTRIES/2
 */
#define VOP_RING_SIZE ((VCA_VRING_ENTRIES)/2)

struct buffers_dma_ring;
struct vop_device;

/**
 * struct buffer_dma_item - info about single dma transfer, including
 * destination and source addresses togeter with mappings.
 *
 * @id: unique item id
 * @status_ready: true if this item is ready for next use
 * @ring: pointer to item ring containing this item
 * @buf: intermediate buffer for this transfer item - in case extra bufferring is needed
 *       due to alignment issues
 * @buf_da: intermediate buffer DMA address (DMA engine accessible for read purpose)
 * @data_size: size of source buffer, excluding vrtio header and space appended in order
 *             to fix alignment
 * @remaped: destination virtual address
 * @src_phys: CPU physical source addess
 * @src_phys_da: DMA engine accessible address of source data
 * @src_phys_sz: source size of data part (excluding virtio header) incremented to
 *               compensate for exmpty space left in order to fix alignment, rounded
 *               to 64 bytes
 * @jiffies: timestamp for DMA start (in jiffies)
 * @vringh_tx: vringh for local virtio device transmit queue
 * @k_from: kernel io vector with input data. In standard virtio net case contains two
 *          buffers: 10-byte header and the payload buffer.
 * @head_from: head value matching k_from data. This value uniquely identifies kerel io
 *             vector within the transmit queue and is used to mark this io vector as used
 *             (in this case - transmitted)
 * @bytes_read: amount of data read from all buffers contained in source kernel io vector.
 * @kvec_buf_id: specified from which kvec ring buffer the target kiovec comes.
 * @kvec_to: pointer to target kvec in kvec ring buffer
 * @num_kvecs_to: number of kiov in target kiovector
 * @head_to: head value for target kiovector. his value uniquely identifies kerel io
 *             vector within the peer receive queue and is used to mark this io vector as use
 *             (in this case - filled in with data)
 * @bytes_written: total amount of data used in target kiovector
*/
struct buffer_dma_item {
	u16 id;
	bool status_ready;
	struct buffers_dma_ring *ring;

	/* intermediate buffer - used in case of alignment requirements not met */
	void *buf;
	dma_addr_t buf_da;

	size_t data_size; /* excluding header */
	void *remapped;
	dma_addr_t src_phys;
	dma_addr_t src_phys_da;
	size_t src_phys_sz;

	unsigned long jiffies;

	/* source vringh data */
	struct vop_vringh *vringh_tx;
	struct vringh_kiov k_from;
	u16 head_from;
	size_t bytes_read;

	/* destination kvecs info */
	int kvec_buff_id;
	struct vop_peer_kvec* kvec_to;
	unsigned num_kvecs_to;
	u16 head_to;
	size_t bytes_written;

	/* Keep to close waiting callback before deinit DMA engine. */
	struct dma_async_tx_descriptor *tx;
};

struct buffers_dma_ring {
	struct vop_dev_common *cdev;
	struct vop_device *vdev;
	struct device *dev;
	struct dma_device *dma_dev;
	size_t buf_size;
	size_t buf_pages;

	struct buffer_dma_item *items;

	u16 indicator_rcv;

	struct completion wait_avail_read;
	struct completion wait_dma_send;

	u16 counter_dma_send;
	u16 counter_done_transfer;

	u16 send_aligment_overhead;

	DEBUG_PERF_VARIABLES(wait)
	DEBUG_PERF_VARIABLES(item)
	DEBUG_PERF_VARIABLES(ioremap_busy)
};

typedef void (*vop_send_heads_up_pfn)(
	struct vop_dev_common *cdev,
	enum vop_heads_up_notification op);

#define VOP_DEV_READY_STATE_STOP        (0)
#define VOP_DEV_READY_STATE_STARTING    (1)
#define VOP_DEV_READY_STATE_WORK        (2)

struct vop_dev_common {
	volatile u8 ready;
	struct completion sync_desc_read;
	struct buffers_dma_ring *buffers_ring;

	struct completion vrd_complete;
	struct completion vdm_complete;
	struct completion vsd_complete;

	struct vop_device *vdev;

	struct vop_vringh *vringh_rcv;
	struct vop_vringh *vringh_tx;

	bool feature_desc_alignment;
	bool write_in_thread;

	struct vop_kvec_buff kvec_buff;

	vop_send_heads_up_pfn send_heads_up;
	struct vop_heads_up_irq heads_up_used_irq;
	struct vop_heads_up_irq heads_up_avail_irq;

	struct vca_device_desc *dd_self;
	struct vca_device_desc *dd_peer;

	wait_queue_head_t remap_free_queue;
};

int common_dev_init(
	struct vop_dev_common *cdev,
	struct vop_device *vdev,
	struct vop_vringh *vringh_tx,
	struct vop_vringh *vringh_rcv,
	int num_write_descriptors,
	vop_send_heads_up_pfn send_head_up_pfn,
	struct vca_device_desc *dd_self,
	struct vca_device_desc *dd_peer);

void common_dev_deinit(struct vop_dev_common *cdev, struct vop_device *vdev);

int common_dev_start(struct vop_dev_common *cdev);
void common_dev_stop(struct vop_dev_common *cdev);

void descriptor_read_notification(struct vop_dev_common *cdev);

void common_dev_heads_up_used_irq(struct vop_dev_common *cdev);
void common_dev_heads_up_avail_irq(struct vop_dev_common *cdev);

void common_dev_notify_avail(struct vop_dev_common *cdev);

int vop_common_get_descriptors(struct vop_device *vdev, struct vop_vringh* vr,
		u16 *head, struct vringh_kiov *kiov, bool read);

#endif

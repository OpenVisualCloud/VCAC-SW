/*
 * Intel VCA Software Stack (VCASS)
 *
 * Copyright(c) 2016-2019 Intel Corporation.
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
 * Intel VCA PXE support code
 */

#ifndef _VCAPXE_H_
#define _VCAPXE_H_

#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/miscdevice.h>

#include "vcapxe_ioctl.h"

#define VCAPXE_RING_SIZE 512

struct vcapxe_frame {
	char frame_data[ETH_FRAME_LEN]; // ETH_FRAME_LEN = 1514
	u32 frame_len;
};

struct vcapxe_ring {
	struct vcapxe_frame frames[VCAPXE_RING_SIZE];
	u32 read_index;
	u32 write_index;
};

struct vcapxe_shared {
	struct vcapxe_ring h2n;
	struct vcapxe_ring n2h;
	u32 doorbell;
	u32 shutdown;
};

struct vcapxe_device {
	char mdev_name[80];
	struct miscdevice mdev;
	struct mutex lock;

	spinlock_t state_lock;
	int state;

	struct plx_device *xdev;
	struct net_device *netdev;
	struct plx_pxe_hw_ops* hw_ops;

	struct vca_irq* doorbell_irq;
};

struct vcapxe_private {
	struct plx_pxe_hw_ops* hw_ops;

	struct vcapxe_device *pxe_dev;

	struct plx_device *xdev;
	struct net_device *netdev;

	struct vcapxe_shared* shared_area;

	struct work_struct shutdown_worker;

	struct sk_buff* outgoing;
};

#endif // _VCAPXE_H_

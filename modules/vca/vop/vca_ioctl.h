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
 * Intel VCA Host driver.
 *
 */
#ifndef _VCA_IOCTL_H_
#define _VCA_IOCTL_H_

#include <linux/types.h>

/*
 * vca_copy - VCA virtio descriptor copy.
 *
 * @iov: An array of IOVEC structures containing user space buffers.
 * @iovcnt: Number of IOVEC structures in iov.
 * @vr_idx: The vring index.
 * @update_used: A non zero value results in used index being updated.
 * @out_len: The aggregate of the total length written to or read from
 *	the virtio device.
 */
struct vca_copy_desc {
#ifdef __KERNEL__
	struct iovec __user *iov;
#else
	struct iovec *iov;
#endif
	__u32 iovcnt;
	__u8 vr_idx;
	__u8 update_used;
	__u32 out_len;
};

/*
 * Add a new virtio device
 * The (struct vca_device_desc *) pointer points to a device page entry
 *	for the virtio device consisting of:
 *	- struct vca_device_desc
 *	- struct vca_vqconfig (num_vq of these)
 *	- host and guest features
 *	- virtio device config space
 * The total size referenced by the pointer should equal the size returned
 * by desc_size() in vca_dev_common.h
 */
#define VCA_VIRTIO_ADD_DEVICE _IOWR('s', 1, struct vca_device_desc *)

/*
 * Copy the number of entries in the iovec and update the used index
 * if requested by the user.
 */
#define VCA_VIRTIO_COPY_DESC	_IOWR('s', 2, struct vca_copy_desc *)

#endif

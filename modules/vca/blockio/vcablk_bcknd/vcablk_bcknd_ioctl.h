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
 * Intel VCA Block IO driver.
 */
#ifndef __VCA_BLK_IOCTL_H__
#define __VCA_BLK_IOCTL_H__

#include <linux/ioctl.h>
#include <asm/types.h>
#include <linux/limits.h>
#include "../vcablk_common/vcablk.h"

/**
 * struct vcablk_bcknd_desc: Block device information shared between the
 * vca block driver bcknd and userspace bcknd
 *
 * @disk_id: Disk index.
 * @type: Disk source image: file[1]/disk device etc.  Type 0/-1 terminates.
 * @size: Size of disk
 * @fd_disc: file descriptor to file with image
 *
 *
 * @num_vq: Number of virtqueues.
 * @feature_len: Number of bytes of feature bits.  Multiply by 2: one for
   host features and one for guest acknowledgements.
 * @config_len: Number of bytes of the config array after virtqueues.
 * @status: A status byte, written by the Guest.
 * @config: Start of the following variable length config.
 */

enum VCABLK_DISK_TYPE {
	VCABLK_DISK_TYPE_UNINIT = 0x0,
	VCABLK_DISK_TYPE_FILE= 0x1,
	VCABLK_DISK_TYPE_MEMORY = 0x2,
};

enum {
	VCABLK_DISK_MODE_READ_ONLY = 0x1,
	VCABLK_DISK_MODE_READ_WRITE = 0x2
};

enum disk_state {
	DISK_STATE_CREATE = 0x0,
	DISK_STATE_OPEN = 0x2,
};

struct vcablk_disk_info_desc {
	__u16 disk_id;
	__u8 exist;
	__u8 type;
	__u8 mode;
	__u64 size;
	__u8 state;
	char file_path[PATH_MAX];
} __attribute__ ((aligned(8)));


struct vcablk_disk_open_desc {
	__u16 disk_id;
	__u8 type;
	__u8 mode;
	__u64 size;
	char file_path[PATH_MAX];
} __attribute__ ((aligned(8)));

struct vcablk_disk_close_desc {
	__u16 disk_id;
} __attribute__ ((aligned(8)));

/*
 * IOCTL commands --- we will commandeer 0x56 ('V')
 */
#define VCA_BLK_GET_VERSION     _IOWR('V', 1, __u32)
#define VCA_BLK_GET_DISKS_MAX   _IOWR('V', 2, __u32)
#define VCA_BLK_GET_DISK_INFO   _IOWR('V', 3, struct vcablk_disk_info_desc *)
#define VCA_BLK_OPEN_DISC       _IOWR('V', 4, struct vcablk_disk_open_desc *)
#define VCA_BLK_CLOSE_DISC      _IOWR('V', 5, struct vcablk_disk_close_desc *)

#endif /* __VCA_BLK_IOCTL_H__ */

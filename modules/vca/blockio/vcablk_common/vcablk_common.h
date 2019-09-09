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
#ifndef __VCA_BLK_COMMON_H__
#define __VCA_BLK_COMMON_H__

#include "vcablk.h"

typedef u64 pci_addr;

/*
 * We can tweak our hardware sector size, but the kernel talks to us
 * in terms of small sectors, always.
 */
#ifndef SECTOR_SIZE
#define SECTOR_SIZE	512
#define SECTOR_SHIFT	9
#endif
#define VOP_BLK_ALIGNMENT 8

#define REQUEST_READ 1
#define REQUEST_WRITE 2
#define REQUEST_SYNC 3

#ifndef READ_ONCE
#define READ_ONCE(x) ACCESS_ONCE(x)
#endif
#ifndef WRITE_ONCE
#define WRITE_ONCE(x, val) ( ACCESS_ONCE(x) = (val) )
#endif


struct VCA_ALIGNED_PREFIX(VOP_BLK_ALIGNMENT)
vcablk_request {
	/* Transfer parameters */
	__u64 phys_buff;
	__u64 sector;
	__u64 sectors_num;

	/* Request type */
	__u16 cookie;
	__u8 request_type;
} VCA_ALIGNED_POSTFIX(VOP_BLK_ALIGNMENT);

struct VCA_ALIGNED_PREFIX(VOP_BLK_ALIGNMENT)
vcablk_completion {
	__u16 cookie;
	__s8 ret;

} VCA_ALIGNED_POSTFIX(VOP_BLK_ALIGNMENT);

#define VCABLK_RB_GET_REQUEST(cnt, num_elems, elems) \
		VCABLK_RING_GET_OBJ(cnt, num_elems, elems, struct vcablk_request *)

#define VCABLK_RB_GET_COMPLETION(cnt, num_elems, elems) \
		VCABLK_RING_GET_OBJ(cnt, num_elems, elems, struct vcablk_completion *)

#define CMD_NONE	0
#define CMD_CREATE	1
#define CMD_RUN		2
#define CMD_STOP_NORMAL	3
#define CMD_STOP_FORCE	4
#define CMD_DESTROY	5


struct VCA_ALIGNED_PREFIX(VOP_BLK_ALIGNMENT)
vcablk_ring {
	__u16 num_elems;  /* Number of elements in ring, must be power of 2 */
	__u32 elem_size;  /* Size of element  with alignment */

	__u16 last_add;
	__u16 last_used;

	__u32 size_alloc;
	__u64 dma_addr;
	char elems[];
} VCA_ALIGNED_POSTFIX(VOP_BLK_ALIGNMENT);

struct VCA_ALIGNED_PREFIX(VOP_BLK_ALIGNMENT)
vcablk_device_response {
	__u8 command_ack;
	__u32 status;

	int done_db;

	__u64 request_ring;
	__u64 request_ring_size;
	__u64 completion_ring;
	__u64 completion_ring_size;
} VCA_ALIGNED_POSTFIX(VOP_BLK_ALIGNMENT);

struct VCA_ALIGNED_PREFIX(VOP_BLK_ALIGNMENT)
vcablk_device_ctrl {
	__u8 command; //Set by bcknd when configuration of dev block change

	/* Specific block device */
	__u64 size_bytes;
	__u8 read_only;

	int request_db;

	/* response filled by frontend in response to command */
	struct vcablk_device_response response;

} VCA_ALIGNED_POSTFIX(VOP_BLK_ALIGNMENT);

#define DESTROY_DEV_PAGE_INIT 1
#define DESTROY_DEV_PAGE_END 2
#define VCA_FRONTEND_NAME_SIZE 256

#define VCA_FLAG_EVENTS_FRONTEND_DUMP_START (1<<30)
#define VCA_FLAG_EVENTS_FRONTEND_DUMP_END (1<<31)

struct VCA_ALIGNED_PREFIX(VOP_BLK_ALIGNMENT)
vcablk_dev_page {
	/* Generic Frontend*/
	__u32 version_frontend;
	__u32 dev_page_size;

	/* Flag to detect new devpage in this same address */
	__u32 reinit_dev_page;

	char name[VCA_FRONTEND_NAME_SIZE];
	__u32 valid_magic_bcknd;
	__u32 flags_events;
	__u32 reserved[2];

	/* Generic Backend*/
	__u32 num_devices;  /*Max to add for frontend*/

	int destroy_devpage;

	int notify_backend_db; /* Frontend to backend ack, and set devpage */
	int notify_frontend_db;

	struct vcablk_device_ctrl devs[];
} VCA_ALIGNED_POSTFIX(VOP_BLK_ALIGNMENT);

#endif /* __VCA_BLK_COMMON_H__ */

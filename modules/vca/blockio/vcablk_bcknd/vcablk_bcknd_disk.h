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
#ifndef __VCA_BLK_BACKEND_DISK_H__
#define __VCA_BLK_BACKEND_DISK_H__

#include "vcablk_bcknd_ioctl.h"
struct vcablk_bcknd_dev;
struct vcablk_bcknd_disk;

struct vcablk_bcknd_disk* vcablk_bcknd_disk_create(struct vcablk_bcknd_dev *bdev,
		struct vcablk_disk_open_desc *desc);
int vcablk_bcknd_disk_prepare_request_db(struct vcablk_bcknd_disk *bckd);
int vcablk_bcknd_disk_start(struct vcablk_bcknd_disk *bckd, int done_db,
		__u64 request_ring_ph, __u64 request_ring_size,
		__u64 completion_ring_ph, __u64 completion_ring_size);
void vcablk_bcknd_disk_stop(struct vcablk_bcknd_disk *bckd);
void vcablk_bcknd_disk_destroy(struct vcablk_bcknd_disk *bckd);

enum disk_state vcablk_bcknd_disk_get_state(struct vcablk_bcknd_disk *bckd);

wait_queue_head_t* vcablk_bcknd_disk_get_probe_queue(struct vcablk_bcknd_disk *bckd);

const struct vcablk_media* vcablk_bcknd_disk_get_media(struct vcablk_bcknd_disk *bckd);
const int vcablk_bcknd_disk_get_id(struct vcablk_bcknd_disk *bckd);

#endif /* __VCA_BLK_BACKEND_DISK_H__ */

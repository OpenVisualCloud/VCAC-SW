/*
* Intel VCA Software Stack (VCASS)
*
* Copyright(c) 2015-2018 Intel Corporation.
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
* Intel VCA User Space Tools.
*/

#ifndef _VCASSD_VIRTIO_BACKEND_H_
#define _VCASSD_VIRTIO_BACKEND_H_

#include "vcassd_common.h"

#ifdef __cplusplus
extern "C" {
#endif
void add_virtio_net_device(struct vca_info *vca);
#ifdef __cplusplus
}
#endif

#endif

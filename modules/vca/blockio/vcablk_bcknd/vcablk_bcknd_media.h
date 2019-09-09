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
 * Copyright (C) 2001 Alessandro Rubini and Jonathan Corbet
 * Copyright (C) 2001 O'Reilly & Associates
 *
 * Intel VCA Block IO driver.
 *
 */
#ifndef __VCABLK_BACKEND_MEDIA_H__
#define __VCABLK_BACKEND_MEDIA_H__

#include <linux/limits.h>
#include "vcablk_bcknd_ioctl.h"

/*
 * vcablk_media - Storage to keep data in backend
 *
 * @read_only: Storage is read only.
 * @size_bytes: Size of device in bytes.
 * @file_path: Path to file if storage based on file, or name of storage.
 * @type: Type of storage device.
 * @transfer: Pointer to specific function transfer for storage device.
 * @sync: Pointer to specific function sync for storage device.
 * @data: Specific container on data for storage device.
 */
struct vcablk_media {
	bool read_only;
	size_t size_bytes;
	char file_path[PATH_MAX];
	enum VCABLK_DISK_TYPE type;

	int (*transfer)(struct vcablk_media *media, unsigned long sector,
			unsigned long nsect, char *buffer, int write);

	int (*sync)(struct vcablk_media *media);

	union {
		u8 *memory;
		struct file *file;
	} data;
};

#define vcablk_media_sync(media) \
	media->sync(media)
#define vcablk_media_transfer(media, sector, nsect, buffer, write ) \
	media->transfer(media, sector, nsect, buffer, write)

struct vcablk_media * vcablk_media_create_memory(size_t size_bytes,
		const char *file_path, bool read_only);

struct vcablk_media *vcablk_media_create_file(size_t size_bytes,
		const char *file_path, bool read_only);

void vcablk_media_destroy(struct vcablk_media *media);
size_t vcablk_media_size(const struct vcablk_media *media);
bool vcablk_media_read_only(const struct vcablk_media *media);
const char *vcablk_media_file_path(const struct vcablk_media *media);

#endif /* __VCABLK_BACKEND_MEDIA_H__ */

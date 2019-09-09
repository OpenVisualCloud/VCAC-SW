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
#include <linux/blkdev.h>
#include <linux/vmalloc.h>
#include <linux/version.h>
#include "vcablk_common/vcablk_common.h"
#include "vcablk_bcknd_media.h"

static struct file*
file_open(const char* path, int flags, int rights)
{
	struct file* file = NULL;
	mm_segment_t old_fs;
	old_fs = get_fs();
	set_fs(KERNEL_DS);
	file = filp_open(path, flags | O_LARGEFILE, rights);
	set_fs(old_fs);
	return file;
}

static void
file_close(struct file* file)
{
	filp_close(file, NULL);
}

static int
file_read(struct file* file, unsigned long long offset, unsigned char* data,
		unsigned int size)
{
	mm_segment_t old_fs;
	int ret;

	old_fs = get_fs();
	set_fs(KERNEL_DS);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 20)
	ret = kernel_read(file, data, size, &offset);
#else
	ret = vfs_read(file, data, size, &offset);
#endif
	set_fs(old_fs);
	return ret;
}

static int
file_write(struct file* file, unsigned long long offset, unsigned char* data,
		unsigned int size)
{
	mm_segment_t old_fs;
	int ret;

	old_fs = get_fs();
	set_fs(KERNEL_DS);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 20)
	ret = kernel_write(file, data, size, &offset);
#else
	ret = vfs_write(file, data, size, &offset);
#endif
	set_fs(old_fs);
	return ret;
}

static int
file_sync(struct file* file)
{
	return vfs_fsync(file, 0);
}

/*
 * Handle an I/O request for memory.
 */
static int
media_transfer_mem(struct vcablk_media *media, unsigned long sector,
		unsigned long nsect, char *buffer, int write)
{
	unsigned long offset = sector << SECTOR_SHIFT;
	unsigned long nbytes = nsect << SECTOR_SHIFT;

	if ((offset + nbytes) > media->size_bytes) {
		pr_notice("Beyond-end write (%ld %ld)\n", offset, nbytes);
		return -EIO;
	}

	if (write) {
		memcpy_toio(media->data.memory + offset, buffer, nbytes);
	} else {
		memcpy_toio(buffer, media->data.memory + offset, nbytes);
	}

	return 0;
}

/*
 * Handle an I/O request for file.
 */
static int
media_transfer_file(struct vcablk_media *media, unsigned long sector,
		unsigned long nsect, char *buffer, int write)
{
	struct file *file = media->data.file;
	unsigned long offset = sector << SECTOR_SHIFT;
	unsigned long nbytes = nsect << SECTOR_SHIFT;
	ssize_t nbytesdone;

	if ((offset + nbytes) > media->size_bytes) {
		pr_notice("Beyond-end write (%ld %ld)\n", offset, nbytes);
		return -EIO;
	}

	if (write) {
		if (media->read_only)
			return -EACCES;

		nbytesdone = file_write(file, offset, buffer, nbytes);
	} else {
		nbytesdone = file_read(file, offset, buffer, nbytes);
	}

	if (!likely(nbytes == nbytesdone)) {
		pr_warning("%s: %s ERROR nbytes %lu nbytesdone %li\n",
				media->file_path, __func__, nbytes, nbytesdone);
	}

	return 0;
}

/*
 * Handle an sync I/O request for memory.
 */
static int
media_sync_mem(struct vcablk_media *media)
{
	return 0;
}

/*
 * Handle an sync I/O request for file.
 */
static int
media_sync_file(struct vcablk_media *media)
{
	return file_sync(media->data.file);
}

static struct vcablk_media*
media_create(size_t size_bytes, const char *file_path, bool read_only)
{
	struct vcablk_media *media;

	if (strlen(file_path) + 1 >= sizeof(media->file_path))
		return NULL;

	media = vmalloc(sizeof(struct vcablk_media));
	if (!media)
		return NULL;
	memset (media, 0, sizeof(struct vcablk_media));
	media->size_bytes = size_bytes;
	media->read_only = read_only;
	strncpy(media->file_path, file_path, sizeof(media->file_path)-1);
	return media;
}

struct vcablk_media*
vcablk_media_create_file(size_t size_bytes, const char *file_path,
		bool read_only)
{
	struct vcablk_media *const media = media_create(size_bytes, file_path, read_only);
	if (media) {
		int err;
		pr_debug("%s: file disk %s \n", __func__, file_path);
		media->type = VCABLK_DISK_TYPE_FILE;
		media->data.file = file_open(media->file_path,
				media->read_only?O_RDONLY:O_RDWR, 0);
		if (!IS_ERR(media->data.file)) {
			media->transfer = media_transfer_file;
			media->sync = media_sync_file;
			return media;
		}
		err = PTR_ERR(media->data.file);
		pr_err("%s: open file failure %s %i\n", __func__, file_path, err);
		vfree(media);
		return ERR_PTR(err);
	}
	return ERR_PTR(ENOMEM);
}

struct vcablk_media*
vcablk_media_create_memory(size_t size_bytes,const char *file_path,
		bool read_only)
{
	struct vcablk_media *const media = media_create(size_bytes, file_path, read_only);
	if (media) {
		pr_debug("%s: memory disk\n", __func__);
		media->type = VCABLK_DISK_TYPE_MEMORY;
		/* Get some memory */
		media->data.memory = vmalloc(media->size_bytes);
		if (media->data.memory) {
			media->transfer = media_transfer_mem;
			media->sync = media_sync_mem;
			return media;
		}
		pr_err("%s: vmalloc failure %lu\n", __func__, size_bytes);
		vfree(media);
	}
	return ERR_PTR(ENOMEM);
}

void
vcablk_media_destroy(struct vcablk_media *media)
{
	if (!media)
		return;

	switch (media->type) {
	case VCABLK_DISK_TYPE_MEMORY: {
		if (media->data.memory) {
			vfree(media->data.memory);
			media->data.memory = NULL;
		}
		break;
	}
	case VCABLK_DISK_TYPE_FILE: {
		if (media->data.file) {
			file_close(media->data.file);
			media->data.file = 0;
		}
		break;
	}
	case VCABLK_DISK_TYPE_UNINIT:
	default:
		break;
	}

	media->type = VCABLK_DISK_TYPE_UNINIT;
	vfree(media);
}

size_t
vcablk_media_size(const struct vcablk_media *media)
{
	return media->size_bytes;
}

bool
vcablk_media_read_only(const struct vcablk_media *media)
{
	return media->read_only;
}

const char*
vcablk_media_file_path(const struct vcablk_media *media)
{
	return media->file_path;
}

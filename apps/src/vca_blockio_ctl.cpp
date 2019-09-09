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
* Intel VCA User Space Tools.
*/

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <stdbool.h>
#include <sys/stat.h>

#include "vca_blockio_ctl.h"
#include "vca_defs.h"

using namespace Printer;

const char *get_blk_state_cstr(enum blk_state state)
{
	switch (state) {
	case BLK_STATE_ACTIVE:
		return BLK_STATE_ACTIVE_STR;
	case BLK_STATE_INACTIVE:
		return BLK_STATE_INACTIVE_STR;
	case BLK_STATE_ENABLED:
		return BLK_STATE_ENABLED_STR;
	case BLK_STATE_DISABLED:
		return BLK_STATE_DISABLED_STR;
	default:
		LOG_ERROR("blk state not recognized!\n");
		return "";
	};
}

bool is_block_dev_id_valid(unsigned int id)
{
	return (id < MAX_BLK_DEVS);
}

bool extract_block_dev_id(const char *device_str, int &id) {
	bool ok = device_str
		&& (sscanf(device_str, BLK_CONFIG_NAME "%d", &id) == 1)
		&& (id >= 0)
		&& (id < MAX_BLK_DEVS);
	if (!ok) id = FAIL;
	return ok;
}

std::string get_block_dev_name_from_id(int blk_dev_id)
{
	std::string blk_disk = BLK_CONFIG_NAME + int_to_string(blk_dev_id);

	return blk_disk;
}

std::string get_mode_string(int mode)
{
	if (mode == VCABLK_DISK_MODE_READ_ONLY)
		return "RO";
	if (mode == VCABLK_DISK_MODE_READ_WRITE)
		return "RW";
	return "UNKNOWN";
}

bool check_blk_disk_exist(filehandle_t blk_dev_fd, unsigned int blk_dev_id)
{
	int rc = SUCCESS;
	struct vcablk_disk_info_desc info;

	info.disk_id = blk_dev_id;
	info.exist = 0;

	rc = ioctl(blk_dev_fd, VCA_BLK_GET_DISK_INFO, &info);
	if (rc < 0) {
		LOG_ERROR("Ioctl VCA_BLK_GET_DISK_INFO error: %s\n", strerror(errno));
		return false;
	}

	return info.exist;
}


bool is_blk_disk_opened(filehandle_t blk_dev_fd, unsigned int blk_dev_id)
{
	int rc = SUCCESS;
	struct vcablk_disk_info_desc info;

	info.disk_id = blk_dev_id;

	rc = ioctl(blk_dev_fd, VCA_BLK_GET_DISK_INFO, &info);
	if (rc < 0) {
		LOG_ERROR("Ioctl VCA_BLK_GET_DISK_INFO error: %s\n", strerror(errno));
		return false;
	}
	if (info.exist)
		return info.state==DISK_STATE_OPEN;

	return false;
}

bool is_blk_disk_rw(filehandle_t blk_dev_fd, unsigned int blk_dev_id)
{
	int rc = SUCCESS;
	struct vcablk_disk_info_desc info;

	info.disk_id = blk_dev_id;

	rc = ioctl(blk_dev_fd, VCA_BLK_GET_DISK_INFO, &info);
	if (rc < 0) {
		LOG_ERROR("Ioctl VCA_BLK_GET_DISK_INFO error: %s\n", strerror(errno));
		return false;
	}

	if (info.exist) {
		return (info.mode == VCABLK_DISK_MODE_READ_WRITE &&
			info.type == VCABLK_DISK_TYPE_FILE);
	}

	return false;
}

size_t get_blk_file_size(struct vca_blk_dev_info blk_dev_info) {
	size_t blk_dev_size;

	if (blk_dev_info.type == VCABLK_DISK_TYPE_MEMORY)
		return MB_TO_B(blk_dev_info.size_mb);
	else {
		if (file_exists(blk_dev_info.path.c_str())) {
			blk_dev_size = get_file_size(blk_dev_info.path.c_str());
			if (blk_dev_size < BLK_SECTOR_SIZE || blk_dev_size % BLK_SECTOR_SIZE) {
				LOG_ERROR("Invalid block device size. Expected multiple of %d\n", BLK_SECTOR_SIZE);
				return 0;
			}

			return blk_dev_size;
		}
		else {
			LOG_ERROR("File %s not exist\n", blk_dev_info.path.c_str());
			return 0;
		}
	}
}

int open_blk_dev(filehandle_t blk_dev_fd, struct vca_blk_dev_info blk_dev_info)
{
	int rc = SUCCESS;

	struct vcablk_disk_open_desc disk;
	size_t blk_dev_size;

	disk.disk_id = blk_dev_info.blk_dev_id;
	disk.type = blk_dev_info.type;
	disk.mode = blk_dev_info.mode == BLK_MODE_RO ? VCABLK_DISK_MODE_READ_ONLY : VCABLK_DISK_MODE_READ_WRITE;

	blk_dev_size = get_blk_file_size(blk_dev_info);
	if (blk_dev_size == 0) { // "0" means an error
		rc = -EINVAL;
		return rc;
	}
	disk.size = blk_dev_size;

	if (blk_dev_info.type == VCABLK_DISK_TYPE_MEMORY) {
		STRCPY_S(disk.file_path, BLK_RAMDISK, sizeof(disk.file_path));
	}
	else {
		STRCPY_S(disk.file_path, blk_dev_info.path.c_str(), sizeof(disk.file_path));
	}

	rc = ioctl(blk_dev_fd, VCA_BLK_OPEN_DISC, &disk);
	if (rc < 0) {
		LOG_ERROR("Ioctl VCA_BLK_OPEN_DISC error: %s\n", strerror(errno));
		return rc;
	}
	else {
		LOG_DEBUG("Block device %s has been created successfully!\n", get_block_dev_name_from_id(blk_dev_info.blk_dev_id).c_str());
	}

	return rc;
}

int close_blk_dev(filehandle_t blk_dev_fd, unsigned int blk_dev_id)
{
	int rc = SUCCESS;
	__u32 id_close = blk_dev_id;

	rc = ioctl(blk_dev_fd, VCA_BLK_CLOSE_DISC, &id_close);
	if (rc < 0) {
		if (errno == ENODEV) {
			LOG_DEBUG("Block disk %i already closed.\n", blk_dev_id);
			return SUCCESS;
		}
		else if (errno == EBUSY && blk_dev_id == 0){
			LOG_DEBUG("Ioctl VCA_BLK_CLOSE_DISC error : %s\n", strerror(errno));
			LOG_ERROR("You cannot close block device on which OS is booted, " \
					  "please use os-shutdown command first\n");
			return rc;
		}
		else {
			LOG_ERROR("Ioctl VCA_BLK_CLOSE_DISC error: %s\n", strerror(errno));
			return rc;
		}
	}
	LOG_DEBUG("Block device %s has been closed successfully!\n", get_block_dev_name_from_id(blk_dev_id).c_str());

	return rc;
}

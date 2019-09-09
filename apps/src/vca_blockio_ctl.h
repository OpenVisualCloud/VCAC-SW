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

#ifndef _VCA_BLOCKIO_H_
#define _VCA_BLOCKIO_H_

#include "helper_funcs.h"

#ifdef __cplusplus
#include <string>
#endif

#include <vcablk_bcknd/vcablk_bcknd_ioctl.h>

#define BLK_SECTOR_SIZE			512
#define MAX_BLK_DEVS			8
#define BLK_RAMDISK			"vcablk_ramdisk"
#define BLK_CONFIG_NAME			"vcablk"

#define BLK_CONFIG_MODE_STR		"mode"
#define BLK_CONFIG_PATH_STR		"path"
#define BLK_CONFIG_RAMDISK_SIZE_STR	"ramdisk-size-mb"
#define BLK_CONFIG_ENABLED_STR		"enabled"

#define BLK_STATE_ACTIVE_STR		"active"
#define BLK_STATE_INACTIVE_STR		"inactive"
#define BLK_STATE_ENABLED_STR		"enabled"
#define BLK_STATE_DISABLED_STR		"disabled"

enum blk_mode {
	BLK_MODE_RO,
	BLK_MODE_RW
};

struct vca_blk_dev_info {
	unsigned int blk_dev_id;
	enum VCABLK_DISK_TYPE type;
	enum blk_mode mode;
	std::string path;
	size_t size_mb;
	bool enabled;
};

enum blk_state {
	BLK_STATE_ACTIVE,
	BLK_STATE_INACTIVE,
	BLK_STATE_ENABLED,
	BLK_STATE_DISABLED
};

const char *get_blk_state_cstr(enum blk_state state);
// read blockio device ID from string in format vcablkN, where N is digit
bool extract_block_dev_id(const char *from, int &id);
bool is_block_dev_id_valid(unsigned int id);
std::string get_block_dev_name_from_id(int blk_dev_id);
std::string get_mode_string(int blockio_mode);
bool check_blk_disk_exist(filehandle_t blk_dev_fd, unsigned int blk_dev_id);
bool is_blk_disk_opened(filehandle_t blk_dev_fd, unsigned int blk_dev_id);
bool is_blk_disk_rw(filehandle_t blk_dev_fd, unsigned int blk_dev_id);
size_t get_blk_file_size(struct vca_blk_dev_info blk_dev_info);
int open_blk_dev(filehandle_t blk_dev_fd, struct vca_blk_dev_info blk_dev_info);
int close_blk_dev(filehandle_t blk_dev_fd, unsigned int blk_dev_id);

#endif

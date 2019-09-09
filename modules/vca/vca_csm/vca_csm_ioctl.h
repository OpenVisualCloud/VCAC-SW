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
 * Intel VCA card state management driver.
 *
 */

#ifndef _VCA_CSM_IOCTL_H_
#define _VCA_CSM_IOCTL_H_

#include <linux/types.h>
#include "../common/vca_common.h"

/**
 * struct vca_csm_ioctl_mem_desc: structure for passing memory to ioctl
 *
 * @ret: return value from IOCTL
 * @mem: pointer to memory in user space
 * @mem_size: size of memory in @mem
 */
struct vca_csm_ioctl_mem_desc {
	void *mem;
	size_t mem_size;
	enum vca_lbp_retval ret;
};

/**
 * struct vca_csm_ioctl_param_desc: structure for setting lbp parameter via ioctl
 *
 * @ret: return value from IOCTL
 *
 */
struct vca_csm_ioctl_param_desc {
	enum vca_lbp_retval ret;
	enum vca_lbp_param param;
	unsigned int value;
};

/**
 * struct vca_csm_ioctl_mac_addr_desc: structure for getting mac address
 * @ret: return value from IOCTL
 */
struct vca_csm_ioctl_mac_addr_desc {
	enum vca_lbp_retval ret;
	char mac_addr[6];
};

/**
 * struct vca_csm_ioctl_bios_param_desc: structure for get/set bios params
 * @ret: return value from IOCTL
 * @param: parameter to set/get
 * @value: value of parameter
 */
struct vca_csm_ioctl_bios_param_desc {
	enum vca_lbp_retval ret;
	enum vca_lbp_bios_param param;
	union {
		unsigned long long value;	// WIP: this is vague. IMHO we want exactly 64-bits here
		char version[8];
		struct {
			unsigned long long year : 13;
			unsigned long long month : 5;
			unsigned long long day : 6;
			unsigned long long hour : 6;
			unsigned long long minutes : 7;
			unsigned long long seconds : 7;
			unsigned long long miliseconds : 16;
			unsigned long long isDailySavingTime : 1;
			unsigned long long isAdjustDailySavingTime : 1;
		} time;
	} value;
};

struct vca_csm_ioctl_agent_cmd {
	enum vca_lbp_retval ret;
	size_t buf_size;
	char buf[]; /* char[buf_size] */
};

#define VCA_MAX_AGENT_BUF_SIZE  (4 * 1024)
#define VCA_MAX_EEPROM_BUF_SIZE (1024 * 1024)


#define LBP_HANDSHAKE _IOWR('s', 1, enum vca_lbp_retval *)

#define LBP_BOOT_RAMDISK _IOWR('s', 2, struct vca_csm_ioctl_mem_desc *)

#define LBP_BOOT_FROM_USB _IOWR('s', 3, enum vca_lbp_retval *)

#define LBP_FLASH_BIOS _IOWR('s', 4, struct vca_csm_ioctl_mem_desc *)

#define LBP_FLASH_FIRMWARE _IOWR('s', 5, enum vca_lbp_retval *)

#define LBP_SET_PARAMETER _IOWR('s', 6, struct vca_csm_ioctl_param_desc *)

#define CSM_START _IOWR('s', 7, int *)

#define CSM_STOP _IOWR('s', 8, void *)

#define LBP_GET_MAC_ADDR _IOWR('s', 9, struct vca_csm_ioctl_mac_addr_desc *)

#define LBP_SET_TIME _IOWR('s', 10, enum vca_lbp_retval *)

#define LBP_UPDATE_MAC_ADDR _IOWR('s', 11, struct vca_csm_ioctl_mem_desc *)

#define LBP_SET_BIOS_PARAM _IOWR('s', 12, struct vca_csm_ioctl_bios_param_desc *)

#define LBP_GET_BIOS_PARAM _IOWR('s', 13, struct vca_csm_ioctl_bios_param_desc *)

#define VCA_DEPRECATED_1 _IOWR('s', 14, struct vca_csm_ioctl_mem_desc *)

#define LBP_CLEAR_SMB_EVENT_LOG _IOWR('s', 15, struct vca_csm_ioctl_mem_desc *)

#define VCA_READ_EEPROM_CRC _IOR('s', 16, unsigned)

#define VCA_AGENT_COMMAND _IOWR('s', 17, struct vca_csm_ioctl_agent_cmd *)

#define LBP_BOOT_BLKDISK _IOWR('s', 18, enum vca_lbp_retval *)

#define VCA_WRITE_SPAD_POWER_BUTTON _IO('s', 19)

#define VCA_WRITE_SPAD_POWER_OFF _IO('s', 20)

#define VCA_GET_MEM_SIZE _IOWR('s', 22, int *)

#define LBP_SET_SERIAL_NR _IOWR('s', 23, struct vca_csm_ioctl_mem_desc *)

#define LBP_BOOT_VIA_PXE _IOWR('s', 24, enum vca_lbp_retval *)

#endif

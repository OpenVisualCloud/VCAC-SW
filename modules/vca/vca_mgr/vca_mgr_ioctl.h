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

#ifndef _VCA_MGR_IOCTL_H_
#define _VCA_MGR_IOCTL_H_

#include <linux/types.h>
#include "../common/vca_common.h"

/**
 * struct vca_ioctl_desc: structure for use with vca_mgr_ioctl
 *
 * @modules_id: id of a module to perform ioctl on
 * @on: to signal state of operation(for power button
 * to turn it on(true)/off(false)
 */
struct vca_ioctl_desc {
	union {
		unsigned int smb_id;
		int cpu_id;
	};
	bool hold;
	bool ret;
};

/**
 * struct vca_eeprom_desc: structure for use with
 * vca_mgr_ioctl
 *
 * @eeprom_mem: pointer to memory containing eeprom data
 * @eeprom_size: size of eeprom_mem
 */
struct vca_eeprom_desc {
	enum plx_eep_retval ret;
	size_t buf_size;
	char buf[]; /* char[data_size] */
};

#define VCA_RESET _IOWR('s', 1, struct vca_ioctl_desc *)

#define VCA_READ_CPU_NUM _IOR('s', 2, __u32 *)

#define VCA_READ_CARD_TYPE _IOR('s', 3, enum vca_card_type *)

#define VCA_SET_SMB_ID _IOWR('s', 4, unsigned int *)

#define VCA_DEPRECATED_2 _IOWR('s', 5, struct vca_eeprom_desc *)

#define VCA_POWER_BUTTON _IOWR('s', 6, struct vca_ioctl_desc *)

#define VCA_READ_MODULES_BUILD _IOR('s', 7, char *)

#define VCA_CHECK_POWER_BUTTON _IOWR('s', 8, struct vca_ioctl_desc *)

#define VCA_UPDATE_EEPROM _IOWR('s', 9, struct vca_eeprom_desc *)

#define VCA_ENABLE_GOLD_BIOS _IOW('s', 10, struct vca_ioctl_desc *)

#define VCA_DISABLE_GOLD_BIOS _IOW('s', 11, struct vca_ioctl_desc *)

#endif

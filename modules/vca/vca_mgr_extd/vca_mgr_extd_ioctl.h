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

#ifndef _VCA_MGR_EXTD_IOCTL_H_
#define _VCA_MGR_EXTD_IOCTL_H_

#include <linux/types.h>
#include "../common/vca_common.h"

/**
 * struct vca_secondary_eeprom_desc: structure for use with
 * vca_mgr_extd_ioctl
 *
 * @eeprom_mem: pointer to memory containing eeprom data
 * @eeprom_size: size of eeprom_mem
 */
struct vca_secondary_eeprom_desc {
	enum plx_eep_retval ret;
	size_t buf_size;
	char buf[]; /* char[data_size] */
};

#define VCA_DEPRECATED_3 _IOWR('e', 1, struct vca_secondary_eeprom_desc *)
#define VCA_READ_BOARD_ID _IOR('e', 2, unsigned)
#define VCA_UPDATE_SECONDARY_EEPROM _IOWR('e', 4, struct vca_secondary_eeprom_desc *)

#endif

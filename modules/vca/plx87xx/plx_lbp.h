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
 * Intel PLX87XX VCA PCIe driver
 */
#ifndef _PLX_LBP_H_
#define _PLX_LBP_H_

#define PLX_USE_CPU_DMA 1

#include "plx_lbp_ifc.h"
#define PLX_LBP_PROTOCOL_VERSION_CURRENT PLX_LBP_PROTOCOL_VERSION_2_0

#include "../common/vca_common.h"

struct plx_lbp {
	struct vca_irq *irq;
	struct completion card_wait;
	u32 i7_ddr_size_mb;

	struct {
		unsigned int i7_irq_timeout_ms;
		unsigned int i7_alloc_timeout_ms;
		unsigned int i7_cmd_timeout_ms;
		unsigned int i7_mac_write_timeout_ms;
	} parameters;
};

enum plx_lbp_param {
	PLX_LBP_PARAM_i7_READY_TIMEOUT = 0,
	PLX_LBP_PARAM_I7_MAP_TIMEOUT,
	PLX_LBP_PARAM_BOOTING_TIMEOUT_MS,
	PLX_LBP_PARAM_MAC_WRITE_TIMEOUT_MS
};

enum plx_lbp_flash_type {
	PLX_LBP_FLASH_BIOS = 0,
	PLX_LBP_FLASH_MAC,
	PLX_LBP_FLASH_SN,
	PLX_LBP_FLASH_SMB_EVENTS
};

int plx_lbp_init(struct plx_device *xdev);
int plx_lbp_copy_bios_info(struct plx_device *xdev, enum PLX_LBP_PARAM param, u64 * dest);
void plx_lbp_deinit(struct plx_device *xdev);
enum vca_lbp_retval plx_lbp_handshake(struct plx_device *xdev);
enum vca_lbp_retval plx_lbp_boot_ramdisk(struct plx_device *xdev,
 void __user * img, size_t img_size);
enum vca_lbp_retval plx_lbp_boot_blkdisk(struct plx_device *xdev);
enum vca_lbp_retval plx_lbp_boot_via_pxe(struct plx_device *xdev);
enum vca_lbp_retval plx_lbp_flash_bios(struct plx_device *xdev,
 void __user * bios_file, size_t bios_file_size);
enum vca_lbp_retval plx_lbp_clear_smb_event_log(struct plx_device *xdev,
 void __user * file, size_t file_size);
enum vca_lbp_retval plx_lbp_update_mac_addr(struct plx_device *xdev,
 void __user * file, size_t file_size);
enum vca_lbp_retval plx_lbp_set_serial_number(struct plx_device *xdev,
 void __user * file, size_t file_size);
enum vca_lbp_retval plx_lbp_boot_from_usb(struct plx_device *xdev);
enum vca_lbp_retval plx_lbp_flash_firmware(struct plx_device *xdev);
enum vca_lbp_retval plx_lbp_set_param(struct plx_device *xdev,
 enum vca_lbp_param param, unsigned int val);
enum vca_lbp_states plx_lbp_get_state(struct plx_device *xdev);
enum vca_lbp_retval plx_lbp_set_state(struct plx_device *xdev, enum vca_lbp_states state);
enum vca_lbp_rcvy_states plx_lbp_get_rcvy_state(struct plx_device *xdev);
enum vca_lbp_retval plx_lbp_get_mac_addr(struct plx_device *xdev,
 char out_mac_addr[6]);
enum vca_lbp_retval plx_lbp_set_time(struct plx_device *xdev);
enum vca_lbp_retval plx_lbp_set_bios_param(struct plx_device *xdev,
 enum PLX_LBP_PARAM param, u64 value);
enum vca_lbp_retval plx_lbp_get_bios_param(struct plx_device *xdev,
 enum PLX_LBP_PARAM param, u64 * value);
const char * plx_lbp_flash_type_to_string(struct plx_device *xdev, enum plx_lbp_flash_type flash_type);

void plx_lbp_reset_start(unsigned int card_id, unsigned int cpu_id);
void plx_lbp_reset_stop(unsigned int card_id, unsigned int cpu_id);
#endif

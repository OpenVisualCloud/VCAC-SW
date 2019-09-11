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
 * Intel VCA User Space Tools.
 */

#ifndef _VCA_CTRL_H_
#define _VCA_CTRL_H_

#include "vca_defs.h"
#include <linux/limits.h>

#define VCA_DMA_PATH		"device/dma_device/plx_dma_hang"
#define VCA_CATERR_PATH		"device/caterr"
#define VCA_ID_LED_PATH		"device/id-led"

#define VCA_UPDATE_MAC_SET_MAC_PATH VCA_CONFIG_DIR	"setmacev.nsh"
#define VCA_SET_SERIAL_NUMBER_PATH VCA_CONFIG_DIR	"setserialnr.nsh"
#define VCA_UPDATE_MAC_IMG_PATH VCA_CONFIG_DIR		"MACUpdateImage.img"
#define VCA_SET_SERIAL_NR_IMG_PATH VCA_CONFIG_DIR		"SerialNumberUpdateImage.img"
#define VCA_CLEAR_SMB_EVENT_LOG_PATH VCA_CONFIG_DIR	"ClearSMBiosEventLogImage.img"

#define VCA_JUMPER_OPEN		"rcvy_jumper_open"
#define VCA_DMA_HANG		"1"
#define VCA_CATERR_YES		"caterr"

#define CARD_ID_ARG		"card_id"
#define CPU_ID_ARG		"cpu_id"
#define FILE_PATH_ARG		"file_path"
#define MAC_ADDR_ARG		"mac_addr"
#define SN_ARG				"serial_nr"
#define SMB_ID_ARG		"smb_id"
#define TRIGGER_ARG		"trigger"
#define IP_ADDR_ARG		"ip_addr"
#define BIOS_CFG_NAME_ARG	"bios_cfg_name"
#define BIOS_CFG_VALUE_ARG	"bios_cfg_value"
#define CONFIG_CHANGE_CMD	"config"
#define CONFIG_PARAM_NAME	"config_param_name"
#define CONFIG_PARAM_VALUE	"config_param_value"
#define STRIP_PARAM		"strip_param"
#define SUBCMD			"subcmd"
#define FORCE_GET_LAST_OS_IMAGE "force_get_last_os_image"
#define FORCE_LAST_OS_IMAGE	"--force-last-os-image"

#define UPDATE_EEPROM_CMD	"update-EEPROM"
#define VCA_HW_INFO_CMD		"info-hw"
#define VCA_SYS_INFO_CMD	"info-system"
#define BOOT_CMD		"boot"

#define INFO_CMD					"info"
#define INFO_SUBCMD_HW				"hw"
#define INFO_SUBCMD_SYSTEM			"system"
#define INFO_SUBCMD_NODE_OS			"node-os"
#define INFO_SUBCMD_NODE_BIOS		"BIOS"
#define INFO_SUBCMD_NODE_MEMSIZE	"memsize"
#define INFO_SUBCMD_CPU_UUID		"cpu-uuid"
#define INFO_SUBCMD_NODE_STATS  "node-stats"

#define NETWORK_CMD		"network"
#define NETWORK_SUBCMD_ALL	"all"
#define NETWORK_SUBCMD_IP	"ip"
#define NETWORK_SUBCMD_IP6	"ip6"
#define NETWORK_SUBCMD_MAC	"mac"
#define NETWORK_SUBCMD_VM_MAC	"vm-mac"
#define NETWORK_SUBCMD_STATS	"stats"
#define NETWORK_SUBCMD_RENEW	"dhcp-renew"

#define DHCP_STRING		"dhcp"

#define BIOS_UPDATE_CMD		"update-BIOS"
#define RECOVER_BIOS_CMD	"recover-BIOS"

#define CONFIG_DEFAULT_CMD	"config-default"

#define BLOCKIO_CMD		"blockio"
#define BLOCKIO_SUBCMD_LIST	"list"
#define BLOCKIO_SUBCMD_OPEN	"open"
#define BLOCKIO_SUBCMD_CLOSE	"close"
#define BLOCKIO_ID_ARG		"blockio_id"
#define BLOCKIO_MODE_ARG	"blockio_mode"
#define BLOCKIO_MODE_PARAM_ARG	"blockio_mode_param"
#define BLOCKIO_MODE_RO		"RO"
#define BLOCKIO_MODE_RW		"RW"
#define BLOCKIO_MODE_RAMDISK	"ramdisk"
#define BLOCKIO_BOOT_DEV_NAME	"vcablk0"

#define PXE_CMD		"pxe"
#define PXE_SUBCMD_ENABLE	"enable"
#define PXE_SUBCMD_DISABLE	"disable"
#define PXE_SUBCMD_STATUS	"status"
#define PXE_BOOT_DEV_NAME	"vcapxe"

#define GOLD_CMD		"golden-BIOS"

#define BIOS_VERSION_ERROR	"00000000"
#define BIOS_MV_2_0_VERSION	"0ACIE200"
#define BIOS_MV_2_0_VERSION_SGX	"0ACIE203"

#define BIOS_CFG_SGX			"sgx"
#define BIOS_CFG_EPOCH			"epoch"		// SGX epoch stored on node (=ESON)
#define BIOS_CFG_PRM			"prm"		// Processor Reserved Memory for SGX's ECP
#define BIOS_CFG_GPU_APERTURE	"gpu-aperture"
#define BIOS_CFG_TDP			"tdp"
#define BIOS_CFG_HT				"ht"
#define BIOS_CFG_GPU			"gpu"

#define ID_LED_CMD			"id-led"
#define ID_LED_SUBCMD_ON	"on"
#define ID_LED_SUBCMD_OFF	"off"
#endif

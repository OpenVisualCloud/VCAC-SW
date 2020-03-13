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
 *
 * Definitions for leveraged boot protocol.
 */
#ifndef _PLX_LBP_IFC_H_
#define _PLX_LBP_IFC_H_

/* SPAD (scratchpad) registers id for LBP (leveraged boot protocol) */
enum PLX_LBP_SPAD {
	PLX_LBP_SPAD_BOOTPARAM_ADDR_LOW = 0, /* lower 32 bit of shared mem */
	PLX_LBP_SPAD_BOOTPARAM_ADDR_HIGH = 1,  /* higher 32 bit of shared mem */
	PLX_LBP_SPAD_i7_READY = 2,  /* layout: struct plx_lbp_i7_ready */
	PLX_LBP_SPAD_E5_READY = 3,  /* layout: struct plx_lbp_e5_ready */
	PLX_LBP_SPAD_i7_CMD = 4,    /* layout: struct plx_lbp_i7_cmd */
	PLX_LBP_SPAD_DATA_LOW = 5,  /* uint32: lower half of data */
	PLX_LBP_SPAD_DATA_HIGH = 6, /* uint32: higherhalf of data */
	PLX_LBP_SPAD_i7_ERROR = 7,  /* uint32: UEFI error code upon state
			 				    PLX_LBP_i7_GENERAL_ERROR */
};

/* definitions for PLX_SPAD_i7_READY register */
#define PLX_LBP_i7_DOWN  0
#define PLX_LBP_i7_UP    (1 << 0)
#define PLX_LBP_i7_READY (1 << 1)
#define PLX_LBP_i7_BUSY  (1 << 2)
#define PLX_LBP_i7_BOOTING  (1 << 3)
#define PLX_LBP_i7_FLASHING (1 << 4)
#define PLX_LBP_i7_DONE     (1 << 5)
#define PLX_LBP_i7_UEFI_ERROR    (1 << 6)
#define PLX_LBP_i7_GENERAL_ERROR (1 << 7)
/* Software (drivers + OS) states */
#define PLX_LBP_i7_OS_READY (1 << 8)
#define PLX_LBP_i7_NET_DEV_READY (1 << 9)
#define PLX_LBP_i7_NET_DEV_UP (1 << 10)
#define PLX_LBP_i7_NET_DEV_DOWN (1 << 11)
#define PLX_LBP_i7_NET_DEV_NO_IP (1 << 12)
#define PLX_LBP_i7_DRV_PROBE_DONE (1 << 13)
#define PLX_LBP_i7_DRV_PROBE_ERROR (PLX_LBP_i7_DRV_PROBE_DONE | PLX_LBP_i7_GENERAL_ERROR)
#define PLX_LBP_i7_DHCP_IN_PROGRESS (1 << 14)
#define PLX_LBP_i7_DHCP_DONE (1 << 15)
#define PLX_LBP_i7_DHCP_ERROR (PLX_LBP_i7_DHCP_DONE | PLX_LBP_i7_GENERAL_ERROR)
#define PLX_LBP_i7_NFS_MOUNT_DONE (1 << 16)
#define PLX_LBP_i7_NFS_MOUNT_ERROR (PLX_LBP_i7_NFS_MOUNT_DONE | PLX_LBP_i7_GENERAL_ERROR)
#define PLX_LBP_i7_BOOTING_BLOCK_IO (1 << 17)
#define PLX_LBP_i7_SOFTWARE_NODE_DOWN (1 << 18)
#define PLX_LBP_i7_OS_REBOOTING (1 << 19)
#define PLX_LBP_i7_AFTER_REBOOT (1 << 20)
#define PLX_LBP_i7_POWER_DOWN (1 << 21)
#define PLX_LBP_i7_POWERING_DOWN (1 << 22)

#define PLX_LBP_i7_ANY_ERROR (PLX_LBP_i7_UEFI_ERROR | PLX_LBP_i7_GENERAL_ERROR)

#define PLX_LBP_PROTOCOL_GET_MINOR(x) x & 0x0F
#define PLX_LBP_PROTOCOL_GET_MAJOR(x) x & 0xF0

#define PLX_LBP_PROTOCOL_VERSION_MAJOR_0 0
#define PLX_LBP_PROTOCOL_VERSION_MAJOR_1 1
#define PLX_LBP_PROTOCOL_VERSION_MAJOR_2 2
#define PLX_LBP_PROTOCOL_VERSION_MINOR_0 0
#define PLX_LBP_PROTOCOL_VERSION_MINOR_1 1
#define PLX_LBP_PROTOCOL_VERSION_MINOR_2 2
#define PLX_LBP_PROTOCOL_VERSION_MINOR_3 3
#define PLX_LBP_PROTOCOL_VERSION_MINOR_4 4
#define PLX_LBP_PROTOCOL_VERSION(MAJOR, MINOR) (((MAJOR) << 4) | (MINOR))
#define PLX_LBP_PROTOCOL_VERSION_0_2 PLX_LBP_PROTOCOL_VERSION(PLX_LBP_PROTOCOL_VERSION_MAJOR_0, PLX_LBP_PROTOCOL_VERSION_MINOR_2)
#define PLX_LBP_PROTOCOL_VERSION_0_3 PLX_LBP_PROTOCOL_VERSION(PLX_LBP_PROTOCOL_VERSION_MAJOR_0, PLX_LBP_PROTOCOL_VERSION_MINOR_3)
#define PLX_LBP_PROTOCOL_VERSION_0_4 PLX_LBP_PROTOCOL_VERSION(PLX_LBP_PROTOCOL_VERSION_MAJOR_0, PLX_LBP_PROTOCOL_VERSION_MINOR_4)
#define PLX_LBP_PROTOCOL_VERSION_1_0 PLX_LBP_PROTOCOL_VERSION(PLX_LBP_PROTOCOL_VERSION_MAJOR_1, PLX_LBP_PROTOCOL_VERSION_MINOR_0)
#define PLX_LBP_PROTOCOL_VERSION_1_1 PLX_LBP_PROTOCOL_VERSION(PLX_LBP_PROTOCOL_VERSION_MAJOR_1, PLX_LBP_PROTOCOL_VERSION_MINOR_1)
#define PLX_LBP_PROTOCOL_VERSION_1_2 PLX_LBP_PROTOCOL_VERSION(PLX_LBP_PROTOCOL_VERSION_MAJOR_1, PLX_LBP_PROTOCOL_VERSION_MINOR_2)
#define PLX_LBP_PROTOCOL_VERSION_2_0 PLX_LBP_PROTOCOL_VERSION(PLX_LBP_PROTOCOL_VERSION_MAJOR_2, PLX_LBP_PROTOCOL_VERSION_MINOR_0)

struct plx_lbp_i7_ready {
	union {
		struct {
			u32 ready : 24;
			u32 version : 8;
		};
		struct {
			u32 : 24;
			u32 major : 4;
			u32 minor : 4;
		};
		u32 value;
	};
};

/* definitions for PLX_SPAD_i7_READY register */
#define PLX_LBP_E5_NOT_READY 0
#define PLX_LBP_E5_WAITING_FOR_IRQ (1 << 0)
#define PLX_LBP_E5_READY (1 << 1)

struct plx_lbp_e5_ready {
	union {
		struct {
			u32 ready : 8;
			u32 CPUID : 8;
			u32 pci_slot_id : 8;
			u32 ntb_port_id : 8;
		};
		u32 value;
	};
};

/* definitions for PLX_SPAD_i7_CMD register */
/* DO NOT CHANGE THIS VALUES WITHOUT CONSULTING THIS WITH BIOS TEAM! */
enum PLX_LBP_CMD {
	PLX_LBP_CMD_INVALID = 0,
	PLX_LBP_CMD_BOOT_LOADER,
	PLX_LBP_CMD_MAP_RAMDISK,
	PLX_LBP_CMD_UNMAP_RAMDISK,
	PLX_LBP_CMD_BOOT_RAMDISK,
	PLX_LBP_CMD_FLASH_FW,
	PLX_LBP_CMD_FLASH_BIOS,
	PLX_LBP_CMD_FLASH_OS,
	PLX_LBP_CMD_GET_MAC_ADDR,
	PLX_LBP_CMD_SET_TIME,
	PLX_LBP_CMD_SET_PARAM,
	PLX_LBP_CMD_GET_PARAM,
	PLX_LBP_CMD_CLEAR_ERROR,
	PLX_LBP_CMD_BOOT_BLOCK_IO,
};

/* definitions of parameters used with PLX_LBP_CMD_SET_PARAM and
	PLX_LBP_CMD_GET_PARAM */
enum PLX_LBP_PARAM {
	PLX_LBP_PARAM_INVALID = 0,
	PLX_LBP_PARAM_CPU_MAX_FREQ_NON_TURBO,
	PLX_LBP_PARAM_BIOS_VER,
	PLX_LBP_PARAM_BIOS_BUILD_DATE,
	PLX_LBP_PARAM_SGX,
	PLX_LBP_PARAM_GPU_APERTURE,
	PLX_LBP_PARAM_TDP,
	PLX_LBP_PARAM_GPU,
	PLX_LBP_PARAM_HT,
	PLX_LBP_PARAM_SGX_MEM,
	PLX_LBP_PARAM_EPOCH_0,
	PLX_LBP_PARAM_EPOCH_1,
	PLX_LBP_PARAM_EPOCH_CUR_MAX = PLX_LBP_PARAM_EPOCH_1,
	PLX_LBP_PARAM_EPOCH_MAX = PLX_LBP_PARAM_EPOCH_0 + 15,
	PLX_LBP_PARAM_GET_SMBIOS_TABLE,
	PLX_LBP_PARAM_SGX_OWNER_EPOCH_TYPE,
	PLX_LBP_PARAM_SGX_TO_FACTORY,
};

enum PLX_LBP_PARAM_SGX_TO_FACTORY_PARAMS {
	PLX_LBP_PARAM_SGX_TO_FACTORY_NO = 0,
	PLX_LBP_PARAM_SGX_TO_FACTORY_YES,
};

enum PLX_LBP_PARAM_SGX_EPOCH_TYPE_PARAMS {
	PLX_LBP_PARAM_SGX_EPOCH_TYPE_NO_CHANGE = 0,
	PLX_LBP_PARAM_SGX_EPOCH_TYPE_NEW_RANDOM,
	PLX_LBP_PARAM_SGX_EPOCH_TYPE_USER_DEFINED,
};

enum PLX_LBP_PARAM_SGX_PARAMS {
	PLX_LBP_PARAM_SGX_DISABLE = 0,
	PLX_LBP_PARAM_SGX_ENABLE,
	PLX_LBP_PARAM_SGX_SOFTWARE_CONTROL,
};

enum PLX_LBP_PARAM_HT_PARAMS {
	PLX_LBP_PARAM_HT_DISABLE = 0,
	PLX_LBP_PARAM_HT_ENABLE,
};

enum PLX_LBP_PARAM_GPU_APERTURE_PARAMS {
	PLX_LBP_PARAM_GPU_APERTURE_128MB  = 0x00,
	PLX_LBP_PARAM_GPU_APERTURE_256MB  = 0x01,
	PLX_LBP_PARAM_GPU_APERTURE_512MB  = 0x03,
	PLX_LBP_PARAM_GPU_APERTURE_1024MB = 0x07,
	PLX_LBP_PARAM_GPU_APERTURE_2048MB = 0x0f,
	PLX_LBP_PARAM_GPU_APERTURE_4096MB = 0x1f,
};

enum PLX_LBP_PARAM_SGX_MEM_PARAMS {
	PLX_LBP_PARAM_SGX_MEM_AUTO  = 0x00000000,
	PLX_LBP_PARAM_SGX_MEM_32MB  = 0x02000000,
	PLX_LBP_PARAM_SGX_MEM_64MB  = 0x04000000,
	PLX_LBP_PARAM_SGX_MEM_128MB = 0x08000000,
};

#define PLX_LBP_PARAM_VERSION_0_2_CPU_MAX_FREQ_NON_TURBO_MIN_VAL 8
#define PLX_LBP_PARAM_VERSION_0_2_CPU_MAX_FREQ_NON_TURBO_MAX_VAL 17
#define PLX_LBP_PARAM_VERSION_0_2_CPU_START_TURBO 17
#define PLX_LBP_PARAM_CPU_START_TURBO 0

#define PLX_LBP_PARAM_BAR23 1
#define PLX_LBP_PARAM_BAR45 2

struct plx_lbp_i7_cmd {
	union {
		struct {
			u32 cmd : 16;
			u32 param : 16;
		};
		u32 value;
	};
};

/* definitions for PLX_SPAD_i7_PROGRESS register */
struct plx_lbp_i7_progress{
	union {
		struct {
			/* actual command that needs progress tracking */
			u32 cmd : 16;
			/* fraction of work already done (1 means 1/65535
			work is done) */
			u32 progress : 16;
		};
		u32 value;
	};
};

/* definitions for PLX_SPAD_i7_ERROR register */
enum PLX_LBP_ERROR {
	PLX_LBP_ERROR_NO_ERROR = 0,
	PLX_LBP_i7_ERROR_ALLOCATE_RAMDISK,
	PLX_LBP_i7_ERROR_MAP_RAMDISK,
	PLX_LBP_i7_ERROR_BOOT_RAMDISK,
	PLX_LBP_i7_ERROR_SET_PARAM,
	PLX_LBP_i7_ERROR_BOOT_BLOCK_IO,
	PLX_LBP_i7_ERROR_GET_PARAM,
};

/* doorbell interrupt for the card BIOS to notify host about
 * readiness for handshake.
 * */
#define PLX_LBP_DOORBELL 0

/* definitions for PLX_LBP_SPAD_DATA_HIGH/LOW */
struct plx_lbp_time {
	union {
		struct {
			u64 year : 13;
			u64 month : 5;
			u64 day : 6;
			u64 hour : 6;
			u64 minutes : 7;
			u64 seconds : 7;
			u64 miliseconds : 16;
			u64 isDailySavingTime : 1;
			u64 isAdjustDailySavingTime : 1;
		};
		u64 value;
	};
};

struct plx_lbp_bios_version {
	union {
		struct {
			u8 version[8];
		};
		u64 value;
	};
};

struct plx_lbp_timezone {
	u16 minuteswest; /* minutes west of Greenwich */
};

/*************** PROTOCOL VERSION 0.3 AND UP ***************/

struct plx_lbp_time_v3 {
	union {
		struct {
			u64 year : 12;
			u64 month : 4;
			u64 day : 5;
			u64 hour : 5;
			u64 minutes : 6;
			u64 seconds : 6;
			u64 miliseconds : 10;
			u64 isDailySavingTime : 1;
			u64 isAdjustDailySavingTime : 1;
			u64 minuteswest : 11;
		};
		u64 value;
	};
};

/*************** PROTOCOL VERSION 0.4 AND UP FLAGS ***************/
/* Values retured by data_lo register */
#define PLX_GOLD_IMAGE				(0x01l)
#define PLX_PROCESSOR_TERM_TRIP_STATUS		(0x02l)
#define PLX_POWER_BUTTON_OVERRIDE_STATUS	(0x04l)


#endif /* _PLX_LBP_IFC_H_ */

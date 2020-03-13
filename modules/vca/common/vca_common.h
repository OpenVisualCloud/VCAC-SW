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
 * Intel VCA driver.
 *
 */
#ifndef __VCA_COMMON_H_
#define __VCA_COMMON_H_
//#define RDK_SUPPORT

#include <linux/stat.h>

/* The maximum number of VCA cpus supported in a single host system. */
#define VCA_MAX_NUM_CPUS 256

#define MAX_VCA_CARD_CPUS 4
#define MAX_VCA_CARDS 8

#define PERMISSION_READ		(S_IRUSR | S_IRGRP | S_IROTH)
#define PERMISSION_WRITE_READ	(S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH)

/**
 * enum vca_card_type - type of VCA card.
 */
enum vca_card_type {
	VCA_UNKNOWN     = 0x0,
	VCA_VV_FAB1     = 0x2,
	VCA_VV_FAB2     = 0x4,
	VCA_MV_FAB1     = 0x8,
	VCA_FPGA_FAB1   = 0x10,
	VCA_VCAA_FAB1   = 0x20,
	VCA_VCGA_FAB1	= 0x40,
	VCA_VV          = VCA_VV_FAB1 | VCA_VV_FAB2,
	VCA_MV          = VCA_MV_FAB1,
	VCA_FPGA        = VCA_FPGA_FAB1,
	VCA_VCAA		= VCA_VCAA_FAB1,
	VCA_VCGA		= VCA_VCGA_FAB1,
	VCA_PRODUCTION  = VCA_VV | VCA_MV | VCA_FPGA | VCA_VCAA | VCA_VCGA
};

/**
 * enum vca_lbp_states - VCA states.
 */
enum vca_lbp_states {
	VCA_BIOS_DOWN = 0,
	VCA_BIOS_UP,	/* link up and ready for handshake*/
	VCA_BIOS_READY,		/* handshake succesful */
	VCA_OS_READY,
	VCA_NET_DEV_READY,
	VCA_BOOTING,
	VCA_FLASHING,
	VCA_RESETTING,
	VCA_BUSY,
	VCA_DONE,
	VCA_ERROR,
	VCA_NET_DEV_UP,
	VCA_NET_DEV_DOWN,
	VCA_NET_DEV_NO_IP,
	VCA_DRV_PROBE_DONE,
	VCA_DRV_PROBE_ERROR,
	VCA_DHCP_IN_PROGRESS,
	VCA_DHCP_DONE,
	VCA_DHCP_ERROR,
	VCA_NFS_MOUNT_DONE,
	VCA_NFS_MOUNT_ERROR,
	VCA_LINK_DOWN,
	VCA_BOOTING_BLOCKIO,
	VCA_OS_REBOOTING,
	VCA_AFTER_REBOOT,
	VCA_POWER_DOWN,
	VCA_POWERING_DOWN,
	VCA_SIZE
};

/**
* enum vca_lbp_rcvy_states - VCA recovery states.
*/
enum vca_lbp_rcvy_states {
	VCA_RCVY_JUMPER_CLOSE = 0,
	VCA_RCVY_JUMPER_OPEN,
	VCA_RCVY_NON_READABLE,
	VCA_RCVY_SIZE
};

enum vca_os_type {
	VCA_OS_TYPE_UNKNOWN = 0,
	VCA_OS_TYPE_LINUX,
	VCA_OS_TYPE_WINDOWS,
	VCA_OS_TYPE_SIZE
};

#define VCA_OS_TYPE_UNKNOWN_TEXT "os_unknown"
#define VCA_OS_TYPE_LINUX_TEXT "linux"
#define VCA_OS_TYPE_WINDOWS_TEXT "windows"

static inline const char * const get_vca_os_type_string(enum vca_os_type os_type)
{
	switch (os_type) {
	case VCA_OS_TYPE_UNKNOWN:
		return VCA_OS_TYPE_UNKNOWN_TEXT;
	case VCA_OS_TYPE_LINUX:
		return VCA_OS_TYPE_LINUX_TEXT;
	case VCA_OS_TYPE_WINDOWS:
		return VCA_OS_TYPE_WINDOWS_TEXT;
	case VCA_OS_TYPE_SIZE:
	default:
#ifdef __KERNEL__
		BUG_ON(true);
#else
		assert(0);
#endif
		return NULL;
	}
	return NULL;
}

#define VCA_LINK_DOWN_TEXT "link_down"
#define VCA_BIOS_DOWN_TEXT "bios_down"
#define VCA_BIOS_UP_TEXT "bios_up"
#define VCA_BIOS_READY_TEXT "bios_ready"
#define VCA_OS_READY_TEXT "os_ready"
#define VCA_NET_DEV_READY_TEXT "net_device_ready"
#define VCA_BOOTING_TEXT "booting"
#define VCA_FLASHING_TEXT "flashing"
#define VCA_RESETTING_TEXT "resetting"
#define VCA_BUSY_TEXT "busy"
#define VCA_DONE_TEXT "done"
#define VCA_ERROR_TEXT "error"
#define VCA_NET_DEV_UP_TEXT "net_device_up"
#define VCA_NET_DEV_DOWN_TEXT "net_device_down"
#define VCA_NET_DEV_NO_IP_TEXT "net_device_no_ip"
#define VCA_DRV_PROBE_DONE_TEXT "drv_probe_done"
#define VCA_DRV_PROBE_ERROR_TEXT "drv_probe_error"
#define VCA_DHCP_IN_PROGRESS_TEXT "dhcp_in_progress"
#define VCA_DHCP_DONE_TEXT "dhcp_done"
#define VCA_DHCP_ERROR_TEXT "dhcp_error"
#define VCA_NFS_MOUNT_DONE_TEXT "nfs_mount_done"
#define VCA_NFS_MOUNT_ERROR_TEXT "nfs_mount_error"
#define VCA_BOOTING_BLOCKIO_TEXT "booting"
#define VCA_OS_REBOOTING_TEXT "os_rebooting"
#define VCA_AFTER_REBOOT_TEXT "waiting_for_boot"
#define VCA_POWER_DOWN_TEXT "power_off"
#define VCA_POWERING_DOWN_TEXT "powering_down"
/*
 * A state-to-string lookup table, for exposing a human readable state
 * via sysfs. Always keep in sync with enum vca_lbp_states
 */
static inline const char * const get_vca_state_string(enum vca_lbp_states state)
{
	switch(state) {
		case VCA_BIOS_DOWN:
			return VCA_BIOS_DOWN_TEXT;
		case VCA_BIOS_UP:
			return VCA_BIOS_UP_TEXT;
		case VCA_BIOS_READY:
			return VCA_BIOS_READY_TEXT;
		case VCA_OS_READY:
			return VCA_OS_READY_TEXT;
		case VCA_NET_DEV_READY:
			return VCA_NET_DEV_READY_TEXT;
		case VCA_BOOTING:
			return VCA_BOOTING_TEXT;
		case VCA_FLASHING:
			return VCA_FLASHING_TEXT;
		case VCA_RESETTING:
			return VCA_RESETTING_TEXT;
		case VCA_BUSY:
			return VCA_BUSY_TEXT;
		case VCA_DONE:
			return VCA_DONE_TEXT;
		case VCA_ERROR:
			return VCA_ERROR_TEXT;
		case VCA_NET_DEV_UP:
			return VCA_NET_DEV_UP_TEXT;
		case VCA_NET_DEV_DOWN:
			return VCA_NET_DEV_DOWN_TEXT;
		case VCA_NET_DEV_NO_IP:
			return VCA_NET_DEV_NO_IP_TEXT;
		case VCA_DRV_PROBE_DONE:
			return VCA_DRV_PROBE_DONE_TEXT;
		case VCA_DRV_PROBE_ERROR:
			return VCA_DRV_PROBE_ERROR_TEXT;
		case VCA_DHCP_IN_PROGRESS:
			return VCA_DHCP_IN_PROGRESS_TEXT;
		case VCA_DHCP_DONE:
			return VCA_DHCP_DONE_TEXT;
		case VCA_DHCP_ERROR:
			return VCA_DHCP_ERROR_TEXT;
		case VCA_NFS_MOUNT_DONE:
			return VCA_NFS_MOUNT_DONE_TEXT;
		case VCA_NFS_MOUNT_ERROR:
			return VCA_NFS_MOUNT_ERROR_TEXT;
		case VCA_LINK_DOWN:
			return VCA_LINK_DOWN_TEXT;
		case VCA_BOOTING_BLOCKIO:
			return VCA_BOOTING_BLOCKIO_TEXT;
		case VCA_OS_REBOOTING:
			return VCA_OS_REBOOTING_TEXT;
		case VCA_AFTER_REBOOT:
			return VCA_AFTER_REBOOT_TEXT;
		case VCA_POWER_DOWN:
			return VCA_POWER_DOWN_TEXT;
		case VCA_POWERING_DOWN:
			return VCA_POWERING_DOWN_TEXT;
		case VCA_SIZE:
		default:
#ifdef __KERNEL__
			BUG_ON(true);
#else
			assert(0);
#endif
			return NULL;
	}
	return NULL;
}

/*
 * A string-to-state lookup for translating a human readable state
 * from sysfs to LBP state. Always keep in sync with enum vca_lbp_states
 *
 * Return VCA_SIZE if invalid state string provided
 */
static inline enum vca_lbp_states get_vca_state_num(const char * str, size_t len)
{
	if (!strncmp(VCA_BIOS_DOWN_TEXT, str, len))
		return VCA_BIOS_DOWN;
	else if (!strncmp(VCA_BIOS_UP_TEXT, str, len))
		return VCA_BIOS_UP;
	else if (!strncmp(VCA_BIOS_READY_TEXT, str, len))
		return VCA_BIOS_READY;
	else if (!strncmp(VCA_OS_READY_TEXT, str, len))
		return VCA_OS_READY;
	else if (!strncmp(VCA_NET_DEV_READY_TEXT, str, len))
		return VCA_NET_DEV_READY;
	else if (!strncmp(VCA_BOOTING_TEXT, str, len))
		return VCA_BOOTING;
	else if (!strncmp(VCA_FLASHING_TEXT, str, len))
		return VCA_FLASHING;
	else if (!strncmp(VCA_RESETTING_TEXT, str, len))
		return VCA_RESETTING;
	else if (!strncmp(VCA_BUSY_TEXT, str, len))
		return VCA_BUSY;
	else if (!strncmp(VCA_DONE_TEXT, str, len))
		return VCA_DONE;
	else if (!strncmp(VCA_ERROR_TEXT, str, len))
		return VCA_ERROR;
	else if (!strncmp(VCA_NET_DEV_UP_TEXT, str, len))
		return VCA_NET_DEV_UP;
	else if (!strncmp(VCA_NET_DEV_DOWN_TEXT, str, len))
		return VCA_NET_DEV_DOWN;
	else if (!strncmp(VCA_NET_DEV_NO_IP_TEXT, str, len))
		return VCA_NET_DEV_NO_IP;
	else if (!strncmp(VCA_DRV_PROBE_DONE_TEXT, str, len))
		return VCA_DRV_PROBE_DONE;
	else if (!strncmp(VCA_DRV_PROBE_ERROR_TEXT, str, len))
		return VCA_DRV_PROBE_ERROR;
	else if (!strncmp(VCA_DHCP_IN_PROGRESS_TEXT, str, len))
		return VCA_DHCP_IN_PROGRESS;
	else if (!strncmp(VCA_DHCP_DONE_TEXT, str, len))
		return VCA_DHCP_DONE;
	else if (!strncmp(VCA_DHCP_ERROR_TEXT, str, len))
		return VCA_DHCP_ERROR;
	else if (!strncmp(VCA_NFS_MOUNT_DONE_TEXT, str, len))
		return VCA_NFS_MOUNT_DONE;
	else if (!strncmp(VCA_NFS_MOUNT_ERROR_TEXT, str, len))
		return VCA_NFS_MOUNT_ERROR;
	else if (!strncmp(VCA_BOOTING_BLOCKIO_TEXT, str, len))
		return VCA_BOOTING_BLOCKIO;
	else if (!strncmp(VCA_OS_REBOOTING_TEXT, str, len))
		return VCA_OS_REBOOTING;
	else if (!strncmp(VCA_AFTER_REBOOT_TEXT, str, len))
		return VCA_AFTER_REBOOT;
	else if (!strncmp(VCA_POWER_DOWN_TEXT, str, len))
		return VCA_POWER_DOWN;
	else if (!strncmp(VCA_POWERING_DOWN_TEXT, str, len))
		return VCA_POWERING_DOWN;
	else
		return VCA_SIZE;
}

static inline const char * const get_vca_rcvy_state_string(enum vca_lbp_rcvy_states rcvy_state)
{
	switch (rcvy_state)
	{
	case VCA_RCVY_JUMPER_CLOSE:
		return "rcvy_jumper_close";
	case VCA_RCVY_JUMPER_OPEN:
		return "rcvy_jumper_open";
	case VCA_RCVY_NON_READABLE:
		return "rcvy_status_non_readable";
	case VCA_RCVY_SIZE:
	default:
#ifdef __KERNEL__
		BUG_ON(true);
#else
		assert(0);
#endif
		return NULL;
	}
	return NULL;
}

/**
 * enum vca_lbp_param - VCA lbp params
 */
enum vca_lbp_param {
	VCA_LBP_PARAM_i7_IRQ_TIMEOUT_MS = 0,
	VCA_LBP_PARAM_i7_ALLOC_TIMEOUT_MS,
	VCA_LBP_PARAM_i7_CMD_TIMEOUT_MS,
	VCA_LBP_PARAM_i7_MAC_WRITE_TIMEOUT_MS,
	VCA_LBP_PARAM_SIZE
};

/**
* enum vca_lbp_bios_param - VCA lbp bios params
 */
enum vca_lbp_bios_param {
	VCA_LBP_BIOS_PARAM__INVALID = 0,
	VCA_LBP_BIOS_PARAM_CPU_MAX_FREQ_NON_TURBO,
	VCA_LBP_BIOS_PARAM_VERSION,
	VCA_LBP_BIOS_PARAM_BUILD_DATE,
	VCA_LBP_PARAM_SGX,
	VCA_LBP_PARAM_EPOCH_0,
	VCA_LBP_PARAM_EPOCH_1,
	VCA_LBP_PARAM_SGX_TO_FACTORY,
	VCA_LBP_PARAM_SGX_OWNER_EPOCH_TYPE,
	VCA_LBP_PARAM_SGX_MEM,
	VCA_LBP_PARAM_GPU_APERTURE,
	VCA_LBP_PARAM_TDP,
	VCA_LBP_PARAM_HT,
	VCA_LBP_PARAM_GPU
};

/**
 * enum vca_lbp_retval: data returning from plx_lbp functions
 */
enum vca_lbp_retval {
	LBP_STATE_OK = 0,
	LBP_SPAD_i7_WRONG_STATE,
	LBP_IRQ_TIMEOUT,
	LBP_ALLOC_TIMEOUT,
	LBP_CMD_TIMEOUT,
	LBP_BAD_PARAMETER_VALUE,
	LBP_UNKNOWN_PARAMETER,
	LBP_INTERNAL_ERROR,
	LBP_PROTOCOL_VERSION_MISMATCH,
	LBP_WAIT_INTERRUPTED,
	LBP_BIOS_INFO_CACHE_EMPTY,
	LBP_SIZE
};

enum plx_eep_retval {
	PLX_EEP_STATUS_OK = 0,
	PLX_EEP_INTERNAL_ERROR,
	PLX_EEP_TIMEOUT,
	PLX_EEP_SIZE
};

enum vca_agent_cmd {
	VCA_NO_COMMAND = 0,
	VCA_AGENT_SENSORS,
	VCA_AGENT_IP,
	VCA_AGENT_IP_STATS,
	VCA_AGENT_RENEW,
	VCA_AGENT_VM_MAC,
	VCA_AGENT_OS_SHUTDOWN,
	VCA_AGENT_OS_REBOOT,
	VCA_AGENT_OS_INFO,
	VCA_AGENT_CPU_UUID,
	VCA_AGENT_NODE_STATS,
	VCA_AGENT_SN_INFO,
	VCA_AGENT_MEM_INFO,
	VCA_AGENT_CMD_SIZE
};

/* Check if not crossing 16MB boundary for DMA zone and display info if so */
#define CHECK_DMA_ZONE(MODULE, VAL)	if ((u64)(VAL) < 0x1000000) \
	dev_dbg((MODULE), "%s %d: Address below 16M: 0x%llx\n", \
	__func__, \
	__LINE__, \
	(u64)(VAL))

#endif

#define VCA_DMA_LINK_NAME "dma_device"

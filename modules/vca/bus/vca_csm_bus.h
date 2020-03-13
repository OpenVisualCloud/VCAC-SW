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
 * Intel VCA card state managment bus driver.
 */
#ifndef _VCA_CSM_BUS_H_
#define _VCA_CSM_BUS_H_

#include <linux/device.h>
#include <linux/version.h>
#include <linux/cdev.h>
#include <linux/miscdevice.h>
#include "../common/vca_dev_common.h"
#include "../common/vca_dev.h"
#include "../common/vca_common.h"
/*#include "../scif/scif.h"*/
#include "../vca_csm/vca_csm_ioctl.h"

struct vca_csm_device_id {
	u32 device;
	u32 vendor;
};

#define VCA_CSM_DEV_VCA 1
#define VCA_CSM_DEV_ANY_ID 0xffffffff

/**
 * vca_csm_device - representation of a vca_csm device
 *
 * @attr_group: Pointer to list of sysfs attribute groups.
 * @sdev: Device for sysfs entries.
 * @vca_csm_mutex: Mutex for synchronizing access to data structures.
 * @vca_csm_host_mutex: Mutex for synchronizing vca_agent_commands.
 * @hw_ops: the hardware bus ops for this device.
 * @id: Device id
 * @dev: underlying device.
 * @index: unique position on the vca_csm bus
 * @chdev: char device for vca_csm
 * @list: for vca_csm_list
 * @net_config_va: virtual address of net_config memory
 * @net_config_windows_va: virtual address of net_config_windows memory
 * @sys_config_va: virtual address of sys_config memory
 */
struct vca_csm_device {
	const struct attribute_group **attr_group;
	struct device *sdev;
	struct mutex vca_csm_mutex;
	struct mutex vca_csm_host_mutex;

	struct vca_csm_hw_ops *hw_ops;
	struct vca_csm_device_id id;
	struct device dev;
	int index;

	struct cdev *chdev;
	struct list_head list;

	u64 net_config_va;
	u64 net_config_windows_va;
	u64 sys_config_va;
};

/**
 * vca_csm_driver - operations for a vca_csm driver
 *
 * @driver: underlying device driver (populate name and owner).
 * @id_table: the ids serviced by this driver.
 * @probe: the function to call when a device is found.  Returns 0 or -errno.
 * @remove: the function to call when a device is removed.
 */
struct vca_csm_driver {
	struct device_driver driver;
	const struct vca_csm_device_id *id_table;
	int (*probe)(struct vca_csm_device *dev);
	void (*scan)(struct vca_csm_device *dev);
	void (*remove)(struct vca_csm_device *dev);
};

/**
 * vca_csm_hw_ops - vca_csm bus ops
 *
 * @start: boot VCA
 * @stop: prepare VCA for reset
 * @aper: return VCA PCIe aperture
 * @set_net_config: sets network configuration
 * @get_net_config: gets network configuration
 * @set_net_config_windows: sets network configuration for Windows
 * @get_net_config_windows: gets network configuration for Windows
 */
struct vca_csm_hw_ops {
	int (*start)(struct vca_csm_device *cdev, int id);
	void (*stop)(struct vca_csm_device *cdev, bool force);

	ssize_t (*set_net_config)(struct vca_csm_device *cdev,
		const char *buf, size_t count);
	void *(*get_net_config)(struct vca_csm_device *cdev,
		size_t *out_count);
	ssize_t(*set_net_config_windows)(struct vca_csm_device *cdev,
		const char *buf, size_t count);
	void *(*get_net_config_windows)(struct vca_csm_device *cdev,
		size_t *out_count);
	ssize_t (*set_sys_config)(struct vca_csm_device *cdev,
		const char *buf, size_t count);
	void *(*get_sys_config)(struct vca_csm_device *cdev,
		size_t *out_count);
	enum vca_os_type (*get_os_type)(struct vca_csm_device *cdev);

	void (*get_card_and_cpu_id)(struct vca_csm_device *cdev,
		u8 *out_card_id, u8 *out_cpu_id);
	u32 (*get_cpu_num)(struct vca_csm_device *cdev);
	u32(*get_meminfo)(struct vca_csm_device *cdev);
	u32 (*link_width)(struct vca_csm_device *cdev);
	u32 (*link_status)(struct vca_csm_device *cdev);
	enum vca_card_type(*get_card_type)(struct vca_csm_device *cdev);

	enum vca_lbp_retval (*lbp_boot_ramdisk)(struct vca_csm_device *cdev,
		void __user * img, size_t img_size);
	enum vca_lbp_retval (*lbp_boot_blkdisk)(struct vca_csm_device *cdev);
	enum vca_lbp_retval (*lbp_boot_from_usb)(struct vca_csm_device *cdev);
	enum vca_lbp_retval (*lbp_handshake)(struct vca_csm_device *cdev);
	enum vca_lbp_retval (*lbp_set_param)(struct vca_csm_device *cdev,
		enum vca_lbp_param param, unsigned int value );
	enum vca_lbp_retval (*lbp_flash_bios)(struct vca_csm_device *cdev,
		void __user * bios_file, size_t bios_file_size);
	enum vca_lbp_retval(*lbp_clear_smb_event_log)(struct vca_csm_device *cdev,
		void __user * file, size_t file_size);
	enum vca_lbp_retval (*lbp_update_mac_addr)(struct vca_csm_device *cdev,
		void __user * file, size_t file_size);
	enum vca_lbp_retval (*lbp_set_serial_number)(struct vca_csm_device *cdev,
		void __user * file, size_t file_size);
	enum vca_lbp_retval (*lbp_flash_firmware)(struct vca_csm_device *cdev);
	enum vca_lbp_states (*lbp_get_state)(struct vca_csm_device *cdev);
	enum vca_lbp_rcvy_states (*lbp_get_rcvy_state)(struct vca_csm_device *cdev);
	enum vca_lbp_retval (*lbp_get_mac_addr)(struct vca_csm_device *cdev,
		char out_mac_addr[6]);
	enum vca_lbp_retval (*lbp_set_time)(struct vca_csm_device *cdev);
	enum vca_lbp_retval (*lbp_set_bios_param)(struct vca_csm_device *cdev,
		enum vca_lbp_bios_param param, u64 value);
	enum vca_lbp_retval (*lbp_get_bios_param)(struct vca_csm_device *cdev,
		enum vca_lbp_bios_param param, u64 * value);

	enum vca_lbp_retval (*vca_csm_agent_command)(
		struct vca_csm_device *cdev, char* value, size_t * size);

	void (*set_link_down_flag)(struct vca_csm_device *cdev);
	void(*set_power_off_flag)(struct vca_csm_device *cdev);
	__u32 (*get_eeprom_crc)(struct vca_csm_device *cdev);
	enum vca_caterr_retval (*get_caterr)(struct vca_csm_device *cdev);
};

struct vca_csm_device *
vca_csm_register_device(struct device *pdev,
	int id, struct vca_csm_hw_ops *hw_ops);
void vca_csm_unregister_device(struct vca_csm_device *dev);
int vca_csm_register_driver(struct vca_csm_driver *drv);
void vca_csm_unregister_driver(struct vca_csm_driver *drv);

static inline struct vca_csm_device *dev_to_vca_csm(struct device *dev)
{
	return container_of(dev, struct vca_csm_device, dev);
}

static inline struct vca_csm_driver *drv_to_vca_csm(struct device_driver *drv)
{
	return container_of(drv, struct vca_csm_driver, driver);
}
#endif /* _VCA_CSM_BUS_H */

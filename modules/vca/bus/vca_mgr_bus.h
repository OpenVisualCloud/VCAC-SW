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
 * Intel VCA manager bus driver.
 */

#ifndef _VCA_MGR_BUS_H_
#define _VCA_MGR_BUS_H_

#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/kthread.h>
#include <linux/sched.h>

struct vca_mgr_device_id {
	u32 device;
	u32 vendor;
};

#define VCA_MGR_DEV_VCA 1
#define VCA_MGR_DEV_ANY_ID 0xffffffff

/**
 * vca_mgr_device - representation of a vca_mgr device
 *
 * @dev: underlying device.
 * @misc_dev_name: name of misc_dev(vca_mgr)
 * @misc_dev: vca_mgr device
 * @hw_ops: the hardware bus ops for this device
 * @id: Device id
 * @index: unique position on the vca_csm bus
 * @cpu_threads_compl: to complete all threads at once
 */
struct vca_mgr_device {
	struct device dev;
	char misc_dev_name[16];
	struct miscdevice misc_dev;
	struct vca_mgr_hw_ops *hw_ops;
	struct vca_mgr_device_id id;
	int index;

	struct completion cpu_threads_compl;
	struct completion wait_start_compl;
	struct mutex ioctrl_mutex;
};

/**
 * vca_mgr_driver - operations for a vca_mgr driver
 *
 * @driver: underlying device driver (populate name and owner).
 * @id_table: the ids serviced by this driver.
 * @probe: the function to call when a device is found.  Returns 0 or -errno.
 * @remove: the function to call when a device is removed.
 */
struct vca_mgr_driver {
	struct device_driver driver;
	const struct vca_mgr_device_id *id_table;
	int (*probe)(struct vca_mgr_device *dev);
	void (*scan)(struct vca_mgr_device *dev);
	void (*remove)(struct vca_mgr_device *dev);
};

/**
 * vca_mgr_hw_ops - vca_mgr bus ops
 *
 * @reset: trigger VCA cpus reset
 * @power_button_pressed: check power button is pressed
 * @power_button: triggers VCA cpus halt/toggle
 */

struct vca_mgr_hw_ops {
	void (*reset)(struct vca_mgr_device *vca_dev, int cpu_id);
	int (*check_power_button)(struct vca_mgr_device *vca_dev, int cpu_id);
	void (*press_power_button)(struct vca_mgr_device *vca_dev, int cpu_id, bool hold, struct completion *wait_start);
	int (*get_card_id)(struct vca_mgr_device *vca_dev);
	enum vca_card_type (*get_card_type)(struct vca_mgr_device *vca_dev);
	u32 (*get_cpu_num)(struct vca_mgr_device *vca_dev);
	void (*set_SMB_id)(struct vca_mgr_device * vca_dev, u8 id);
	enum plx_eep_retval (*update_eeprom)(struct vca_mgr_device *vca_dev,
		char *eeprom_data, size_t eeprom_size);
	__u32 (*get_eeprom_crc)(struct vca_mgr_device *vca_dev);
	void (*enable_gold_bios)(struct vca_mgr_device * vca_dev, int cpu_id);
	void (*disable_gold_bios)(struct vca_mgr_device * vca_dev, int cpu_id);
};

struct vca_mgr_device * vca_mgr_register_device(struct device *pdev,
	int id, struct vca_mgr_hw_ops *hw_ops);
void vca_mgr_unregister_device(struct vca_mgr_device *dev);
int vca_mgr_register_driver(struct vca_mgr_driver *drv);
void vca_mgr_unregister_driver(struct vca_mgr_driver *drv);

/*
 * module_vca_mgr_driver() - Helper macro for drivers that don't do
 * anything special in module init/exit.  This eliminates a lot of
 * boilerplate.  Each module may only use this macro once, and
 * calling it replaces module_init() and module_exit()
 */
#define module_vca_mgr_driver(__vca_mgr_driver) \
	module_driver(__vca_mgr_driver, vca_mgr_register_driver, \
			vca_mgr_unregister_driver)

static inline struct vca_mgr_device *dev_to_vca_mgr(struct device *dev)
{
	return container_of(dev, struct vca_mgr_device, dev);
}

static inline struct vca_mgr_driver *drv_to_vca_mgr(struct device_driver *drv)
{
	return container_of(drv, struct vca_mgr_driver, driver);
}

#endif /* _VCA_MGR_BUS_H_ */

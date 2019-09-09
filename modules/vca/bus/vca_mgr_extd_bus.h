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

#ifndef _VCA_MGR_EXTD_BUS_H_
#define _VCA_MGR_EXTD_BUS_H_

#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/kthread.h>
#include <linux/sched.h>

struct vca_mgr_extd_device_id {
	u32 device;
	u32 vendor;
};

#define VCA_MGR_EXTD_DEV_VCA 1
#define VCA_MGR_EXTD_DEV_ANY_ID 0xffffffff

/**
 * vca_mgr_extd_device - representation of a vca_mgr_extd device
 *
 * @dev: underlying device.
 * @misc_dev_name: name of misc_dev(vca_mgr_extd)
 * @misc_dev: vca_mgr_extd device
 * @hw_ops: the hardware bus ops for this device
 * @id: Device id
 * @index: unique position on the vca_csm bus
 * @cpu_threads_compl: to complete all threads at once
 */
struct vca_mgr_extd_device {
	struct device dev;
	char misc_dev_name[16];
	struct miscdevice misc_dev;
	struct vca_mgr_extd_hw_ops *hw_ops;
	struct vca_mgr_extd_device_id id;
	int index;

	struct completion cpu_threads_compl;
};

/**
 * vca_mgr_extd_driver - operations for a vca_mgr_extd driver
 *
 * @driver: underlying device driver (populate name and owner).
 * @id_table: the ids serviced by this driver.
 * @probe: the function to call when a device is found.  Returns 0 or -errno.
 * @remove: the function to call when a device is removed.
 */
struct vca_mgr_extd_driver {
	struct device_driver driver;
	const struct vca_mgr_extd_device_id *id_table;
	int (*probe)(struct vca_mgr_extd_device *dev);
	void (*scan)(struct vca_mgr_extd_device *dev);
	void (*remove)(struct vca_mgr_extd_device *dev);
};

/**
 * vca_mgr_extd_hw_ops - vca_mgr_extd bus ops
 */
struct vca_mgr_extd_hw_ops {
	enum plx_eep_retval(*update_eeprom)(struct vca_mgr_extd_device *vca_dev,
		char *eeprom_data, size_t eeprom_size);
	int (*get_card_id)(struct vca_mgr_extd_device *vca_dev);
	__u32 (*get_plx_straps)(struct vca_mgr_extd_device *vca_dev);
};

struct vca_mgr_extd_device * vca_mgr_extd_register_device(struct device *pdev,
	int id, struct vca_mgr_extd_hw_ops *hw_ops);
void vca_mgr_extd_unregister_device(struct vca_mgr_extd_device *dev);
int vca_mgr_extd_register_driver(struct vca_mgr_extd_driver *drv);
void vca_mgr_extd_unregister_driver(struct vca_mgr_extd_driver *drv);

/*
 * module_vca_mgr_extd_driver() - Helper macro for drivers that don't do
 * anything special in module init/exit.  This eliminates a lot of
 * boilerplate.  Each module may only use this macro once, and
 * calling it replaces module_init() and module_exit()
 */
#define module_vca_mgr_extd_driver(__vca_mgr_extd_driver) \
	module_driver(__vca_mgr_extd_driver, vca_mgr_extd_register_driver, \
			vca_mgr_extd_unregister_driver)

static inline struct vca_mgr_extd_device *dev_to_vca_mgr_extd(struct device *dev)
{
	return container_of(dev, struct vca_mgr_extd_device, dev);
}

static inline struct vca_mgr_extd_driver *drv_to_vca_mgr_extd(struct device_driver *drv)
{
	return container_of(drv, struct vca_mgr_extd_driver, driver);
}

#endif /* _VCA_MGR_EXTD_BUS_H_ */

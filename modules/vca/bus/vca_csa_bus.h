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

#ifndef _VCA_CSA_BUS_H_
#define _VCA_CSA_BUS_H_

#include <linux/device.h>
#include <linux/version.h>
#include <linux/miscdevice.h>
#include <linux/irqreturn.h>

#include "../vca_virtio/uapi/vca_virtio_ring.h"
#include "../common/vca_dev_common.h"
#include "../common/vca_dev.h"
#include "../common/vca_common.h"

struct vca_csa_device_id {
	u32 device;
	u32 vendor;
};

#define VCA_CSA_DEV_VCA 1
#define VCA_CSA_DEV_ANY_ID 0xffffffff

/**
 * vca_csa_device - representation of a vca_csa device
 *
 * @attr_group: Pointer to list of sysfs attribute groups.
 * @sdev: Device for sysfs entries.
 * @vca_csa_mutex: Mutex for synchronizing access to data structures.
 * @dev: underlying device.
 * @hw_ops: the hardware bus ops for this device
 * @id: Device id
 * @index: unique position on the vca_csa bus
 * @h2c_csa_mem_db: doorbell for new host_csa_mem
 * @csa_mem_db_cookie: cookie for handling new host_csa_mem
 * @last_serviced_request: last request counter value seen by read handler
 * @csa_requests: request counter incremented by interrupt handler
 * @csa_request_wq: wait queue notified when request handler is incremented
 * @card_csa_mem_va: virtual address of card_csa_mem
 */
struct vca_csa_device {
	const struct attribute_group **attr_group;
	struct device *sdev;
	struct mutex vca_csa_mutex;

	struct device dev;
	struct vca_csa_hw_ops *hw_ops;
	struct vca_csa_device_id id;
	int index;

	int h2c_csa_mem_db;
	struct vca_irq *csa_mem_db_cookie;

	int last_serviced_request;
	atomic_t csa_requests;
	wait_queue_head_t csa_request_wq;
	u64 card_csa_mem_va;
};

/**
 * vca_csa_driver - operations for a vca_csa driver
 *
 * @driver: underlying device driver (populate name and owner).
 * @id_table: the ids serviced by this driver.
 * @probe: the function to call when a device is found.  Returns 0 or -errno.
 * @remove: the function to call when a device is removed.
 */
struct vca_csa_driver {
	struct device_driver driver;
	const struct vca_csa_device_id *id_table;
	int (*probe)(struct vca_csa_device *dev);
	void (*scan)(struct vca_csa_device *dev);
	void (*remove)(struct vca_csa_device *dev);
};

/**
 * vca_csa_hw_ops - vca_csa bus ops
 */
struct vca_csa_hw_ops {
	/* for irq handling */
	struct vca_irq * (*request_irq)(struct vca_csa_device *cpdev,
			irqreturn_t (*func)(int irq, void *data),
			const char *name, void *data, int intr_src);
	void (*free_irq)(struct vca_csa_device *cdev,
			struct vca_irq *cookie, void *data);
	int (*next_db)(struct vca_csa_device *cdev);
	int (*get_vca_agent_command)(struct vca_csa_device *cdev);

	void (*set_h2c_csa_mem_db)(struct vca_csa_device *cdev, int db);

	ssize_t (*set_net_config)(struct vca_csa_device *cdev,
		const char *buf, size_t count);
	ssize_t (*get_net_config)(struct vca_csa_device *cdev, char * out_buf);
	ssize_t (*set_sys_config)(struct vca_csa_device *cdev,
		const char *buf, size_t count);
	ssize_t (*get_sys_config)(struct vca_csa_device *cdev, char * out_buf);
	ssize_t (*set_csa_mem)(struct vca_csa_device *rdev,
		const char *buf, size_t count);
	enum vca_lbp_states (*get_state)(struct vca_csa_device *cdev);
	enum vca_lbp_retval (*set_state)(struct vca_csa_device *cdev,
					 enum vca_lbp_states state);
	void (*set_os_type)(struct vca_csa_device *cdev, enum vca_os_type os_type);
};

struct vca_csa_device * vca_csa_register_device(struct device *pdev,
	int id, struct vca_csa_hw_ops *hw_ops);
void vca_csa_unregister_device(struct vca_csa_device *dev);
int vca_csa_register_driver(struct vca_csa_driver *drv);
void vca_csa_unregister_driver(struct vca_csa_driver *drv);

static inline struct vca_csa_device *dev_to_vca_csa(struct device *dev)
{
	return container_of(dev, struct vca_csa_device, dev);
}

static inline struct vca_csa_driver *drv_to_vca_csa(struct device_driver *drv)
{
	return container_of(drv, struct vca_csa_driver, driver);
}

#endif /* _VCA_CSA_BUS_H_ */

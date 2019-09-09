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

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/idr.h>
#include <linux/version.h>

#include "vca_mgr_bus.h"

/* Unique numbering for vca_mgr devices. */
static DEFINE_IDA(vca_mgr_index_ida);

static ssize_t device_show(struct device *d,
			   struct device_attribute *attr, char *buf)
{
	struct vca_mgr_device *dev = dev_to_vca_mgr(d);
	return snprintf(buf, PAGE_SIZE, "0x%04x\n", dev->id.device);
}
#if LINUX_VERSION_CODE > KERNEL_VERSION(3, 11, 0)
static DEVICE_ATTR_RO(device);
#endif

static ssize_t vendor_show(struct device *d,
			   struct device_attribute *attr, char *buf)
{
	struct vca_mgr_device *dev = dev_to_vca_mgr(d);
	return snprintf(buf, PAGE_SIZE, "0x%04x\n", dev->id.vendor);
}
#if LINUX_VERSION_CODE > KERNEL_VERSION(3, 11, 0)
static DEVICE_ATTR_RO(vendor);
#endif

static ssize_t modalias_show(struct device *d,
			     struct device_attribute *attr, char *buf)
{
	struct vca_mgr_device *dev = dev_to_vca_mgr(d);
	return snprintf(buf, PAGE_SIZE, "vca_mgr:d%08Xv%08X\n",
		       dev->id.device, dev->id.vendor);
}
#if LINUX_VERSION_CODE > KERNEL_VERSION(3, 11, 0)
static DEVICE_ATTR_RO(modalias);

static struct attribute *vca_mgr_dev_attrs[] = {
	&dev_attr_device.attr,
	&dev_attr_vendor.attr,
	&dev_attr_modalias.attr,
	NULL,
};
ATTRIBUTE_GROUPS(vca_mgr_dev);
#else
static struct device_attribute vca_mgr_dev_attrs[] = {
	__ATTR_RO(device),
	__ATTR_RO(vendor),
	__ATTR_RO(modalias),
	__ATTR_NULL
};
#endif

static inline int vca_mgr_id_match(const struct vca_mgr_device *dev,
				const struct vca_mgr_device_id *id)
{
	if (id->device != dev->id.device && id->device != VCA_MGR_DEV_ANY_ID)
		return 0;

	return id->vendor == VCA_MGR_DEV_ANY_ID || id->vendor == dev->id.vendor;
}

/*
 * This looks through all the IDs a driver claims to support.  If any of them
 * match, we return 1 and the kernel will call vca_mgr_dev_probe().
 */
static int vca_mgr_dev_match(struct device *dv, struct device_driver *dr)
{
	unsigned int i;
	struct vca_mgr_device *dev = dev_to_vca_mgr(dv);
	const struct vca_mgr_device_id *ids;

	ids = drv_to_vca_mgr(dr)->id_table;
	for (i = 0; ids[i].device; i++)
		if (vca_mgr_id_match(dev, &ids[i]))
			return 1;
	return 0;
}

static int vca_mgr_uevent(struct device *dv, struct kobj_uevent_env *env)
{
	struct vca_mgr_device *dev = dev_to_vca_mgr(dv);

	return add_uevent_var(env, "MODALIAS=vca_mgr:d%08Xv%08X",
			      dev->id.device, dev->id.vendor);
}

static int vca_mgr_dev_probe(struct device *d)
{
	int err;
	struct vca_mgr_device *dev = dev_to_vca_mgr(d);
	struct vca_mgr_driver *drv = drv_to_vca_mgr(dev->dev.driver);

	err = drv->probe(dev);
	if (!err)
		if (drv->scan)
			drv->scan(dev);
	return err;
}

static int vca_mgr_dev_remove(struct device *d)
{
	struct vca_mgr_device *dev = dev_to_vca_mgr(d);
	struct vca_mgr_driver *drv = drv_to_vca_mgr(dev->dev.driver);

	drv->remove(dev);
	return 0;
}

struct bus_type vca_mgr_bus = {
	.name  = "vca_mgr_bus",
	.match = vca_mgr_dev_match,
#if LINUX_VERSION_CODE > KERNEL_VERSION(3, 11, 0)
	.dev_groups = vca_mgr_dev_groups,
#else
	.dev_attrs = vca_mgr_dev_attrs,
#endif
	.uevent = vca_mgr_uevent,
	.probe = vca_mgr_dev_probe,
	.remove = vca_mgr_dev_remove,
};
EXPORT_SYMBOL_GPL(vca_mgr_bus);

int vca_mgr_register_driver(struct vca_mgr_driver *driver)
{
	driver->driver.bus = &vca_mgr_bus;
	return driver_register(&driver->driver);
}
EXPORT_SYMBOL_GPL(vca_mgr_register_driver);

void vca_mgr_unregister_driver(struct vca_mgr_driver *driver)
{
	driver_unregister(&driver->driver);
}
EXPORT_SYMBOL_GPL(vca_mgr_unregister_driver);

static inline void vca_mgr_release_dev(struct device *d)
{
	struct vca_mgr_device *vca_dev = dev_to_vca_mgr(d);
	kfree(vca_dev);
}

struct vca_mgr_device *
vca_mgr_register_device(struct device *pdev, int id,
 struct vca_mgr_hw_ops *hw_ops)
{
	int ret;
	struct vca_mgr_device *vca_dev;

	vca_dev = kzalloc(sizeof(*vca_dev), GFP_KERNEL);
	if (!vca_dev)
		return ERR_PTR(-ENOMEM);

	vca_dev->dev.parent = pdev;
	vca_dev->id.device = id;
	vca_dev->id.vendor = VCA_MGR_DEV_ANY_ID;
	vca_dev->dev.release = vca_mgr_release_dev;
	vca_dev->hw_ops = hw_ops;
	dev_set_drvdata(&vca_dev->dev, vca_dev);
	vca_dev->dev.bus = &vca_mgr_bus;

	/* Assign a unique device index and hence name. */
	ret = ida_simple_get(&vca_mgr_index_ida, 0, 0, GFP_KERNEL);
	if (ret < 0)
		goto free_vca_dev;

	vca_dev->index = ret;
	dev_set_name(&vca_dev->dev, "vca_mgr-dev%u", vca_dev->index);
	/*
	 * device_register() causes the bus infrastructure to look for a
	 * matching driver.
	 */
	ret = device_register(&vca_dev->dev);
	if (ret)
		goto ida_remove;

	return vca_dev;
ida_remove:
	ida_simple_remove(&vca_mgr_index_ida, vca_dev->index);
free_vca_dev:
	kfree(vca_dev);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(vca_mgr_register_device);

void vca_mgr_unregister_device(struct vca_mgr_device *dev)
{
	int index = dev->index; /* save for after device release */

	device_unregister(&dev->dev);
	ida_simple_remove(&vca_mgr_index_ida, index);
}
EXPORT_SYMBOL_GPL(vca_mgr_unregister_device);

static int __init vca_mgr_init(void)
{
	return bus_register(&vca_mgr_bus);
}

static void __exit vca_mgr_exit(void)
{
	bus_unregister(&vca_mgr_bus);
	ida_destroy(&vca_mgr_index_ida);
}

core_initcall(vca_mgr_init);
module_exit(vca_mgr_exit);

MODULE_AUTHOR("Intel Corporation");
MODULE_DESCRIPTION("Intel(R) VCA card OS state management bus driver");
MODULE_LICENSE("GPL v2");
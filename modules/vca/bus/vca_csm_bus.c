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

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/idr.h>
#include <linux/version.h>

#include "vca_csm_bus.h"

/* Unique numbering for vca_csm devices. */
static DEFINE_IDA(vca_csm_index_ida);

static ssize_t device_show(struct device *d,
			   struct device_attribute *attr, char *buf)
{
	struct vca_csm_device *dev = dev_to_vca_csm(d);
	return snprintf(buf, PAGE_SIZE, "0x%04x\n", dev->id.device);
}
#if LINUX_VERSION_CODE > KERNEL_VERSION(3, 11, 0)
static DEVICE_ATTR_RO(device);
#endif

static ssize_t vendor_show(struct device *d,
			   struct device_attribute *attr, char *buf)
{
	struct vca_csm_device *dev = dev_to_vca_csm(d);
	return snprintf(buf, PAGE_SIZE, "0x%04x\n", dev->id.vendor);
}
#if LINUX_VERSION_CODE > KERNEL_VERSION(3, 11, 0)
static DEVICE_ATTR_RO(vendor);
#endif

static ssize_t modalias_show(struct device *d,
			     struct device_attribute *attr, char *buf)
{
	struct vca_csm_device *dev = dev_to_vca_csm(d);
	return snprintf(buf, PAGE_SIZE, "vca_csm:d%08Xv%08X\n",
		       dev->id.device, dev->id.vendor);
}
#if LINUX_VERSION_CODE > KERNEL_VERSION(3, 11, 0)
static DEVICE_ATTR_RO(modalias);

static struct attribute *vca_csm_dev_attrs[] = {
	&dev_attr_device.attr,
	&dev_attr_vendor.attr,
	&dev_attr_modalias.attr,
	NULL,
};
ATTRIBUTE_GROUPS(vca_csm_dev);
#else
static struct device_attribute vca_csm_dev_attrs[] = {
	__ATTR_RO(device),
	__ATTR_RO(vendor),
	__ATTR_RO(modalias),
	__ATTR_NULL
};
#endif

static inline int vca_csm_id_match(const struct vca_csm_device *dev,
				const struct vca_csm_device_id *id)
{
	if (id->device != dev->id.device && id->device != VCA_CSM_DEV_ANY_ID)
		return 0;

	return id->vendor == VCA_CSM_DEV_ANY_ID || id->vendor == dev->id.vendor;
}

/*
 * This looks through all the IDs a driver claims to support.  If any of them
 * match, we return 1 and the kernel will call vca_csm_dev_probe().
 */
static int vca_csm_dev_match(struct device *dv, struct device_driver *dr)
{
	unsigned int i;
	struct vca_csm_device *dev = dev_to_vca_csm(dv);
	const struct vca_csm_device_id *ids;

	ids = drv_to_vca_csm(dr)->id_table;
	for (i = 0; ids[i].device; i++)
		if (vca_csm_id_match(dev, &ids[i]))
			return 1;
	return 0;
}

static int vca_csm_uevent(struct device *dv, struct kobj_uevent_env *env)
{
	struct vca_csm_device *dev = dev_to_vca_csm(dv);

	return add_uevent_var(env, "MODALIAS=vca_csm:d%08Xv%08X",
			      dev->id.device, dev->id.vendor);
}

static int vca_csm_dev_probe(struct device *d)
{
	int err;
	struct vca_csm_device *dev = dev_to_vca_csm(d);
	struct vca_csm_driver *drv = drv_to_vca_csm(dev->dev.driver);

	err = drv->probe(dev);
	if (!err)
		if (drv->scan)
			drv->scan(dev);
	return err;
}

static int vca_csm_dev_remove(struct device *d)
{
	struct vca_csm_device *dev = dev_to_vca_csm(d);
	struct vca_csm_driver *drv = drv_to_vca_csm(dev->dev.driver);

	drv->remove(dev);
	return 0;
}

struct bus_type vca_csm_bus = {
	.name  = "vca_csm_bus",
	.match = vca_csm_dev_match,
#if LINUX_VERSION_CODE > KERNEL_VERSION(3, 11, 0)
	.dev_groups = vca_csm_dev_groups,
#else
	.dev_attrs = vca_csm_dev_attrs,
#endif
	.uevent = vca_csm_uevent,
	.probe = vca_csm_dev_probe,
	.remove = vca_csm_dev_remove,
};
EXPORT_SYMBOL_GPL(vca_csm_bus);

int vca_csm_register_driver(struct vca_csm_driver *driver)
{
	driver->driver.bus = &vca_csm_bus;
	return driver_register(&driver->driver);
}
EXPORT_SYMBOL_GPL(vca_csm_register_driver);

void vca_csm_unregister_driver(struct vca_csm_driver *driver)
{
	driver_unregister(&driver->driver);
}
EXPORT_SYMBOL_GPL(vca_csm_unregister_driver);

static inline void vca_csm_release_dev(struct device *d)
{
	struct vca_csm_device *cdev = dev_to_vca_csm(d);
	kfree(cdev);
}

struct vca_csm_device *
vca_csm_register_device(struct device *pdev, int id, struct vca_csm_hw_ops *hw_ops)
{
	int ret;
	struct vca_csm_device *cdev;

	cdev = kzalloc(sizeof(*cdev), GFP_KERNEL);
	if (!cdev)
		return ERR_PTR(-ENOMEM);

	cdev->dev.parent = pdev;
	cdev->id.device = id;
	cdev->id.vendor = VCA_CSM_DEV_ANY_ID;
	cdev->dev.release = vca_csm_release_dev;
	cdev->hw_ops = hw_ops;
	dev_set_drvdata(&cdev->dev, cdev);
	cdev->dev.bus = &vca_csm_bus;

	/* Assign a unique device index and hence name. */
	ret = ida_simple_get(&vca_csm_index_ida, 0, 0, GFP_KERNEL);
	if (ret < 0)
		goto free_cdev;

	cdev->index = ret;
	dev_set_name(&cdev->dev, "vca_csm-dev%u", cdev->index);
	/*
	 * device_register() causes the bus infrastructure to look for a
	 * matching driver.
	 */
	ret = device_register(&cdev->dev);
	if (ret)
		goto ida_remove;

	return cdev;
ida_remove:
	ida_simple_remove(&vca_csm_index_ida, cdev->index);
free_cdev:
	kfree(cdev);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(vca_csm_register_device);

void vca_csm_unregister_device(struct vca_csm_device *dev)
{
	int index = dev->index; /* save for after device release */

	device_unregister(&dev->dev);
	ida_simple_remove(&vca_csm_index_ida, index);
}
EXPORT_SYMBOL_GPL(vca_csm_unregister_device);

static int __init vca_csm_init(void)
{
	return bus_register(&vca_csm_bus);
}

static void __exit vca_csm_exit(void)
{
	bus_unregister(&vca_csm_bus);
	ida_destroy(&vca_csm_index_ida);
}

core_initcall(vca_csm_init);
module_exit(vca_csm_exit);

MODULE_AUTHOR("Intel Corporation");
MODULE_DESCRIPTION("Intel(R) VCA card OS state management bus driver");
MODULE_LICENSE("GPL v2");

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
 * Intel VCA card state management driver.
 *
 */
#include <linux/pci.h>
#include <linux/version.h>
#include "../vca_device/vca_device.h"
#include "../common/vca_common.h"
#include "vca_csm_main.h"


static ssize_t
state_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	enum vca_lbp_states state;
	struct vca_csm_device *cdev = dev_get_drvdata(dev);

	if(!cdev)
		return -EINVAL;

	state = cdev->hw_ops->lbp_get_state(cdev);

	if (state >= VCA_SIZE)
		return -EINVAL;

	return scnprintf(buf, PAGE_SIZE, "%s\n",
		get_vca_state_string(state));
}
static DEVICE_ATTR(state, PERMISSION_READ, state_show, NULL);

static ssize_t
os_type_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	enum vca_os_type os_type;
	struct vca_csm_device *cdev = dev_get_drvdata(dev);

	if (!cdev)
		return -EINVAL;

	os_type = cdev->hw_ops->get_os_type(cdev);

	if (os_type >= VCA_OS_TYPE_SIZE)
		return -EINVAL;

	return scnprintf(buf, PAGE_SIZE, "%s\n",
		get_vca_os_type_string(os_type));
}
static DEVICE_ATTR(os_type, PERMISSION_READ, os_type_show, NULL);

static ssize_t
net_config_show(struct device *dev, struct device_attribute *attr,
		 char *buf)
{
	ssize_t count;
	void * addr;

	struct vca_csm_device *cdev = dev_get_drvdata(dev);

	if (!cdev)
		return -EINVAL;

	addr = cdev->hw_ops->get_net_config(cdev, &count);

	return scnprintf(buf, count, "%s\n", (char*)addr);
}

static ssize_t
net_config_store(struct device *dev, struct device_attribute *attr,
		   const char *buf, size_t count)
{
	struct vca_csm_device *cdev = dev_get_drvdata(dev);

	if (!cdev)
		return -EINVAL;

	mutex_lock(&cdev->vca_csm_mutex);

	count = cdev->hw_ops->set_net_config(cdev, buf, count);

	mutex_unlock(&cdev->vca_csm_mutex);

	return count;
}
static DEVICE_ATTR(net_config, PERMISSION_WRITE_READ, net_config_show, net_config_store);

static ssize_t
net_config_windows_show(struct device *dev, struct device_attribute *attr,
	char *buf)
{
	ssize_t count;
	void * addr;

	struct vca_csm_device *cdev = dev_get_drvdata(dev);

	if (!cdev)
		return -EINVAL;

	addr = cdev->hw_ops->get_net_config_windows(cdev, &count);

	return scnprintf(buf, count, "%s\n", (char*)addr);
}

static ssize_t
net_config_windows_store(struct device *dev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	struct vca_csm_device *cdev = dev_get_drvdata(dev);

	if (!cdev)
		return -EINVAL;

	mutex_lock(&cdev->vca_csm_mutex);

	count = cdev->hw_ops->set_net_config_windows(cdev, buf, count);

	mutex_unlock(&cdev->vca_csm_mutex);

	return count;
}
static DEVICE_ATTR(net_config_windows, PERMISSION_WRITE_READ, net_config_windows_show, net_config_windows_store);

static ssize_t
sys_config_show(struct device *dev, struct device_attribute *attr,
		 char *buf)
{
	ssize_t count;
	void * addr;

	struct vca_csm_device *cdev = dev_get_drvdata(dev);

	if (!cdev)
		return -EINVAL;

	addr = cdev->hw_ops->get_sys_config(cdev, &count);

	return scnprintf(buf, count, "%s\n", (char*)addr);
}

static ssize_t
sys_config_store(struct device *dev, struct device_attribute *attr,
		   const char *buf, size_t count)
{
	struct vca_csm_device *cdev = dev_get_drvdata(dev);

	if (!cdev)
		return -EINVAL;

	mutex_lock(&cdev->vca_csm_mutex);

	count = cdev->hw_ops->set_sys_config(cdev, buf, count);

	mutex_unlock(&cdev->vca_csm_mutex);

	return count;
}
static DEVICE_ATTR(sys_config, PERMISSION_WRITE_READ, sys_config_show, sys_config_store);

const static char* link_up_str = "up\n";
const static char* link_down_str = "down\n";
static ssize_t
link_state_show(struct device *dev, struct device_attribute *attr,
		 char *buf)
{
	u32 link_state;
	struct vca_csm_device *cdev = dev_get_drvdata(dev);

	if (!cdev)
		return -EINVAL;

	link_state = cdev->hw_ops->link_status(cdev);

	if (link_state)
		return scnprintf(buf, strlen(link_up_str)+1, link_up_str);

	return scnprintf(buf, strlen(link_down_str)+1, link_down_str);
}
static DEVICE_ATTR(link_state, PERMISSION_READ, link_state_show, NULL);

static ssize_t
link_width_show(struct device *dev, struct device_attribute *attr,
		 char *buf)
{
	u32 link_width;
	struct vca_csm_device *cdev = dev_get_drvdata(dev);

	if (!cdev)
		return -EINVAL;

	link_width = cdev->hw_ops->link_width(cdev);

	return scnprintf(buf, 6, "%d\n", link_width);
}
static DEVICE_ATTR(link_width, PERMISSION_READ, link_width_show, NULL);

static ssize_t
bios_flags_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	enum vca_lbp_rcvy_states state;
	struct vca_csm_device *cdev = dev_get_drvdata(dev);

	if (!cdev)
		return -EINVAL;

	state = cdev->hw_ops->lbp_get_rcvy_state(cdev);

	if (state >= VCA_RCVY_SIZE)
		return -EINVAL;

	return scnprintf(buf, PAGE_SIZE, "%s\n",
		get_vca_rcvy_state_string(state));
}
static DEVICE_ATTR(bios_flags, PERMISSION_READ, bios_flags_show, NULL);

static struct attribute *vca_csm_default_attrs[] = {
	&dev_attr_state.attr,
	&dev_attr_os_type.attr,
	&dev_attr_net_config.attr,
	&dev_attr_net_config_windows.attr,
	&dev_attr_sys_config.attr,
	&dev_attr_link_state.attr,
	&dev_attr_link_width.attr,
	&dev_attr_bios_flags.attr,
	NULL
};

ATTRIBUTE_GROUPS(vca_csm_default);

void vca_csm_sysfs_init(struct vca_csm_device *cdev)
{
	cdev->attr_group = vca_csm_default_groups;
}

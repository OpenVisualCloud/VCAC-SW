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
#include <linux/sched.h>
#include <linux/wait.h>
#include "../vca_device/vca_device.h"
#include "vca_csa_main.h"

#define WAIT_FOR_DATA_TIMEOUT_MS 2000

#ifndef __ATTR_RW
#define __ATTR_RW(_name) __ATTR(_name, (S_IWUSR | S_IRUGO),		\
		 _name##_show, _name##_store)
#endif

#ifndef DEVICE_ATTR_RW
#define DEVICE_ATTR_RW(_name) \
	struct device_attribute dev_attr_##_name = __ATTR_RW(_name)
#endif

static ssize_t
csa_mem_show(struct device *dev, struct device_attribute *attr,
		 char *buf)
{
	char vca_agent_cmd;
	struct vca_csa_device *cdev = dev_get_drvdata(dev);
	int count = 0;
	long res;

	if (!cdev)
		return -EINVAL;

	res = wait_event_interruptible_timeout(cdev->csa_request_wq,
			cdev->last_serviced_request != atomic_read(&cdev->csa_requests),
			msecs_to_jiffies(WAIT_FOR_DATA_TIMEOUT_MS));

	if (!res)
		return 0;
	else if (res < 0)
		return -EINTR;

	vca_agent_cmd = cdev->hw_ops->get_vca_agent_command(cdev);
	switch (vca_agent_cmd) {
	case VCA_AGENT_SENSORS:
		count = snprintf(buf, PAGE_SIZE, "sensors");
		break;
	case VCA_AGENT_IP:
		count = snprintf(buf, PAGE_SIZE, "ip");
		break;
	case VCA_AGENT_IP_STATS:
		count = snprintf(buf, PAGE_SIZE, "ip_stats");
		break;
	case VCA_AGENT_RENEW:
		count = snprintf(buf, PAGE_SIZE, "dhcp_renew");
		break;
	case VCA_AGENT_VM_MAC:
		count = snprintf(buf, PAGE_SIZE, "vm_mac");
		break;
	case VCA_AGENT_OS_SHUTDOWN:
		count = snprintf(buf, PAGE_SIZE, "os_shutdown");
		break;
	case VCA_AGENT_OS_REBOOT:
		count = snprintf(buf, PAGE_SIZE, "os_reboot");
		break;
	case VCA_AGENT_OS_INFO:
		count = snprintf(buf, PAGE_SIZE, "os_info");
		break;
	case VCA_AGENT_CPU_UUID:
		count = snprintf(buf, PAGE_SIZE, "cpu_uuid");
		break;
	case VCA_AGENT_MEM_INFO:
		count = snprintf(buf, PAGE_SIZE, "memory_info");
		break;
	case VCA_AGENT_NODE_STATS:
		count = snprintf(buf, PAGE_SIZE, "node_stats");
		break;
	case VCA_AGENT_SN_INFO:
		count = snprintf(buf, PAGE_SIZE, "SN");
		break;
	case VCA_AGENT_MODE_DMA:
		strcpy(buf, "mode_dma");
		return 8;
	case VCA_AGENT_MODE_MEMCPY:
		strcpy(buf, "mode_memcpy");
		return 11;
	case VCA_AGENT_DMA_INFO: strcpy(buf, "dma_info"); return 8;
	default:
		buf[0] = 0;
		count = 0;
		break;
	}

	cdev->last_serviced_request = atomic_read(&cdev->csa_requests);
	return count;
}

static ssize_t
csa_mem_store(struct device *dev, struct device_attribute *attr,
		   const char *buf, size_t count)
{
	struct vca_csa_device *cdev = dev_get_drvdata(dev);

	if (!cdev)
		return -EINVAL;

	mutex_lock(&cdev->vca_csa_mutex);

	count = cdev->hw_ops->set_csa_mem(cdev, buf, count);

	mutex_unlock(&cdev->vca_csa_mutex);

	return count;
}
static DEVICE_ATTR_RW(csa_mem);

static ssize_t
net_config_show(struct device *dev, struct device_attribute *attr,
		 char *buf)
{
	struct vca_csa_device *cdev = dev_get_drvdata(dev);

	if (!cdev)
		return -EINVAL;

	return cdev->hw_ops->get_net_config(cdev, buf);
}

static ssize_t
net_config_store(struct device *dev, struct device_attribute *attr,
		   const char *buf, size_t count)
{
	struct vca_csa_device *cdev = dev_get_drvdata(dev);

	if (!cdev)
		return -EINVAL;

	mutex_lock(&cdev->vca_csa_mutex);

	count = cdev->hw_ops->set_net_config(cdev, buf, count);

	mutex_unlock(&cdev->vca_csa_mutex);

	return count;
}
static DEVICE_ATTR_RW(net_config);

static ssize_t
sys_config_show(struct device *dev, struct device_attribute *attr,
		 char *buf)
{
	struct vca_csa_device *cdev = dev_get_drvdata(dev);

	if (!cdev)
		return -EINVAL;

	return cdev->hw_ops->get_sys_config(cdev, buf);
}

static ssize_t
sys_config_store(struct device *dev, struct device_attribute *attr,
		   const char *buf, size_t count)
{
	struct vca_csa_device *cdev = dev_get_drvdata(dev);

	if (!cdev)
		return -EINVAL;

	mutex_lock(&cdev->vca_csa_mutex);

	count = cdev->hw_ops->set_sys_config(cdev, buf, count);

	mutex_unlock(&cdev->vca_csa_mutex);

	return count;
}
static DEVICE_ATTR_RW(sys_config);

static ssize_t
state_show(struct device *dev, struct device_attribute *attr,
		 char *buf)
{
	enum vca_lbp_states state;
	struct vca_csa_device *cdev = dev_get_drvdata(dev);

	if(!cdev)
		return -EINVAL;

	state = cdev->hw_ops->get_state(cdev);

	if (state >= VCA_SIZE)
		return -EINVAL;

	return scnprintf(buf, PAGE_SIZE, "%s\n",
		get_vca_state_string(state));
}

static ssize_t
state_store(struct device *dev, struct device_attribute *attr,
	    const char *buf, size_t count)
{
	enum vca_lbp_states state;
	struct vca_csa_device *cdev = dev_get_drvdata(dev);
	size_t len;

	if(!cdev || count == 0 || buf[count] != '\0')
		return -EINVAL;

	len = strcspn(buf, "\n\r");

	if (len == 0)
		return -EINVAL;

	state = get_vca_state_num (buf, len);

	if(state < VCA_SIZE) {
		cdev->hw_ops->set_state(cdev, state);
		return count;
	} else
		return -EINVAL;
}

static DEVICE_ATTR_RW(state);

static struct attribute *vca_csa_default_attrs[] = {
	&dev_attr_csa_mem.attr,
	&dev_attr_net_config.attr,
	&dev_attr_sys_config.attr,
	&dev_attr_state.attr,
	NULL
};

ATTRIBUTE_GROUPS(vca_csa_default);

void vca_csa_sysfs_init(struct vca_csa_device *csa_dev)
{
	csa_dev->attr_group = vca_csa_default_groups;
}

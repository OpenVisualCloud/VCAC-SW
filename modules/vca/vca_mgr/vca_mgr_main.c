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
 * Intel Visual Compute Accelerator manager (vca_mgr) driver.
 *
 */

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/suspend.h>
#include <linux/fs.h>
#include <linux/capability.h>

#include "vca_mgr_main.h"
#include "vca_mgr_ioctl.h"
#include "../plx87xx/plx_device.h"
#include "../include/vca_ioctl_impl.h"

#define POWER_BUTTON_TIMEOUT_MS 10000

static int vca_mgr_open(struct inode *inode, struct file *f)
{
	return 0;
}

static int vca_mgr_release(struct inode *inode, struct file *f)
{
	return 0;
}

struct ioctl_thread_data {
	u32 cmd;
	struct vca_mgr_device *vca_dev;
	struct vca_ioctl_desc desc;
	struct completion *wait_start;
};

static int ioctl_thread( void * argp)
{
	struct ioctl_thread_data * d = (struct ioctl_thread_data*)argp;
	struct vca_mgr_device *vca_dev = d->vca_dev;

	switch(d->cmd) {
	case VCA_RESET:
	{
		vca_dev->hw_ops->reset(vca_dev, d->desc.cpu_id);
		break;
	}
	case VCA_POWER_BUTTON:
	{
		vca_dev->hw_ops->press_power_button(vca_dev, d->desc.cpu_id,
				d->desc.hold, d->wait_start);
		break;
	}
	case VCA_SET_SMB_ID:
	{
		vca_dev->hw_ops->set_SMB_id(vca_dev, d->desc.smb_id);
		break;
	}
	}
	kfree(d);

	return 0;
}

static long vca_mgr_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	char path[64];

	struct vca_mgr_device *vca_dev = container_of(f->private_data,
		struct vca_mgr_device, misc_dev);

	void __user *argp = (void __user *)arg;
	int rc = 0;

	mutex_lock(&vca_dev->ioctrl_mutex);

	switch (cmd) {
	case VCA_RESET:
	{
		struct ioctl_thread_data * td =
			kmalloc(sizeof(struct ioctl_thread_data), GFP_KERNEL);
		if (!td) {
			rc = -ENOMEM;
			goto finish;
		}
		td->wait_start = NULL;
		td->cmd = cmd;
		td->vca_dev = vca_dev;
		if (copy_from_user(&td->desc, argp, sizeof(td->desc))) {
			kfree(td);
			rc = -EFAULT;
			goto finish;
		}
		snprintf(path, sizeof(path), "vca_ioctl_thread%d\n", td->desc.cpu_id);
		if (IS_ERR(kthread_run(ioctl_thread, td, path))) {
			kfree(td);
			rc = -ECHILD;
			goto finish;
		}
		break;
	}
	case VCA_POWER_BUTTON:
	{
		struct ioctl_thread_data * td =
			kmalloc(sizeof(struct ioctl_thread_data), GFP_KERNEL);
		if (!td) {
			rc = -ENOMEM;
			goto finish;
		}
		init_completion(&vca_dev->wait_start_compl);
		td->wait_start = &vca_dev->wait_start_compl;
		td->cmd = cmd;
		td->vca_dev = vca_dev;
		if (copy_from_user(&td->desc, argp, sizeof(td->desc))) {
			kfree(td);
			rc = -EFAULT;
			goto finish;
		}
		rc= vca_dev->hw_ops->check_power_button( vca_dev, td->desc.cpu_id);
		if( rc) {
			if( rc> 0) rc= -EBUSY;
			kfree(td);
			goto finish;
		}
		snprintf(path, sizeof(path), "vca_ioctl_thread%d\n", td->desc.cpu_id);
		if (IS_ERR(kthread_run(ioctl_thread, td, path))) {
			kfree(td);
			rc = -ECHILD;
			goto finish;
		}
		if (wait_for_completion_interruptible_timeout(
				&vca_dev->wait_start_compl,
				msecs_to_jiffies(POWER_BUTTON_TIMEOUT_MS)) <= 0) {
			rc = -EBUSY;
			goto finish;
		}
		break;
	}
	case VCA_CHECK_POWER_BUTTON:
	{
		struct vca_ioctl_desc desc;
		int err = 0;
		if (copy_from_user(&desc, argp, sizeof(desc))) {
			rc = -EFAULT;
			goto finish;
		}
		err = vca_dev->hw_ops->check_power_button(vca_dev, desc.cpu_id);
		desc.ret = err > 0;
		if (err < 0) {
			rc = err;
			goto finish;
		}
		if (copy_to_user(argp, &desc, sizeof(desc))) {
			rc = -EFAULT;
			goto finish;
		}
		break;
	}
	case VCA_READ_CARD_TYPE:
	{
		enum vca_card_type type;
		type = vca_dev->hw_ops->get_card_type(vca_dev);
		if (copy_to_user(argp, &type, sizeof(type))) {
			rc = -EFAULT;
			goto finish;
		}
		break;
	}
	case VCA_READ_CPU_NUM:
	{
		u32 cpu_num;
		cpu_num = vca_dev->hw_ops->get_cpu_num(vca_dev);
		if (copy_to_user(argp, &cpu_num, sizeof(cpu_num))) {
			rc = -EFAULT;
			goto finish;
		}
		break;
	}
	case VCA_SET_SMB_ID:
	{
		struct ioctl_thread_data * td =
			kmalloc(sizeof(struct ioctl_thread_data), GFP_KERNEL);
		if (!td) {
			rc = -ENOMEM;
			goto finish;
		}
		td->wait_start = NULL;
		td->cmd = cmd;
		td->vca_dev = vca_dev;
		if (copy_from_user(&td->desc.smb_id, argp, sizeof(td->desc.smb_id))) {
			kfree(td);
			rc = -EFAULT;
			goto finish;
		}
		snprintf(path, sizeof(path), "vca_ioctl_set_smb_thread%p\n", td->vca_dev);
		if (IS_ERR(kthread_run(ioctl_thread, td, path))) {
			kfree(td);
			rc = -ECHILD;
			goto finish;
		}
		break;
	}
	case VCA_UPDATE_EEPROM:
	{
		size_t total_size;
		struct vca_eeprom_desc *usr_desc = (struct vca_eeprom_desc*)argp;
		struct vca_eeprom_desc *desc;

		if (!capable(CAP_SYS_ADMIN)) {
			rc = -EPERM;
			goto finish;
		}

		desc = vca_get_ioctl_data(usr_desc, VCA_MAX_EEPROM_BUF_SIZE, VCA_MAX_EEPROM_BUF_SIZE, &total_size);
		if (IS_ERR(desc)) {
			rc = PTR_ERR(desc);
			goto finish;
		}

		desc->ret = vca_dev->hw_ops->update_eeprom(vca_dev, desc->buf,
								desc->buf_size);

		/* not returning anything in data in buffer */
		desc->buf_size = 0;

		if (copy_to_user(usr_desc, desc, sizeof(*desc)))
			rc = -EFAULT;

		kfree(desc);
		break;
	}
	case VCA_READ_MODULES_BUILD:
	{
		const char modules_build_text[] = BUILD_NUMBER " build on " BUILD_ONDATE;
		struct vca_ioctl_buffer *pmodules_build = (struct vca_ioctl_buffer *)argp;
		if (copy_to_user(pmodules_build->buf, modules_build_text, min(sizeof(modules_build_text), sizeof(pmodules_build->buf)))) {
			rc = -EFAULT;
			goto finish;
		}
		break;
	}
	case VCA_READ_EEPROM_CRC:
	{
		__u32 crc = vca_dev->hw_ops->get_eeprom_crc(vca_dev);
		if (copy_to_user(argp, &crc, sizeof(crc))) {
			rc = -EFAULT;
			goto finish;
		}
		break;
	}
	case VCA_ENABLE_GOLD_BIOS:
	{
		struct vca_ioctl_desc desc;
		if (copy_from_user(&desc, argp, sizeof(desc))) {
			rc = -EFAULT;
			goto finish;
		}
		vca_dev->hw_ops->enable_gold_bios(vca_dev, desc.cpu_id);
		break;
	}
	case VCA_DISABLE_GOLD_BIOS:
	{
		struct vca_ioctl_desc desc;
		if (copy_from_user(&desc, argp, sizeof(desc))) {
			rc = -EFAULT;
			goto finish;
		}
		vca_dev->hw_ops->disable_gold_bios(vca_dev, desc.cpu_id);
		break;
	}
	default:
		rc = -EINVAL;
		break;
	}

finish:
	mutex_unlock(&vca_dev->ioctrl_mutex);
	return rc;
}

static const struct file_operations vca_mgr_fops = {
	.open = vca_mgr_open,
	.release = vca_mgr_release,
	.unlocked_ioctl = vca_mgr_ioctl,
	.owner = THIS_MODULE,
};

static int vca_mgr_driver_probe(struct vca_mgr_device *vca_dev)
{
	int ret;
	struct miscdevice *mdev = &vca_dev->misc_dev;
	unsigned char minor = 64; // see miscdevice.h
	dev_info(&vca_dev->dev, "buildinfo: build no "
		BUILD_NUMBER ", built on " BUILD_ONDATE ".\n");

	init_completion(&vca_dev->cpu_threads_compl);
	init_completion(&vca_dev->wait_start_compl);
	mutex_init(&vca_dev->ioctrl_mutex);

	mdev->minor = MISC_DYNAMIC_MINOR;
	snprintf(vca_dev->misc_dev_name, sizeof(vca_dev->misc_dev_name),
		"vca_mgr%d", vca_dev->hw_ops->get_card_id(vca_dev));
	mdev->name = vca_dev->misc_dev_name;
	mdev->fops = &vca_mgr_fops;
	ret = misc_register(mdev);
	if(!ret)
		return 0;
	do {
		mdev->minor = minor;
		ret = misc_register(mdev);
		if( !ret)
			return 0;
	} while(MISC_DYNAMIC_MINOR != ++minor);
	dev_err(&vca_dev->dev, "%s failed misc_register %d\n", __func__, ret);
	return ret;
}

static void vca_mgr_driver_remove(struct vca_mgr_device *vca_dev)
{
	complete_all(&vca_dev->cpu_threads_compl);
	complete_all(&vca_dev->wait_start_compl);
	misc_deregister(&vca_dev->misc_dev);
}

static struct vca_mgr_device_id id_table[] = {
	{ VCA_MGR_DEV_VCA, VCA_MGR_DEV_ANY_ID },
	{ 0 },
};

static struct vca_mgr_driver vca_mgr_driver = {
	.driver.name =	KBUILD_MODNAME,
	.driver.owner =	THIS_MODULE,
	.id_table = id_table,
	.probe = vca_mgr_driver_probe,
	.remove = vca_mgr_driver_remove,
};

module_vca_mgr_driver(vca_mgr_driver);

MODULE_DEVICE_TABLE(mbus, id_table);
MODULE_AUTHOR("Intel Corporation");
MODULE_DESCRIPTION("Intel(R) VCA manager(vca_mgr) driver");
MODULE_LICENSE("GPL v2");

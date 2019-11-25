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

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/version.h>
#include <linux/suspend.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,12,0)
#include <linux/uaccess.h>
#else
#include <asm/uaccess.h>
#endif 
#include "../vca_device/vca_device.h"
#include "vca_csm_main.h"
#include "vca_csm_ioctl.h"
#include "../include/vca_ioctl_impl.h"

/*
 * Head entry for the doubly linked vca_csm_device list
 */
static LIST_HEAD(vca_csm_list);
static DEFINE_MUTEX(vca_csm_mtx);

static const char vca_csm_driver_name[] = "vca";

/* VCA_CSM ID allocator */
static struct ida g_vca_csm_ida;
/* Class of VCA devices for sysfs accessibility. */
static struct class *g_vca_csm_class;
/* Base device node number for VCA devices */
static dev_t g_vca_csm_devno;

/**
 * vca_csm_start - Start the VCA.
 * @cdev: pointer to vca_csm_device instance
 *
 * This function initiates VCA for boot.
 * RETURNS: An appropriate -ERRNO error value on error, or zero for success.
 */
int vca_csm_start(struct vca_csm_device *cdev)
{
	int rc = 0;

	mutex_lock(&cdev->vca_csm_mutex);
	rc = cdev->hw_ops->start(cdev, cdev->index);
	mutex_unlock(&cdev->vca_csm_mutex);
	return rc;
}

/**
 * vca_csm_stop - Stop the VCA.
 * @cdev: pointer to vca_csm_device instance
 *
 * RETURNS: None.
 */
void vca_csm_stop(struct vca_csm_device *cdev, bool force)
{
	mutex_lock(&cdev->vca_csm_mutex);
	cdev->hw_ops->stop(cdev, force);
	mutex_unlock(&cdev->vca_csm_mutex);
}

/**
 * vca_agent_command_ioct - handle VCA agent IOCTL
 *
 * @cdev: pointer to vca_csm_device instance
 * @argp: IOCTL argument
 *
 * RETURNS: 0 in case of success or negative error code otherwise
 */
static int vca_agent_command_ioctl(struct vca_csm_device *cdev, void __user *argp)
{
	struct vca_csm_ioctl_agent_cmd __user *data_usr = argp;
	size_t total_size;
	struct vca_csm_ioctl_agent_cmd* desc = vca_get_ioctl_data(data_usr, VCA_MAX_AGENT_BUF_SIZE, sizeof(char), &total_size);
	size_t out_size;
	int rc = 0;

	if (IS_ERR(desc))
		return PTR_ERR(desc);

	desc->ret = cdev->hw_ops->vca_csm_agent_command(cdev, desc->buf, &desc->buf_size);

	out_size = desc->buf_size + sizeof(*data_usr);
	if (out_size > total_size)
		rc = -E2BIG;

	if (!rc && copy_to_user(argp, desc, out_size))
		rc = -EFAULT;

	kfree(desc);
	return rc;
}

static long vca_cpu_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	int rc = 0;
	void __user *argp = (void __user *)arg;
	struct vca_csm_device *cdev =  f->private_data;
	if (!cdev) {
		rc = -ENODEV;
		goto finish;
	}

	switch (cmd) {
	case VCA_AGENT_COMMAND:
	{
		rc = vca_agent_command_ioctl(cdev, argp);
		break;
	}
	case LBP_BOOT_VIA_PXE:
	case LBP_BOOT_BLKDISK: {
		enum vca_lbp_retval ret;
		if (cmd == LBP_BOOT_VIA_PXE) ret = cdev->hw_ops->lbp_boot_via_pxe(cdev);
		else ret = cdev->hw_ops->lbp_boot_blkdisk(cdev);
		if (copy_to_user(argp, &ret, sizeof(ret))) {
					rc = -EFAULT;
				}

		break;
	}
	case LBP_BOOT_RAMDISK:
	case LBP_FLASH_BIOS:
	case LBP_UPDATE_MAC_ADDR:
	case LBP_SET_SERIAL_NR:
	case LBP_CLEAR_SMB_EVENT_LOG:
	{
		struct vca_csm_ioctl_mem_desc desc;
		enum vca_lbp_retval ret;
		if (copy_from_user(&desc, argp, sizeof(desc))) {
			rc = -EFAULT;
			break;
		}
		switch(cmd) {
			case LBP_BOOT_RAMDISK:
				ret = cdev->hw_ops->lbp_boot_ramdisk(cdev,
					desc.mem, desc.mem_info);
				break;
			case LBP_FLASH_BIOS:
				ret = cdev->hw_ops->lbp_flash_bios(cdev,
					desc.mem, desc.mem_info);
				break;
			case LBP_CLEAR_SMB_EVENT_LOG:
				ret = cdev->hw_ops->lbp_clear_smb_event_log(cdev,
					desc.mem, desc.mem_info);
				break;
			case LBP_SET_SERIAL_NR:
				ret = cdev->hw_ops->lbp_set_serial_number(cdev,
					desc.mem, desc.mem_info);
				break;
			case LBP_UPDATE_MAC_ADDR:
				ret = cdev->hw_ops->lbp_update_mac_addr(cdev,
					desc.mem, desc.mem_info);
				break;
		}
		if (copy_to_user(
				&((struct vca_csm_ioctl_mem_desc __user *)argp)->ret,
				&ret,
				sizeof(ret))) {
			rc = -EFAULT;
		}
		break;
	}
	case LBP_HANDSHAKE:
	{
		enum vca_lbp_retval ret;
		ret = cdev->hw_ops->lbp_handshake(cdev);
		if (copy_to_user(
				(enum vca_lbp_retval __user *)argp,
				&ret,
				sizeof(ret))) {
			rc = -EFAULT;
		}
		break;
	}
	case LBP_BOOT_FROM_USB:
	{
		enum vca_lbp_retval ret;
		ret = cdev->hw_ops->lbp_boot_from_usb(cdev);
		if (copy_to_user(
				(enum vca_lbp_retval __user *)argp,
				&ret,
				sizeof(ret))) {
					rc = -EFAULT;
		}
		break;
	}
	case LBP_FLASH_FIRMWARE:
	{
		enum vca_lbp_retval ret = cdev->hw_ops->lbp_flash_firmware(cdev);
		if (copy_to_user(
				(enum vca_lbp_retval __user *)argp,
				&ret,
				sizeof(ret))) {
			rc = -EFAULT;
		}
		break;
	}
	case LBP_SET_PARAMETER:
	{
		enum vca_lbp_retval ret;
		struct vca_csm_ioctl_param_desc desc;
		if (copy_from_user(&desc, argp, sizeof(desc))) {
			rc = -EFAULT;
			break;
		}
		ret = cdev->hw_ops->lbp_set_param(cdev, desc.param, desc.value);
		if (copy_to_user(
				&((struct vca_csm_ioctl_param_desc __user *)argp)->ret,
				&ret,
				sizeof(ret))) {
			rc = -EFAULT;
		}
		break;
	}
	case CSM_START:
	{
		int ret = vca_csm_start(cdev);
		if (copy_to_user(
				(int __user *)argp,
				&ret,
				sizeof(ret))) {
			rc = -EFAULT;
		}
		break;
	}
	case CSM_STOP:
	{
		vca_csm_stop(cdev, true);
		break;
	}
	case LBP_GET_MAC_ADDR:
	{
		struct vca_csm_ioctl_mac_addr_desc desc;
		desc.ret = cdev->hw_ops->lbp_get_mac_addr(cdev, desc.mac_addr);
		if (copy_to_user(
			(struct vca_csm_ioctl_mac_addr_desc __user *)argp,
			&desc,
			sizeof(desc))) {
				rc = -EFAULT;
		}
		break;
	}
	case VCA_GET_MEM_INFO:
	{
		int ret = cdev->hw_ops->get_meminfo(cdev);
		if (copy_to_user(
			(int __user *)argp,
			&ret,
			sizeof(ret))) {
			rc = -EFAULT;
		}
		break;
	}
	case LBP_SET_TIME:
	{
		enum vca_lbp_retval ret = cdev->hw_ops->lbp_set_time(cdev);
		if (copy_to_user(
				(enum vca_lbp_retval __user *)argp,
				&ret,
				sizeof(ret))) {
			rc = -EFAULT;
		}
		break;
	}
	case LBP_SET_BIOS_PARAM:
	case LBP_GET_BIOS_PARAM:
	{
		struct vca_csm_ioctl_bios_param_desc desc;
		if (copy_from_user(&desc, argp, sizeof(desc))) {
			rc = -EFAULT;
			break;
		}
		switch(cmd) {
		case LBP_SET_BIOS_PARAM:
			desc.ret = cdev->hw_ops->lbp_set_bios_param(cdev,
				desc.param, desc.value.value);
			break;
		case LBP_GET_BIOS_PARAM:
			desc.ret = cdev->hw_ops->lbp_get_bios_param(cdev,
				desc.param, &desc.value.value);
			break;
		}
		if (copy_to_user(
			(struct vca_csm_ioctl_bios_param_desc __user *)argp,
			&desc,
			sizeof(desc))) {
				rc = -EFAULT;
		}
		break;
	}
	case VCA_READ_EEPROM_CRC:
	{
		__u32 eeprom_crc = 0;
		eeprom_crc = cdev->hw_ops->get_eeprom_crc(cdev);
		if (copy_to_user(argp, &eeprom_crc, sizeof(eeprom_crc)))
			rc = -EFAULT;
		break;
	}
	case VCA_WRITE_SPAD_POWER_BUTTON:
	{
		cdev->hw_ops->set_link_down_flag(cdev);
		break;
	}case VCA_WRITE_SPAD_POWER_OFF:
	{
		cdev->hw_ops->set_power_off_flag(cdev);
		break;
	}
	default:
		dev_err(cdev->dev.parent, "Invalid ioctl command received\n");
		rc = -EFAULT;
	};

finish:
	return rc;
}

static int vca_cpu_open(struct inode *inode, struct file *f)
{
	int minor = iminor(inode);
	struct vca_csm_device * cdev;

	mutex_lock(&vca_csm_mtx);
	list_for_each_entry(cdev, &vca_csm_list, list) {
		if (cdev->index == minor)
			break;
	}
	mutex_unlock(&vca_csm_mtx);

	f->private_data = cdev;

	return 0;
}
static int vca_cpu_release(struct inode *inode, struct file *f)
{
	return 0;
}

static const struct file_operations vca_cpu_fops = {
	.open = vca_cpu_open,
	.release = vca_cpu_release,
	.unlocked_ioctl = vca_cpu_ioctl,
	.owner = THIS_MODULE,
};

static int vca_csm_driver_probe(struct vca_csm_device *cdev)
{
	int rc = 0;
	u8 card_id, cpu_id;

	INIT_LIST_HEAD(&cdev->list);

	dev_info(&cdev->dev, "buildinfo: build no "
		BUILD_NUMBER ", built on " BUILD_ONDATE ".\n");

	mutex_init(&cdev->vca_csm_mutex);
	mutex_init(&cdev->vca_csm_host_mutex);

	vca_csm_sysfs_init(cdev);

	cdev->chdev = cdev_alloc();
	if (!cdev->chdev) {
		dev_err(cdev->dev.parent, "cdev_alloc failed!\n");
		goto finish;
	}
	cdev->chdev->owner = THIS_MODULE;
	cdev_init(cdev->chdev, &vca_cpu_fops);
	rc = cdev_add(cdev->chdev, MKDEV(MAJOR(g_vca_csm_devno), cdev->index), 1);
	if (rc) {
		dev_err(cdev->dev.parent, "cdev_add failed rc %d\n", rc);
		goto free_cdev;
	}

	cdev->hw_ops->get_card_and_cpu_id(cdev, &card_id, &cpu_id);

	cdev->sdev = device_create_with_groups(g_vca_csm_class, cdev->dev.parent,
		MKDEV(MAJOR(g_vca_csm_devno), cdev->index), cdev,
		cdev->attr_group, "vca%d%d", card_id, cpu_id);
	if (IS_ERR(cdev->sdev)) {
		rc = PTR_ERR(cdev->sdev);
		dev_err(cdev->dev.parent,
			"device_create_with_groups failed rc %d\n", rc);
		goto free_cdev;
	}

	mutex_lock(&vca_csm_mtx);
	list_add(&cdev->list, &vca_csm_list);
	mutex_unlock(&vca_csm_mtx);
	goto finish;

free_cdev:
	cdev_del(cdev->chdev);
finish:
	return rc;
}

static void vca_csm_driver_remove(struct vca_csm_device *cdev)
{
	mutex_lock(&vca_csm_mtx);
	list_del(&cdev->list);
	mutex_unlock(&vca_csm_mtx);

	cdev_del(cdev->chdev);
	vca_csm_stop(cdev, false);
	device_destroy(g_vca_csm_class, MKDEV(MAJOR(g_vca_csm_devno), cdev->index));
}

static struct vca_csm_device_id id_table[] = {
	{ VCA_CSM_DEV_VCA, VCA_CSM_DEV_ANY_ID },
	{ 0 },
};

static struct vca_csm_driver vca_csm_driver = {
	.driver.name =  KBUILD_MODNAME,
	.driver.owner = THIS_MODULE,
	.id_table = id_table,
	.probe = vca_csm_driver_probe,
	.remove = vca_csm_driver_remove,
};

static int __init vca_csm_init(void)
{
	int ret;
	ret = alloc_chrdev_region(&g_vca_csm_devno, 0, VCA_MAX_NUM_CPUS,
				  vca_csm_driver_name);
	if (ret) {
		pr_err("alloc_chrdev_region failed ret %d\n", ret);
		goto finish;
	}

	g_vca_csm_class = class_create(THIS_MODULE, vca_csm_driver_name);
	if (IS_ERR(g_vca_csm_class)) {
		ret = PTR_ERR(g_vca_csm_class);
		pr_err("class_create failed ret %d\n", ret);
		goto cleanup_chrdev;
	}

	ida_init(&g_vca_csm_ida);
	ret = vca_csm_register_driver(&vca_csm_driver);
	if (ret) {
		pr_err("vca_csm_register_driver failed ret %d\n", ret);
		goto ida_destroy;
	}

	return 0;
ida_destroy:
	ida_destroy(&g_vca_csm_ida);
	class_destroy(g_vca_csm_class);
cleanup_chrdev:
	unregister_chrdev_region(g_vca_csm_devno, VCA_MAX_NUM_CPUS);
finish:
	return ret;
}

static void __exit vca_csm_exit(void)
{
	vca_csm_unregister_driver(&vca_csm_driver);
	ida_destroy(&g_vca_csm_ida);
	class_destroy(g_vca_csm_class);
	unregister_chrdev_region(g_vca_csm_devno, VCA_MAX_NUM_CPUS);
}

module_init(vca_csm_init);
module_exit(vca_csm_exit);

MODULE_DEVICE_TABLE(vca_csm, id_table);
MODULE_AUTHOR("Intel Corporation");
MODULE_DESCRIPTION("Intel(R) VCA card OS state management driver");
MODULE_LICENSE("GPL v2");

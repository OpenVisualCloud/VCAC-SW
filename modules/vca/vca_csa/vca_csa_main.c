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
 * Intel Visual Compute Accelerator Card State Agent (vca_csa) driver.
 *
 */

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/suspend.h>
#include <linux/fs.h>
#include "../vca_device/vca_device.h"
#include "vca_csa_main.h"
#include "../plx87xx/plx_device.h"

static const char vca_csa_driver_name[] = "vca";

/* VCA_CSM ID allocator */
static struct ida g_vca_csa_ida;
/* Class of VCA devices for sysfs accessibility. */
static struct class *g_vca_csa_class;
/* Base device node number for VCA devices */
static dev_t g_vca_csa_devno;

static int vca_csa_open(struct inode *inode, struct file *f)
{
	return 0;
}

static int vca_csa_release(struct inode *inode, struct file *f)
{
	return 0;
}

static const struct file_operations vca_csa_fops = {
	.open = vca_csa_open,
	.release = vca_csa_release,
	.owner = THIS_MODULE,
};

static irqreturn_t _vca_csa_csa_mem_intr_handler(int irq, void *data)
{
	struct vca_csa_device *cdev = data;
	dev_dbg(&cdev->dev, "Handling csa_mem interrupt!\n");
	atomic_inc(&cdev->csa_requests);
	wake_up_all(&cdev->csa_request_wq);
	return IRQ_HANDLED;
}

static int vca_csa_driver_probe(struct vca_csa_device *cdev)
{
	int rc = 0;
	dev_info(&cdev->dev, "buildinfo: build no "
		BUILD_NUMBER ", built on " BUILD_ONDATE ".\n");

	mutex_init(&cdev->vca_csa_mutex);

	cdev->last_serviced_request = 0;
	init_waitqueue_head(&cdev->csa_request_wq);
	atomic_set(&cdev->csa_requests, 0);

	vca_csa_sysfs_init(cdev);

	cdev->sdev = device_create_with_groups(g_vca_csa_class, cdev->dev.parent,
		MKDEV(MAJOR(g_vca_csa_devno), cdev->index), cdev,
		cdev->attr_group, "vca");
	if (IS_ERR(cdev->sdev)) {
		rc = PTR_ERR(cdev->sdev);
		dev_err(cdev->dev.parent,
			"device_create_with_groups failed rc %d\n", rc);
		goto exit;
	}

	cdev->hw_ops->set_os_type(cdev, VCA_OS_TYPE_LINUX);

	/* setup handling irq */
	cdev->h2c_csa_mem_db = cdev->hw_ops->next_db(cdev);
	cdev->hw_ops->set_h2c_csa_mem_db(cdev, cdev->h2c_csa_mem_db);
	cdev->csa_mem_db_cookie = cdev->hw_ops->request_irq(cdev,
		_vca_csa_csa_mem_intr_handler, "vca_csa_mem_irq",
		cdev, cdev->h2c_csa_mem_db);
	if (IS_ERR(cdev->csa_mem_db_cookie)) {
		rc = PTR_ERR(cdev->csa_mem_db_cookie);
		dev_dbg(&cdev->dev, "request irq conf failed\n");
		goto exit;
	}
exit:
	return rc;
}

static void vca_csa_driver_remove(struct vca_csa_device *cdev)
{
	cdev->hw_ops->free_irq(cdev, cdev->csa_mem_db_cookie, cdev);
}

static struct vca_csa_device_id id_table[] = {
	{ VCA_CSA_DEV_VCA, VCA_CSA_DEV_ANY_ID },
	{ 0 },
};

static struct vca_csa_driver vca_csa_driver = {
	.driver.name =	KBUILD_MODNAME,
	.driver.owner =	THIS_MODULE,
	.id_table = id_table,
	.probe = vca_csa_driver_probe,
	.remove = vca_csa_driver_remove,
};

static int __init vca_csa_init(void)
{
	int ret;

	ret = alloc_chrdev_region(&g_vca_csa_devno, 0, VCA_MAX_NUM_CPUS,
				  vca_csa_driver_name);
	if (ret) {
		pr_err("alloc_chrdev_region failed ret %d\n", ret);
		goto finish;
	}

	g_vca_csa_class = class_create(THIS_MODULE, vca_csa_driver_name);
	if (IS_ERR(g_vca_csa_class)) {
		ret = PTR_ERR(g_vca_csa_class);
		pr_err("class_create failed ret %d\n", ret);
		goto cleanup_chrdev;
	}

	ida_init(&g_vca_csa_ida);
	ret = vca_csa_register_driver(&vca_csa_driver);
	if (ret) {
		pr_err("alloc_chrdev_region failed ret %d\n", ret);
		goto ida_destroy;
	}
	return 0;
ida_destroy:
	ida_destroy(&g_vca_csa_ida);
	class_destroy(g_vca_csa_class);
cleanup_chrdev:
	unregister_chrdev_region(g_vca_csa_devno, VCA_MAX_NUM_CPUS);
finish:
	return ret;
}

static void __exit vca_csa_exit(void)
{
	vca_csa_unregister_driver(&vca_csa_driver);
	ida_destroy(&g_vca_csa_ida);
	class_destroy(g_vca_csa_class);
	unregister_chrdev_region(g_vca_csa_devno, VCA_MAX_NUM_CPUS);
}

module_init(vca_csa_init);
module_exit(vca_csa_exit);

MODULE_DEVICE_TABLE(vca_csa, id_table);
MODULE_AUTHOR("Intel Corporation");
MODULE_DESCRIPTION("Intel(R) VCA Card State Agent(vca_csa) driver");
MODULE_LICENSE("GPL v2");

/*
 * Intel VCA Software Stack (VCASS)
 *
 * Copyright(c) 2016-2019 Intel Corporation.
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
 * Intel VCA PXE support code
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/debugfs.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/types.h>
#include <linux/jiffies.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>

#include "vcapxe.h"
#include "vcapxe_ioctl.h"
#include "vcapxe_netdev.h"

#include "plx_hw_ops_pxe.h"

MODULE_LICENSE("GPL");

static const int shared_size = ((sizeof(struct vcapxe_shared) + (PAGE_SIZE - 1)) / PAGE_SIZE) * PAGE_SIZE;

static long vcapxe_ioctl (struct file *file, unsigned int cmd,
			unsigned long parm)
{
	int ret = 0;
	void __user *argp = (void __user *)parm;
	__u32 to_write;
	struct vcapxe_device *dev =  container_of(file->private_data,
		struct vcapxe_device, mdev);

	mutex_lock(&dev->lock);
	switch (cmd) {
	case VCA_PXE_ENABLE:
		ret = vcapxe_enable_netdev(dev);
		break;
	case VCA_PXE_DISABLE:
		ret = vcapxe_disable_netdev(dev);
		break;
	case VCA_PXE_QUERY:
		to_write = vcapxe_query_netdev(dev);
		if (copy_to_user(argp, &to_write, sizeof(to_write))) {
			ret = -EFAULT;
		} else ret = 0;
		break;
	default:
		ret = -ENOTTY;
	}
	mutex_unlock(&dev->lock);

	return ret;
}

static const struct file_operations ctl_fops = {
	.open			= nonseekable_open,
	.unlocked_ioctl	= vcapxe_ioctl,
	.compat_ioctl	= vcapxe_ioctl,
	.owner			= THIS_MODULE,
	.llseek			= noop_llseek,
};

struct vcapxe_device *vcapxe_register(struct plx_device *xdev, struct device *parent, struct plx_pxe_hw_ops *hw_ops, int card_id, int cpu_id)
{
	struct vcapxe_device *dev;
	int ret;

	dev = kzalloc(sizeof(struct vcapxe_device), GFP_KERNEL);

	if (dev == NULL)
	{
		pr_err("memory allocation for PXE device for card %d cpu %d failed\n", card_id, cpu_id);
		return NULL;
	}

	snprintf(dev->mdev_name, sizeof(dev->mdev_name), "vcapxe%i%i", card_id, cpu_id);
	mutex_init(&dev->lock);
	spin_lock_init(&dev->state_lock);
	dev->hw_ops = hw_ops;

	dev->mdev.minor = MISC_DYNAMIC_MINOR;
	dev->mdev.name = dev->mdev_name;
	dev->mdev.fops = &ctl_fops;
	dev->mdev.parent = parent;
	dev->xdev = xdev;

	ret = misc_register(&dev->mdev);
	// Workaround to run 8 cards (missing misc devices)
	if (ret) {
		unsigned char minor = 64; // see miscdevice.h
		do {
			dev->mdev.minor = minor;
			ret = misc_register(&dev->mdev);
			if( !ret)
				return dev;
		} while(MISC_DYNAMIC_MINOR != ++minor);
		pr_err( "%s failed misc_register %d\n", __func__, ret);
		goto err;
	}

	pr_info("dev for card/cpu %d %d is %llx\n", card_id, cpu_id, (u64) dev);

	return dev;

err:
	kfree(dev);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL(vcapxe_register);

void vcapxe_go(struct vcapxe_device *pxe) {
	u64 dp_ph;
	struct vcapxe_shared *shared;

	struct plx_device *xdev = pxe->xdev;

	/* read dev_page address */
	dp_ph = pxe->hw_ops->read_spad(xdev, PLX_LBP_SPAD_DATA_HIGH);
	dp_ph <<= 32ULL;
	dp_ph += pxe->hw_ops->read_spad(xdev, PLX_LBP_SPAD_DATA_LOW);

	shared = (struct vcapxe_shared *) pxe->hw_ops->ioremap(xdev, dp_ph, shared_size);

	shared->doorbell = pxe->hw_ops->next_db(xdev);
	pxe->doorbell_irq = pxe->hw_ops->request_threaded_irq(xdev, vcapxe_doorbell_irq, NULL,
				"VCAPXE", netdev_priv(pxe->netdev), shared->doorbell);

	vcapxe_start_netdev(pxe, shared);
}
EXPORT_SYMBOL(vcapxe_go);

void vcapxe_finalize(struct vcapxe_device *pxe, void* shared) {
	pxe->hw_ops->free_irq(pxe->xdev, pxe->doorbell_irq, netdev_priv(pxe->netdev));
	pxe->hw_ops->iounmap(pxe->xdev, shared);
	vcapxe_stop_netdev(pxe);
}

void vcapxe_force_detach(struct vcapxe_device *pxe)
{
	vcapxe_teardown_netdev(pxe);
}
EXPORT_SYMBOL(vcapxe_force_detach);

int vcapxe_is_ready_to_boot(struct vcapxe_device *pxe)
{
	/* we must be ready to "preempt" a unsuccesful PXE boot, therefore
	 * we consider only 'inactive' to be wrong */
	return pxe->state != VCAPXE_STATE_INACTIVE;
}
EXPORT_SYMBOL(vcapxe_is_ready_to_boot);

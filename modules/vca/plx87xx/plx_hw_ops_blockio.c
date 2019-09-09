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
 * Intel PLX87XX VCA PCIe driver
 *
 */

#include "plx_device.h"
#include "plx_hw.h"
#include "plx_hw_ops_blockio.h"
#include "../vop/vop_main.h"

static inline struct plx_device *dev_to_xdev(struct device* dev)
{
	return (struct plx_device *)dev_get_drvdata(dev);
}

static struct vca_irq *
__plx_request_irq(struct device *dev,
		  irqreturn_t (*func)(int irq, void *data),
		  const char *name, void *data, int intr_src)
{
	struct plx_device *xdev = dev_to_xdev(dev);

	dev_dbg(&xdev->pdev->dev, "%s intr_src 0x%x, name %s\n",
		__func__, intr_src, name);

	return plx_request_threaded_irq(xdev, func, NULL, name, data, intr_src);
}

static void __plx_free_irq(struct device *dev,
			   struct vca_irq *cookie, void *data)
{
	struct plx_device *xdev = dev_to_xdev(dev);

	return plx_free_irq(xdev, cookie, data);
}

static void __plx_ack_interrupt(struct device *dev, int num)
{
}

static int __plx_next_db(struct device *dev)
{
	struct plx_device *xdev = dev_to_xdev(dev);

	return plx_next_db(xdev);
}

static void *__plx_get_dp(struct device *dev)
{
	struct plx_device *xdev = dev_to_xdev(dev);

	if (xdev->link_side)
		return xdev->rdp;
	else
		return xdev->dp;
}

static void __plx_send_intr(struct device *dev, int db)
{
	struct plx_device *xdev = dev_to_xdev(dev);

	plx_send_intr(xdev, db);
}

static void __iomem *__plx_ioremap(struct device *dev,
				   dma_addr_t pa, size_t len)
{
	struct plx_device *xdev = dev_to_xdev(dev);

	return plx_ioremap(xdev, pa, len);
}

static void __plx_iounmap(struct device *dev, void __iomem *va)
{
	struct plx_device *xdev = dev_to_xdev(dev);

	plx_iounmap(xdev, va);
}

dma_addr_t __bar_va_to_pa(struct device *dev, void __iomem *va)
{
	struct plx_device *xdev = dev_to_xdev(dev);

	return (dma_addr_t)(xdev->aper.pa + (va - xdev->aper.va));
}

static void __plx_set_net_dev_state(struct device *dev, bool state)
{
	struct plx_device *xdev = dev_to_xdev(dev);
	u32 val = state ? PLX_LBP_i7_NET_DEV_UP : PLX_LBP_i7_NET_DEV_DOWN;

	plx_write_spad(xdev, PLX_LBP_SPAD_i7_READY, val);
}

static void _plx_vop_get_card_and_cpu_id(struct device *dev,
 u8 *out_card_id, u8 *out_cpu_id)
{
	struct plx_device *xdev = dev_to_xdev(dev);
	*out_card_id = xdev->card_id;
	*out_cpu_id = plx_identify_cpu_id(xdev->pdev);
}

static bool __is_link_side(struct device *dev)
{
	struct plx_device *xdev = dev_to_xdev(dev);

	return xdev->link_side;
}

static int __set_dp_addr(struct device *dev)
{
	struct plx_device *xdev = dev_to_xdev(dev);
	struct vca_bootparam *bootparam = xdev->rdp;

	BUG_ON(!xdev->link_side);

	/* TODO: add function in plx_hw for this? */
	bootparam->blockio_dp = xdev->blockio.dp_da;
	wmb();
	ioread8((void *)&bootparam->blockio_dp);

	plx_send_intr(xdev, bootparam->blockio_ftb_db);
	dev_info(&xdev->pdev->dev, "%s DB 0x%x, dp 0x%llx\n",
			__func__, bootparam->blockio_ftb_db, bootparam->blockio_dp);

	return 0;
}

static u64 __get_dp_addr(struct device *dev, bool reset)
{
	struct plx_device *xdev = dev_to_xdev(dev);
	struct vca_bootparam *bootparam = xdev->dp;
	u64 dp = bootparam->blockio_dp;
	BUG_ON(xdev->link_side);

	if (reset)
		bootparam->blockio_dp = 0;

	return dp;
}

static u32 __get_f2b_db(struct device *dev)
{
	struct plx_device *xdev = dev_to_xdev(dev);
	if (!xdev->link_side)
		return xdev->blockio.ftb_db;
	else
		return xdev->rdp->blockio_ftb_db;
}

static void __stop_network_traffic(struct device *dev)
{
	struct plx_device *xdev = dev_to_xdev(dev);
	if(xdev->vpdev) {
		vop_stop_network_traffic(xdev->vpdev);
	}
}

struct plx_blockio_hw_ops blockio_hw_ops  = {
	.request_irq = __plx_request_irq,
	.free_irq = __plx_free_irq,
	.ack_interrupt = __plx_ack_interrupt,
	.next_db = __plx_next_db,
	.get_dp = __plx_get_dp,
	.send_intr = __plx_send_intr,
	.ioremap = __plx_ioremap,
	.iounmap = __plx_iounmap,
	.set_net_dev_state = __plx_set_net_dev_state,
	.get_card_and_cpu_id =  _plx_vop_get_card_and_cpu_id,
	.is_link_side = __is_link_side,
	.set_dev_page = __set_dp_addr,
	.get_dp_addr = __get_dp_addr,
	.get_f2b_db = __get_f2b_db,
	.bar_va_to_pa = __bar_va_to_pa,
	.stop_network_traffic = __stop_network_traffic,
};

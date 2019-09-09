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

static inline struct plx_device *vpdev_to_xdev(
	struct vop_device *vpdev)
{
	return dev_get_drvdata(vpdev->dev.parent);
}

static struct vca_irq *
__plx_request_irq(struct vop_device *vpdev,
		  irqreturn_t (*func)(int irq, void *data),
		  const char *name, void *data, int intr_src)
{
	struct plx_device *xdev = vpdev_to_xdev(vpdev);

	dev_dbg(&xdev->pdev->dev, "%s intr_src 0x%x, name %s\n",
		__func__, intr_src, name);

	return plx_request_threaded_irq(xdev, func, NULL, name, data, intr_src);
}

static void __plx_free_irq(struct vop_device *vpdev,
			   struct vca_irq *cookie, void *data)
{
	struct plx_device *xdev = vpdev_to_xdev(vpdev);

	return plx_free_irq(xdev, cookie, data);
}

static void __plx_ack_interrupt(struct vop_device *vpdev, int num)
{
}

static int __plx_next_db(struct vop_device *vpdev)
{
	struct plx_device *xdev = vpdev_to_xdev(vpdev);

	return plx_next_db(xdev);
}

static void *__plx_get_dp(struct vop_device *vpdev)
{
	struct plx_device *xdev = vpdev_to_xdev(vpdev);

	if (xdev->link_side)
		return xdev->rdp;
	else
		return xdev->dp;
}

static void __plx_send_intr(struct vop_device *vpdev, int db)
{
	struct plx_device *xdev = vpdev_to_xdev(vpdev);

	plx_send_intr(xdev, db);
}

static void __iomem *__plx_ioremap(struct vop_device *vpdev,
				   dma_addr_t pa, size_t len)
{
	struct plx_device *xdev = vpdev_to_xdev(vpdev);

	return plx_ioremap(xdev, pa, len);
}

static void __plx_iounmap(struct vop_device *vpdev, void __iomem *va)
{
	struct plx_device *xdev = vpdev_to_xdev(vpdev);

	plx_iounmap(xdev, va);
}

static void __plx_set_net_dev_state(struct vop_device *vpdev, bool state)
{
	struct plx_device *xdev = vpdev_to_xdev(vpdev);
	u32 val = state ? PLX_LBP_i7_NET_DEV_UP : PLX_LBP_i7_NET_DEV_DOWN;

	plx_write_spad(xdev, PLX_LBP_SPAD_i7_READY, val);
}

static void _plx_vop_get_card_and_cpu_id(struct vop_device *vpdev,
 u8 *out_card_id, u8 *out_cpu_id)
{
	struct plx_device *xdev = vpdev_to_xdev(vpdev);
	*out_card_id = xdev->card_id;
	*out_cpu_id = plx_identify_cpu_id(xdev->pdev);
}

static bool __is_link_side(struct vop_device *vpdev)
{
	struct plx_device *xdev = vpdev_to_xdev(vpdev);

	return xdev->link_side;
}

struct vop_hw_ops vop_hw_ops = {
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
	.is_link_side = __is_link_side
};

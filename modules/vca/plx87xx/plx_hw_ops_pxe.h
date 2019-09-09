/*
 * Intel VCA Software Stack (VCASS)
 *
 * Copyright(c) 2015-2019 Intel Corporation.
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

#ifndef _PLX_HWOPS_PXE_H
#define _PLX_HWOPS_PXE_H

#include <linux/irqreturn.h>

#include "plx_device.h"
#include "plx_hw.h"

struct plx_pxe_hw_ops {
	struct vca_irq * (*request_threaded_irq)(struct plx_device *xdev,
				 irq_handler_t handler, irq_handler_t thread_fn,
				 const char *name, void *data, int intr_src);
	void (*free_irq)(struct plx_device *xdev,
			  struct vca_irq *cookie, void *data);
	int (*next_db)(struct plx_device *xdev);
	u32 (*read_spad)(struct plx_device *xdev, unsigned int idx);
	void __iomem * (*ioremap)(struct plx_device *xdev, dma_addr_t pa, size_t len);
	void (*iounmap)(struct plx_device *xdev, void __iomem *va);
};

#endif // _PLX_HWOPS_PXE_H

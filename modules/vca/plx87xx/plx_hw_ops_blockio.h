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

#ifndef _PLX_HWOPS_BLOCKIO_H
#define _PLX_HWOPS_BLOCKIO_H_

#include <linux/irqreturn.h>

struct plx_blockio_hw_ops {
        int (*next_db)(struct device *dev);
        struct vca_irq * (*request_irq)(struct device *dev,
                        irqreturn_t (*func)(int irq, void *data),
                        const char *name, void *data, int intr_src);
        void (*free_irq)(struct device *dev,
                        struct vca_irq *cookie, void *data);
        void (*ack_interrupt)(struct device *dev, int num);
        void * (*get_dp)(struct device *dev);
        void (*send_intr)(struct device *dev, int db);
        void __iomem * (*ioremap)(struct device *dev,
                        dma_addr_t pa, size_t len);
        void (*iounmap)(struct device *dev, void __iomem *va);
        void (*set_net_dev_state)(struct device *dev, bool state);
        void (*get_card_and_cpu_id)(struct device *dev,
                u8 *out_card_id, u8 *out_cpu_id);
        bool (*is_link_side)(struct device *dev);
	/* TODO rename to sth like "set_bdp_addr" */
	int (*set_dev_page)(struct device *dev);
	u64 (*get_dp_addr)(struct device *dev, bool reset);
	u32 (*get_f2b_db)(struct device *dev);
	dma_addr_t (*bar_va_to_pa)(struct device *dev, void __iomem *va);
	void (*stop_network_traffic)(struct device *dev);
};

#endif // _PLX_HWOPS_BLOCKIO_H_



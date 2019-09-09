/*
 * Intel VCA Software Stack (VCASS)
 *
 * Copyright(c) 2016-2017 Intel Corporation.
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
 * Intel VCA Block IO driver
 */
#include <linux/module.h>
#include <linux/dma-mapping.h>

#include "vcablk_common/vcablk_common.h"
#include "vcablk/vcablk_hal.h"
#include "plx_hw_ops_blockio.h"

struct vcablk_dev* vcablk_register(struct device* dev,
	struct plx_blockio_hw_ops* hw_ops,
	void *dev_page,
	size_t dev_page_size)
{
	return vcablk_init(dev, dev_page, dev_page_size, hw_ops);
}
EXPORT_SYMBOL(vcablk_register);

void vcablk_unregister(struct vcablk_dev* bdev)
{
	vcablk_deinit(bdev);
}
EXPORT_SYMBOL(vcablk_unregister);


int vcablk_dma_map_single(struct device *dev, void*va, size_t size, dma_addr_t *addr_out)
{
	dma_addr_t addr = dma_map_single(dev, va, size, DMA_BIDIRECTIONAL);
	int err = dma_mapping_error(dev, addr);
	if (err)
		return err;
	*addr_out = addr;
	return 0;
}

void vcablk_dma_unmap_single(struct device* dev, dma_addr_t dma_addr, size_t size)
{
	dma_unmap_single(dev, dma_addr, size, DMA_BIDIRECTIONAL);
}


int vcablk_dma_map_page(struct device *dev, struct page *page,
		unsigned long offset, size_t size, enum dma_data_direction dir,
		dma_addr_t *addr_out)
{

	dma_addr_t addr = dma_map_page(dev, page, offset, size, dir);
	int err = dma_mapping_error(dev, addr);
	if (err)
		return err;
	*addr_out = addr;
	return 0;
}

void vcablk_dma_unmap_page(struct device *dev, dma_addr_t dma_handle,
		size_t size, enum dma_data_direction dir)
{
	dma_unmap_page(dev, dma_handle, size, dir);
}

static int __init _vcablk_init(void)
{
	return 0;
}
static void _vcablk_exit(void)
{
}

module_init(_vcablk_init);
module_exit(_vcablk_exit);


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
#ifndef __VCA_BLK_IFC_H__
#define __VCA_BLK_IFC_H__

#include <linux/irqreturn.h>
#include <linux/dma-direction.h>

/* communitacation layer API */
int vcablk_dma_map_single(struct device *dev, void *va, size_t size, dma_addr_t *addr_out);
void vcablk_dma_unmap_single(struct device *dev, dma_addr_t dma_addr, size_t size);

int vcablk_dma_map_page(struct device *dev, struct page *page,
		unsigned long offset, size_t size, enum dma_data_direction dir,
		dma_addr_t *addr_out);
void vcablk_dma_unmap_page(struct device *dev, dma_addr_t dma_handle,
		size_t size, enum dma_data_direction dir);


/* frontend API */
struct vcablk_dev
{
	struct work_struct probe_work;
	wait_queue_head_t destroy_devpage_queue;
	struct mutex lock;
	struct vcablk_dev_page *pages;

	int probe_db;
	void *probe_db_cookie;

	int max_dev;
	struct vcablk_disk **vcablk_disks; 	/* Devices array */
	int f2b_db;

	struct device *parent;

	struct plx_blockio_hw_ops* hw_ops;
};


struct vcablk_dev* vcablk_init(struct device *dev, void* dev_page, size_t dev_page_size, struct plx_blockio_hw_ops* hw_ops);
void vcablk_deinit(struct vcablk_dev* fdev);

#endif // __VCA_BLK_IFC_H__

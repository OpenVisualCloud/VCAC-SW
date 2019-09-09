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

/* last argument of signatures of .map_page and .unmap_page
 * changed in kernel 4.8  to unsigned long
 * we are not using that arg, but we use -Werror compilation flag */
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 8, 0)
	typedef struct dma_attrs *vca_dma_attrs;
#else
	typedef unsigned long vca_dma_attrs;
#endif

static inline struct plx_device *vpdev_to_xdev(struct vop_device *vpdev)
{
	return dev_get_drvdata(vpdev->dev.parent);
}

static dma_addr_t
_plx_dma_map_page(struct device *dev, struct page *page,
		  unsigned long offset, size_t size,
		  enum dma_data_direction dir, vca_dma_attrs attrs)
{
	void *va = phys_to_virt(page_to_phys(page)) + offset;
	struct vop_device *vpdev = dev_get_drvdata(dev);
	struct plx_device *xdev = vpdev_to_xdev(vpdev);

	(void) sizeof(attrs);
	return pci_map_single(xdev->pdev, va, size, dir);
}

static void
_plx_dma_unmap_page(struct device *dev, dma_addr_t dma_addr,
		    size_t size, enum dma_data_direction dir,
		    vca_dma_attrs attrs)
{
	struct vop_device *vpdev = dev_get_drvdata(dev);
	struct plx_device *xdev = vpdev_to_xdev(vpdev);

	(void) sizeof(attrs);
	pci_unmap_single(xdev->pdev, dma_addr, size, dir);
}

struct dma_map_ops _plx_dma_ops = {
	.map_page = _plx_dma_map_page,
	.unmap_page = _plx_dma_unmap_page,
};

/* Initialize the VCA bootparams */
void plx_bootparam_init(struct plx_device *xdev)
{
	struct vca_bootparam *bootparam = xdev->dp;

	memset(bootparam, 0, sizeof(*bootparam));

	bootparam->magic = cpu_to_le32(VCA_MAGIC);
	bootparam->version_host = VCA_PROTOCOL_VERSION;
	bootparam->test_flags_events = 0;
	memset(bootparam->reserved, 0, sizeof(bootparam->reserved));
	bootparam->h2c_config_db = -1;
	bootparam->node_id = xdev->id + 1;
	bootparam->c2h_scif_db = -1;
	bootparam->h2c_scif_db = -1;
	bootparam->h2c_csa_mem_db = -1;
	bootparam->blockio_ftb_db = xdev->blockio.ftb_db;
}

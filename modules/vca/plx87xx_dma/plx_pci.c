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
 * PLX87XX DMA driver
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/seq_file.h>
#include "plx_dma.h"
#include "../common/vca_common.h"

#define INTEL_PCI_DEVICE_2264	0x2264
#define PLX_DRV_NAME		"plx87xx_dma"
#define INTEL_VCA_DMA_ID	0x2952

struct dentry *plx_dma_dbg;

static struct pci_device_id plxv_pci_id_table[] = {
	{PCI_DEVICE(PLX_PCI_VENDOR_ID_PLX, 0x87d0)},
	{PCI_DEVICE(PLX_PCI_VENDOR_ID_PLX, 0x87e0)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, INTEL_VCA_DMA_ID)},
	{ 0 } /* Required last entry */
};

MODULE_DEVICE_TABLE(pci, plxv_pci_id_table);

static ssize_t
plx_dma_hang_show(struct device *dev, struct device_attribute *attr,
		 char *buf)
{
	struct plx_dma_device *plx_dma_dev = dev_get_drvdata(dev);

	if (!plx_dma_dev)
		return -EINVAL;

	return snprintf(buf, PAGE_SIZE, "%d", plx_dma_dev->plx_chan.dma_hang);
}

static ssize_t
plx_dma_hang_write(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct plx_dma_device *plx_dma_dev = dev_get_drvdata(dev);
	struct plx_dma_chan *ch;

	if (!plx_dma_dev)
		return -EINVAL;

	ch = &plx_dma_dev->plx_chan;

	if (buf[0] == '0')
		ch->dma_hang = false;
	else if (buf[0] == '1')
		ch->dma_hang = true;
	return count;
}
static DEVICE_ATTR(plx_dma_hang, PERMISSION_READ,
		   plx_dma_hang_show, plx_dma_hang_write);

static struct plx_dma_device *
plx_alloc_dma(struct pci_dev *pdev, void __iomem *iobase)
{
	struct plx_dma_device *d = kzalloc(sizeof(*d), GFP_KERNEL);

	if (!d)
		return NULL;
	d->pdev = pdev;
	d->reg_base = iobase;
	return d;
}

static int
plxv_pci_probe(struct pci_dev *pdev, const struct pci_device_id *pdev_id)
{
	int rc;
	struct plx_dma_device *dma_dev;
	void __iomem * const *iomap;

	rc = pcim_enable_device(pdev);
	if (rc)
		goto done;
	rc = pcim_iomap_regions(pdev, 0x1, PLX_DRV_NAME);
	if (rc)
		goto done;
	iomap = pcim_iomap_table(pdev);
	if (!iomap) {
		rc = -ENOMEM;
		goto done;
	}
	rc = pci_set_dma_mask(pdev, DMA_BIT_MASK(64));
	if (rc)
		goto done;
	rc = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64));
	if (rc)
		goto done;
	dma_dev = plx_alloc_dma(pdev, iomap[0]);
	if (!dma_dev) {
		rc = -ENOMEM;
		goto done;
	}
	pci_set_master(pdev);
	pci_set_drvdata(pdev, dma_dev);
	rc = plx_dma_probe(dma_dev);
	if (rc) {
		dev_err(&pdev->dev, "plx_dma_probe error rc %d\n", rc);
		goto free_device;
	}
	rc = device_create_file(&pdev->dev, &dev_attr_plx_dma_hang);
	if (rc) {
		dev_err(&pdev->dev,
			"failed to create dma_hand file, rc: %d\n", rc);
		goto free_device;
	}

	return 0;
free_device:
	kfree(dma_dev);
done:
	return rc;
}

static void  plxv_pci_remove(struct pci_dev *pdev)
{
	struct plx_dma_device *dma_dev = pci_get_drvdata(pdev);

	device_remove_file(&pdev->dev, &dev_attr_plx_dma_hang);

	plx_dma_remove(dma_dev);
	kfree(dma_dev);
}

static struct pci_driver plxv_pci_driver = {
	.name     = PLX_DRV_NAME,
	.id_table = plxv_pci_id_table,
	.probe    = plxv_pci_probe,
	.remove   =  plxv_pci_remove
};

static int __init plxv_pci_init(void)
{
	int rc;

	plx_dma_dbg = debugfs_create_dir(KBUILD_MODNAME, NULL);
	rc = pci_register_driver(&plxv_pci_driver);
	if (rc)
		pr_err("%s %d rc %x\n", __func__, __LINE__, rc);
	return rc;
}

static void __exit plxv_pci_exit(void)
{
	pci_unregister_driver(&plxv_pci_driver);
	debugfs_remove_recursive(plx_dma_dbg);
}

module_init(plxv_pci_init);
module_exit(plxv_pci_exit);

MODULE_AUTHOR("Intel Corporation");
MODULE_DESCRIPTION("PLX87XX DMA driver");
MODULE_LICENSE("GPL v2");

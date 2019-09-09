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
#include <linux/blkdev.h>
#include <linux/kthread.h>
#include <linux/mutex.h>
#include <linux/miscdevice.h>
#include <linux/vmalloc.h>

#include "vcablk_hal_test.h"
#include "vcablk_common/vcablk_common.h"
#include "vcablk_bcknd/vcablk_bcknd_hal.h"
#include "vcablk_bcknd/vcablk_bcknd_ioctl.h"
#include "vcablk_test/vcablk_test_hw_ops.h"

static struct vcablk_bcknd_dev *g_bcknd;

/* callback and wrappers for "probe ack irq" */

static struct vcablk_impl_local_cbs * g_cbs;
static u64 g_dp_phys = 0;

#define DB_NUMS (16)
static int db_counter = 0;
static void* contexts[DB_NUMS];
static irqreturn_t (*handlers[DB_NUMS])(int irq, void *data) = {NULL};

int
vcablk_bcknd_create(struct vcablk_bcknd_dev *bdev, struct vcablk_disk_open_desc *desc);

static void vcablk_irq_handler(int id)
{
	if (id < 0 || id >= DB_NUMS) {
		pr_err("%s: Wrong DB id: %i\n", __func__, id);
	} else if (!handlers[id]) {
		pr_err("%s: Not set DB id: %i\n", __func__, id);
	} else {
		handlers[id](id, contexts[id]);
	}
}

void vcablk_bcknd_set_dev_page_local(pci_addr dev_page)
{
	g_cbs = NULL;

	if (dev_page) {
		g_cbs = phys_to_virt(dev_page);
		g_dp_phys = (pci_addr)((char*)dev_page + sizeof(*g_cbs));
		g_cbs->be_callbacks.irq_handler = vcablk_irq_handler;
	}
}
EXPORT_SYMBOL_GPL(vcablk_bcknd_set_dev_page_local);

static int next_db(struct device *dev)
{
	int i;
	int db = -1;

	for(i = 0; i < DB_NUMS; i++)
		if (!handlers[(db_counter + i) % DB_NUMS]) {
			db = (db_counter + i) % DB_NUMS;
			db_counter = (db_counter + i + 1) % DB_NUMS;
			break;
		}

	return db;
}

static void free_irq(struct device *dev,
		struct vca_irq *cookie, void *data)
{
	if (cookie && !IS_ERR(cookie))
		handlers[(uintptr_t)cookie] = NULL;
}

static struct vca_irq* request_irq(struct device *dev,
		irqreturn_t (*func)(int irq, void *data),
		const char *name, void *data, int intr_src)
{
	if (intr_src < 0 || intr_src >= DB_NUMS) {
		pr_err("%s: Wrong DB id: %i\n", __func__, intr_src);
		return ERR_PTR(-EIO);
	}

	if (handlers[intr_src]) {
		pr_err("%s: USED DB id: %i, ignore previous handler in tests!!!\n",
				__func__, intr_src);
		return ERR_PTR(-EIO);
	}

	contexts[intr_src] = data;
	handlers[intr_src] = func;
	return (struct vca_irq *)(unsigned long)intr_src;
}

static void send_intr(struct device *dev, int db)
{
	g_cbs->fe_callbacks.irq_handler(db);
}

static void ack_interrupt(struct device *dev, int db)
{

}

static void vcablk_bcknd_create_test_ram_disk(struct vcablk_bcknd_dev* bdev)
{
	int ret;
	int diskid = 20;
	/* Setup first RAM devices */
	if (!bdev->device_array[diskid]) {
		struct vcablk_disk_open_desc *desc = vmalloc(sizeof (struct vcablk_disk_open_desc));
		if (!desc) {
			pr_warning("vcablk_bcknd: %s Can not alloc memory\n", __func__);
		} else {
			desc->disk_id = diskid;
			strcpy(desc->file_path, "RAM DISK");
			desc->mode = VCABLK_DISK_MODE_READ_WRITE;
			desc->size = 10 * 1024 * 512	/* How big the drive is */;
			desc->type = VCABLK_DISK_TYPE_MEMORY;
			ret = vcablk_bcknd_create(bdev, desc);
			if (ret || !bdev->device_array[diskid]) {
				pr_err("%s: Can not create first device\n", __func__);
			}
			vfree(desc);
		}
	}
}

static void __iomem *comm_ioremap(struct device *dev, dma_addr_t pa, size_t len)
{
	return phys_to_virt(pa);
}

static void comm_iounmap(struct device* dev, void __iomem *va)
{
}

static dma_addr_t bar_va_to_pa(struct device *dev, void __iomem *va)
{
	return (dma_addr_t)virt_to_phys(va);
}

int vcablk_bcknd_dma_sync(struct dma_chan *dma_ch, dma_addr_t dst,
		dma_addr_t src, size_t len)
{
	return -EIO;
}

int vcablk_bcknd_dma_async(struct dma_chan *dma_ch, dma_addr_t dst,
		dma_addr_t src, size_t len, dma_async_tx_callback callback,
		void *callback_param, struct dma_async_tx_descriptor **out_tx)
{
	return -EIO;
}

u32 get_f2b_db(struct device *dev)
{
	return 0;
}

u64 get_dp_addr(struct device *dev, bool reset)
{
	return g_dp_phys;
}

struct plx_blockio_hw_ops g_hw_ops = {
	.next_db = next_db,
	.request_irq = request_irq,
	.free_irq = free_irq,
	.ack_interrupt = ack_interrupt,
	.send_intr = send_intr,
	.ioremap = comm_ioremap,
	.iounmap = comm_iounmap,
	.get_dp_addr = get_dp_addr,
	.get_f2b_db = get_f2b_db,
	.bar_va_to_pa = bar_va_to_pa,
	.stop_network_traffic = NULL,
};

static int __init vcablk_bcknd_init(void)
{
	struct miscdevice *md;

	md =vcablk_bcknd_register(NULL, "vcablk_bcknd_local", &g_hw_ops, NULL);
	if (IS_ERR(md)) {
		int err = PTR_ERR(md);
		g_bcknd = NULL;
		return err;
	}

	g_bcknd = container_of(md, struct vcablk_bcknd_dev, mdev);

	/* Add test device */
	vcablk_bcknd_create_test_ram_disk(g_bcknd);

	return 0;
}

static void vcablk_bcknd_exit(void)
{
	vcablk_bcknd_unregister(&g_bcknd->mdev);
	g_bcknd = NULL;
}

module_init(vcablk_bcknd_init);
module_exit(vcablk_bcknd_exit);


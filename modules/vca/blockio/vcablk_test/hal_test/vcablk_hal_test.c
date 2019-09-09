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

#include "vcablk_hal_test.h"
#include "vcablk_common/vcablk_common.h"
#include "vcablk/vcablk_hal.h"
#include "vcablk_test/vcablk_test_hw_ops.h"

MODULE_LICENSE("GPL");

static struct vcablk_impl_local_cbs *vcablk_dev_imp_local_callbacks = NULL;
static size_t vcablk_dev_imp_local_callbacks_pages = 2;

extern void vcablk_bcknd_set_dev_page_local(pci_addr dev_page);

#define DB_NUMS (16)
static int db_counter = 0;
static void* contexts[DB_NUMS];
static irqreturn_t (*handlers[DB_NUMS])(int irq, void *data) = {NULL};

static struct vcablk_dev* g_fdev = NULL;

struct vcablk_page_map {
	struct list_head list_member;
	struct page *page;
	dma_addr_t addr;
};

LIST_HEAD(vcablk_page_map_list);
DEFINE_MUTEX(vcablk_page_map_list_lock);

int vcablk_dma_map_single(struct device* dev, void *va, size_t size, dma_addr_t *addr_out)
{
	*addr_out = virt_to_phys(va);
	return 0;
}

void vcablk_dma_unmap_single(struct device *dev, dma_addr_t dma_addr, size_t size)
{

}

int vcablk_dma_map_page(struct device *dev, struct page *page,
		unsigned long offset, size_t size, enum dma_data_direction dir,
		dma_addr_t *addr_out)
{
	char *va;
	struct vcablk_page_map *page_map = kmalloc(sizeof(struct vcablk_page_map),
			GFP_ATOMIC);
	if (!page_map)
		return -ENOMEM;

	va = kmap(page) + offset;
	*addr_out = virt_to_phys(va);

	page_map->page = page;
	page_map->addr = *addr_out;
	INIT_LIST_HEAD(&page_map->list_member);

	mutex_lock(&vcablk_page_map_list_lock);
	list_add(&page_map->list_member, &vcablk_page_map_list);
	mutex_unlock(&vcablk_page_map_list_lock);
	return 0;
}

void vcablk_dma_unmap_page(struct device *dev, dma_addr_t dma_handle,
		size_t size, enum dma_data_direction dir)
{
	struct list_head *iter;
	struct vcablk_page_map *obj;

	BUG_ON(!dma_handle);

	mutex_lock(&vcablk_page_map_list_lock);
	list_for_each(iter, &vcablk_page_map_list) {
		obj = list_entry(iter, struct vcablk_page_map, list_member);
		if(obj->addr == dma_handle) {
			list_del(&obj->list_member);
			mutex_unlock(&vcablk_page_map_list_lock);
			kunmap(obj->page);
			kfree(obj);
			return;
		}
	}
	mutex_unlock(&vcablk_page_map_list_lock);

	pr_err("%s: NOT FOUND MAPPING : da 0x%llu size %lu dir %u\n", __func__, dma_handle, size, dir);
	BUG_ON(1);
}

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

static void free_irq(struct device *dev, struct vca_irq *cookie, void *data)
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

static void send_intr(struct device *dev, int db)
{
	if (!vcablk_dev_imp_local_callbacks) {
		pr_err("%s: vcablk_dev_imp_local_callbacks is NULL\n", __func__);
		return;
	}
	if (!vcablk_dev_imp_local_callbacks->be_callbacks.irq_handler) {
		pr_err("%s: irq_handler is NULL\n", __func__);
		return;
	}
	vcablk_dev_imp_local_callbacks->be_callbacks.irq_handler(db);
}

static void ack_interrupt(struct device *dev, int db)
{

}

static void __iomem *comm_ioremap(struct device *dev, dma_addr_t pa, size_t len)
{
	return phys_to_virt(pa);
}

static void comm_iounmap(struct device *dev, void __iomem *va)
{
}

static int vcablk_call_bcknd_set_dev_page(struct device* dev)
{
	//void* page = (void*)((uintptr_t)_page/* - sizeof (struct vcablk_impl_local_cbs)*/);
	void* page = (void*)vcablk_dev_imp_local_callbacks;
	void (*fn_set_dev)(pci_addr);
	pci_addr page_phys = virt_to_phys(page);

	fn_set_dev = symbol_get(vcablk_bcknd_set_dev_page_local);
	if (!fn_set_dev) {
		pr_err("%s: Not found symbol vcablk_bcknd_set_dev_page\n",
				__func__);
		return -EINVAL;
	}

	fn_set_dev(page_phys);

	symbol_put(vcablk_bcknd_set_dev_page_local);

	send_intr(dev, 0);

	return 0;
}

static u32 get_f2b_db(struct device *dev)
{
	return 0;
}

struct plx_blockio_hw_ops g_hw_ops = {
	.next_db = next_db,
	.request_irq = request_irq,
	.free_irq = free_irq,
	.ack_interrupt = ack_interrupt,
	.send_intr = send_intr,
	.ioremap = comm_ioremap,
	.iounmap = comm_iounmap,
	.set_dev_page = vcablk_call_bcknd_set_dev_page,
	.get_f2b_db = get_f2b_db,
};

static void* vcablk_alloc_devpage(size_t *out_size)
{
	size_t size = vcablk_dev_imp_local_callbacks_pages * PAGE_SIZE; // one page for communication callbacks
	void *vcablk_dev_pages = NULL;

	*out_size = 0;

	if (!vcablk_dev_imp_local_callbacks) {
		vcablk_dev_imp_local_callbacks = (void *)__get_free_pages(GFP_KERNEL | __GFP_DMA32,
				vcablk_dev_imp_local_callbacks_pages);

		if (!vcablk_dev_imp_local_callbacks) {
			pr_err("%s: unable to alloc device page\n", __func__);
			return NULL;
		}

		memset(vcablk_dev_imp_local_callbacks, 0, size);
	}

	vcablk_dev_imp_local_callbacks->fe_callbacks.irq_handler = vcablk_irq_handler;

	vcablk_dev_pages =  (void*)((uintptr_t)vcablk_dev_imp_local_callbacks + sizeof (struct vcablk_impl_local_cbs));
	*out_size = size -  sizeof (struct vcablk_impl_local_cbs);
	return vcablk_dev_pages;
}

static void vcablk_dealloc_dev_page(void)
{
	if (vcablk_dev_imp_local_callbacks) {
		free_pages((unsigned long)vcablk_dev_imp_local_callbacks,
				vcablk_dev_imp_local_callbacks_pages);
		vcablk_dev_imp_local_callbacks = NULL;
	}
}

static int __init _vcablk_init(void)
{
	size_t dev_page_size;
	void* dev_page = vcablk_alloc_devpage(&dev_page_size);

	if (!dev_page)
		return -ENOMEM;

	g_fdev = vcablk_init(NULL, dev_page, dev_page_size, &g_hw_ops);
	if (IS_ERR(g_fdev)) {
		int err = PTR_ERR(g_fdev);
		vcablk_dealloc_dev_page();
		g_fdev = NULL;
		return err;
	};
	return 0;
}

static void _vcablk_exit(void)
{
	vcablk_deinit(g_fdev);
	g_fdev = NULL;
	vcablk_dealloc_dev_page();
}

module_init(_vcablk_init);
module_exit(_vcablk_exit);


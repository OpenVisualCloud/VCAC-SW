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
#ifndef __VCA_BLK_BACKEND_H__
#define __VCA_BLK_BACKEND_H__

#include <linux/dmaengine.h>
#include <linux/miscdevice.h>

struct vcablk_bcknd_disk;

/* communitacation layer API */
int vcablk_bcknd_dma_sync(struct dma_chan *dma_ch, dma_addr_t dst,
		dma_addr_t src, size_t len);
int vcablk_bcknd_dma_async(struct dma_chan *dma_ch, dma_addr_t dst,
		dma_addr_t src, size_t len, dma_async_tx_callback callback,
		void *callback_param, struct dma_async_tx_descriptor **out_tx);


/* This is private backend struture. It's put here in header only for test build purposes */
struct vcablk_bcknd_dev
{
	struct miscdevice mdev;
	// global data goes here
	int magic;
	char dev_name[64];

	struct mutex lock;
	struct vcablk_dev_page *dev_pages_remapped;
	char frontend_name[VCA_FRONTEND_NAME_SIZE];
	struct vcablk_bcknd_disk **device_array;
	struct work_struct work_ftb;

	int ftb_db; // Frontend To Backend configuration doorbell (set dev page or task request)
	void *ftb_db_cookie;

	pci_addr dp_addr;

	struct plx_blockio_hw_ops* hw_ops;
	struct dma_chan *dma_ch;
};

/* backend API for lower layer (PCI or local communication) */
int vcablk_bcknd_register_f2b_callback(struct vcablk_bcknd_dev *bdev);
void vcablk_bcknd_unregister_f2b_callback(struct vcablk_bcknd_dev *bdev);
struct miscdevice *vcablk_bcknd_register(struct device* parent,
		const char *dev_name, struct plx_blockio_hw_ops* hw_ops,
		struct dma_chan *dma_ch);
void vcablk_bcknd_unregister(struct miscdevice *mdev);

#endif /* __VCA_BLK_BACKEND_H__ */

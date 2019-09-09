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
#ifndef __VCABLK_COMM_PCI_H__
#define __VCABLK_COMM_PCI_H__

struct vcablk_dev;

int vcablkbe_register_f2b_callback(struct miscdevice*);
void vcablkbe_unregister_f2b_callback(struct miscdevice*);

struct miscdevice *vcablkebe_register(
		struct device* parent,
		struct plx_blockio_hw_ops* hw_ops,
		struct dma_chan *dma_ch,
		int card_id, int cpu_id);

void vcablkebe_unregister(struct miscdevice* md);

struct vcablk_dev* vcablk_register(
		struct device* dev,
		struct plx_blockio_hw_ops* hw_ops,
		void *dev_page,
		size_t dev_page_size);

void vcablk_unregister(struct vcablk_dev*);

#endif //__VCABLK_COMM_PCI_H__

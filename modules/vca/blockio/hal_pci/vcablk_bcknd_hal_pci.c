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

#include "vcablk_common/vcablk_common.h"
#include "vcablk_bcknd/vcablk_bcknd_hal.h"
#include "plx_hw_ops_blockio.h"

static int __init vcablk_bcknd_init(void)
{
	return 0;
}

static void vcablk_bcknd_exit(void)
{
}

module_init(vcablk_bcknd_init);
module_exit(vcablk_bcknd_exit);

int vcablkbe_register_f2b_callback(struct miscdevice* mdev)
{
	struct vcablk_bcknd_dev *bdev = container_of(mdev,
			struct vcablk_bcknd_dev, mdev);
	return vcablk_bcknd_register_f2b_callback(bdev);
}
EXPORT_SYMBOL(vcablkbe_register_f2b_callback);

void vcablkbe_unregister_f2b_callback(struct miscdevice* mdev)
{
	struct vcablk_bcknd_dev *bdev = container_of(mdev,
			struct vcablk_bcknd_dev, mdev);
	vcablk_bcknd_unregister_f2b_callback(bdev);
}
EXPORT_SYMBOL(vcablkbe_unregister_f2b_callback);

struct miscdevice* vcablkebe_register(struct device* parent, struct plx_blockio_hw_ops* hw_ops,
	struct dma_chan *dma_ch, int card_id, int cpu_id)
{
	char dev_name[64];
	snprintf(dev_name, sizeof(dev_name), "vca_blk_bcknd%i%i", card_id, cpu_id);

	return vcablk_bcknd_register(parent, dev_name, hw_ops, dma_ch);
}
EXPORT_SYMBOL(vcablkebe_register);

void vcablkebe_unregister(struct miscdevice* mdev)
{
	 vcablk_bcknd_unregister(mdev);
}
EXPORT_SYMBOL(vcablkebe_unregister);

/*
 * vcablk_bcknd_dma_sync - Wrapper for synchronous DMAs.
 *
 * @dev - The address of the pointer to the device instance used
 * for DMA registration.
 * @dst - destination DMA address.
 * @src - source DMA address.
 * @len - size of the transfer.
 *
 * Return DMA_SUCCESS on success
 */
int vcablk_bcknd_dma_sync(struct dma_chan *dma_ch, dma_addr_t dst,
		dma_addr_t src, size_t len)
{
	int err = 0;
	struct dma_device *ddev;
	struct dma_async_tx_descriptor *tx;

	if (!dma_ch) {
		pr_err("%s: no DMA channel available\n", __func__);
		err = -EBUSY;
		goto error;
	}
	ddev = dma_ch->device;
	tx = ddev->device_prep_dma_memcpy(dma_ch, dst, src, len,
		DMA_PREP_FENCE);
	if (!tx) {
		err = -ENOMEM;
		goto error;
	} else {
		dma_cookie_t cookie;

		cookie = tx->tx_submit(tx);
		if (dma_submit_error(cookie)) {
			err = -ENOMEM;
			goto error;
		}
		err = dma_sync_wait(dma_ch, cookie);
	}
error:
	if (err)
		pr_err("%s %d err %d\n", __func__, __LINE__, err);
	return err;
}

/*
 * vcablk_bcknd_dma_async - Wrapper for asynchronous DMAs.
 *
 * @dma_ch - The address of the pointer to the device instance used
 * for DMA registration.
 * @dst - destination DMA address.
 * @src - source DMA address.
 * @len - size of the transfer.
 * @callback - callback routine
 * @callback_param - callback params
 * @out_tx - dma tx structure to disable callback when canceled.
 *
 * Return error code or dma cookie on successful transfer submi
 */
dma_cookie_t vcablk_bcknd_dma_async(struct dma_chan *dma_ch, dma_addr_t dst,
		dma_addr_t src, size_t len, dma_async_tx_callback callback,
		void *callback_param, struct dma_async_tx_descriptor **out_tx)
{
	dma_cookie_t cookie;

	if (!dma_ch) {
		pr_err("%s: no DMA channel available\n", __func__);
		cookie = -ENOMEM;
	} else {
		struct dma_device *ddev = dma_ch->device;
		struct dma_async_tx_descriptor *tx;

		tx = ddev->device_prep_dma_memcpy(dma_ch,  dst, src, len, DMA_PREP_INTERRUPT);
		if(!tx) {
			cookie = -ENOMEM;
		} else {
			tx->callback = callback;
			tx->callback_param = callback_param;
			if (out_tx) {
				/* It have to be before submitt because callback can be called
				 * in tx_submit() time. */
				*out_tx = tx;
			}
			cookie = tx->tx_submit(tx);
			if (!dma_submit_error(cookie)) {
				dma_async_issue_pending(dma_ch);
			} else {
				if (out_tx) {
					*out_tx = NULL;
				}
			}
		}
	}
	return cookie;
}


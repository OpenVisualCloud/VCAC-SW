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
 */
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/time.h>
#include <linux/rtc.h>
#include <linux/ktime.h>
#include <linux/mutex.h>
#include <linux/dma-mapping.h>
#include <linux/uaccess.h>

#include "plx_device.h"
#include "plx_hw.h"
#include "plx_lbp.h"
#include "plx_procfs.h"

#include "../pxe/vcapxe_register.h"

/* exclude first argument */
#define FIRST_ARG(first, ...) first

#define dev_err_once_dbg_later(...) \
	do { \
	static bool err_print = true; \
	if (err_print) { \
		err_print = false; \
		dev_err(__VA_ARGS__); \
		dev_err( FIRST_ARG(__VA_ARGS__), "[%s:%d Next occurrences will be logged at debug level]\n", __func__, __LINE__); \
	}  else { \
		dev_dbg(__VA_ARGS__); \
	} } while (0)

#define PLX_LBP_i7_IRQ_TIMEOUT_MS 1000
#define PLX_LBP_i7_ALLOC_TIMEOUT_MS 1000
#define PLX_LBP_i7_CMD_TIMEOUT_MS 100
#define PLX_LBP_i7_MAC_WRITE_TIMEOUT_MS 1000
#define LBP_DMA_BUFFER_SIZE (1024*1024) /*1MB*/

//#define FORCE_USE_MEMCPY

extern struct plx_device * plx_contexts[MAX_VCA_CARDS][MAX_VCA_CARD_CPUS];

static struct mutex * get_lbp_lock(unsigned int card_id, unsigned int cpu_id)
{
	static bool first_call = true;
	static struct mutex lbp_locks[MAX_VCA_CARDS][MAX_VCA_CARD_CPUS];
	if (first_call) {
		int i = 0, j = 0;
		for(i = 0; i < MAX_VCA_CARDS; i++)
			for(j = 0; j < MAX_VCA_CARD_CPUS; j++)
				mutex_init(&lbp_locks[i][j]);
		first_call = false;
	}
	if (card_id < MAX_VCA_CARDS && cpu_id < MAX_VCA_CARD_CPUS)
		return &lbp_locks[card_id][cpu_id];
	return NULL;
}

void plx_lbp_reset_start(unsigned int card_id, unsigned int cpu_id)
{
	if (card_id < MAX_VCA_CARDS && cpu_id < MAX_VCA_CARD_CPUS) {
		mutex_lock(get_lbp_lock(card_id, cpu_id));
		plx_contexts[card_id][cpu_id]->lbp_resetting = true;
	}
}
EXPORT_SYMBOL_GPL(plx_lbp_reset_start);

void plx_lbp_reset_stop(unsigned int card_id, unsigned int cpu_id)
{
	if (card_id < MAX_VCA_CARDS && cpu_id < MAX_VCA_CARD_CPUS) {
		struct plx_device * context = plx_contexts[card_id][cpu_id];
		if (context) {
			plx_write_spad(context, PLX_LBP_SPAD_i7_READY, PLX_LBP_i7_DOWN);
			context->lbp_resetting = false;
		}
		mutex_unlock(get_lbp_lock(card_id, cpu_id));
	}
}
EXPORT_SYMBOL_GPL(plx_lbp_reset_stop);

static irqreturn_t plx_lbp_interrupt_handler(int irq, void *dev)
{
	struct plx_device *xdev = dev;
	struct plx_lbp_i7_ready i7_ready;
	complete_all(&xdev->lbp.card_wait);
	i7_ready.value = plx_read_spad( xdev, PLX_LBP_SPAD_i7_READY);
	if ((PLX_LBP_i7_AFTER_REBOOT | PLX_LBP_i7_UP) == i7_ready.ready)
		 plx_reboot_notify();
	return 0;
}

int plx_lbp_init(struct plx_device *xdev)
{
	int err = 0;
	int cpu_id = plx_identify_cpu_id(xdev->pdev);

	xdev->lbp_lock = get_lbp_lock(xdev->card_id, cpu_id);

	mutex_lock(xdev->lbp_lock);
	dev_info(&xdev->pdev->dev, "%s entering\n", __func__);

	init_completion(&xdev->lbp.card_wait);

	if (xdev->link_side) {
		dev_dbg(&xdev->pdev->dev,
		"%s LBP on card side is for debug purposes only, exiting\n", __func__);
		goto unlock;
	}

	xdev->lbp.parameters.i7_irq_timeout_ms = PLX_LBP_i7_IRQ_TIMEOUT_MS;
	xdev->lbp.parameters.i7_alloc_timeout_ms = PLX_LBP_i7_ALLOC_TIMEOUT_MS;
	xdev->lbp.parameters.i7_cmd_timeout_ms = PLX_LBP_i7_CMD_TIMEOUT_MS;
	xdev->lbp.parameters.i7_mac_write_timeout_ms =
		PLX_LBP_i7_MAC_WRITE_TIMEOUT_MS;

	if (!xdev->link_side)
		plx_write_spad(xdev, PLX_LBP_SPAD_E5_READY, PLX_LBP_E5_NOT_READY);
	else
		plx_write_spad(xdev, PLX_LBP_SPAD_i7_READY, PLX_LBP_i7_DOWN);


	xdev->lbp.irq  = plx_request_threaded_irq(xdev,
			plx_lbp_interrupt_handler,
			NULL, "plx lbp", xdev, PLX_LBP_DOORBELL);
	if (IS_ERR(xdev->lbp.irq)) {
		err = PTR_ERR(xdev->lbp.irq);
		dev_err(&xdev->pdev->dev,
				"%s error requesting irq for lbp: %d\n",
				__func__, err);
		goto unlock;
	}

unlock:
	mutex_unlock(xdev->lbp_lock);
	return err;
}


void plx_lbp_deinit(struct plx_device *xdev)
{
	mutex_lock(xdev->lbp_lock);
	dev_info(&xdev->pdev->dev, "%s entering\n", __func__);
	plx_free_irq(xdev, xdev->lbp.irq, xdev);
	mutex_unlock(xdev->lbp_lock);
}


#define lbp_wait_event(event, timeout_ms)  do \
{ \
	unsigned int to = timeout_ms;  \
	err = -ETIME; \
	while (to--) { \
		msleep(1); \
		if (event) { \
			err = 0; \
			break ; \
		} \
	} \
} while(0)


static struct plx_lbp_i7_ready plx_lbp_get_i7_status(struct plx_device *xdev)
{
	struct plx_lbp_i7_ready i7_ready;
	i7_ready.value = plx_read_spad(xdev, PLX_LBP_SPAD_i7_READY);
	return i7_ready;
}

static void plx_lbp_set_i7_status(struct plx_device *xdev, struct plx_lbp_i7_ready status)
{
	plx_write_spad(xdev, PLX_LBP_SPAD_i7_READY, status.value);
}

static struct plx_lbp_i7_ready plx_lbp_get_i7_rcvy_status(struct plx_device *xdev)
{
	struct plx_lbp_i7_ready i7_ready;
	i7_ready.value = plx_read_spad(xdev, PLX_LBP_SPAD_DATA_LOW);
	return i7_ready;
}

static
int plx_lbp_wait_for_i7_up(struct plx_device *xdev, unsigned int timeout_ms)
{
	u32 i7_ready;
	int err;

	dev_dbg(&xdev->pdev->dev, "%s entering\n", __func__);

	err = wait_for_completion_interruptible_timeout(&xdev->lbp.card_wait,
		msecs_to_jiffies(timeout_ms));

	if (err < 0) {
		dev_info(&xdev->pdev->dev, "%s wait interrupted\n",__func__);
		goto exit;
	} else if (err == 0) {
		dev_info(&xdev->pdev->dev, "%s wait timeout\n",__func__);
		err = -ETIME;
		goto exit;
	}
	err = 0;

	i7_ready = plx_lbp_get_i7_status(xdev).ready;
	if ((i7_ready & ~PLX_LBP_i7_AFTER_REBOOT) != PLX_LBP_i7_UP) {
		dev_err(&xdev->pdev->dev, "%s invalid card state: %x\n",
				__func__, i7_ready);
		err = -ENODEV;
		goto exit;
	}

	dev_dbg(&xdev->pdev->dev, "%s card is up\n",__func__);
exit:
	return err;
}

static
int plx_lbp_wait_for_i7_state(struct plx_device *xdev, unsigned int state,
				unsigned int timeout)
{
	unsigned int ready;
	int err;

	if (state == 0)
		lbp_wait_event(
			(ready = plx_lbp_get_i7_status(xdev).ready) == state,
			timeout);
	else
		lbp_wait_event(
			(ready = plx_lbp_get_i7_status(xdev).ready) & state,
			timeout);

	if (err) {
		if (err == -ETIME) {
			dev_err(&xdev->pdev->dev,
				"%s timeout\n", __func__);
		} else {
			dev_info(&xdev->pdev->dev,
				"%s wait error %d\n" ,__func__, err);
		}
		return err;
	}

	return ready;
}

static struct plx_lbp_i7_cmd plx_lbp_get_i7_cmd(struct plx_device *xdev)
{
	struct plx_lbp_i7_cmd i7_cmd;
	i7_cmd.value = plx_read_spad(xdev, PLX_LBP_SPAD_i7_CMD);
	return i7_cmd;
}

static
int plx_lbp_wait_for_i7_cmd_consumed(struct plx_device *xdev,
 unsigned int timeout_ms)
{
	u32 i7_cmd;
	int err = 0;

	dev_dbg(&xdev->pdev->dev, "%s entering\n", __func__);

	lbp_wait_event(
		(i7_cmd = plx_lbp_get_i7_cmd(xdev).cmd) == PLX_LBP_CMD_INVALID,
		timeout_ms);

	if (err) {
		if (err == -ETIME) {
			dev_err(&xdev->pdev->dev,
				"%s timeout\n", __func__);
		} else {
			dev_info(&xdev->pdev->dev,
				"%s wait error %d\n" ,__func__, err);
		}
	} else {
		dev_dbg(&xdev->pdev->dev, "%s command consumed\n",__func__);
	}

	return err;
}

/* TODO: irq/plx mutex ??? */
enum vca_lbp_retval plx_lbp_handshake(struct plx_device *xdev)
{
	int err;
	struct plx_lbp_i7_cmd cmd_invalid;
	struct plx_lbp_e5_ready e5_waiting;
	struct plx_lbp_e5_ready e5_not_ready;

	struct plx_lbp_e5_ready e5_ready;
	struct plx_lbp_i7_ready e7_ready;

	xdev->bios_information_date = 0;
	xdev->bios_information_version = 0;
	xdev->bios_cfg_sgx = -1;
	xdev->bios_cfg_gpu_aperture = -1;
	xdev->bios_cfg_tdp = -1;
	xdev->bios_cfg_hyper_threading = -1;
	xdev->bios_cfg_gpu = -1;

	cmd_invalid.cmd = PLX_LBP_CMD_INVALID;

	e5_waiting.ready = PLX_LBP_E5_WAITING_FOR_IRQ;

	e5_not_ready.ready = PLX_LBP_E5_NOT_READY;
	e5_not_ready.pci_slot_id = PCI_SLOT(xdev->pdev->devfn);
	e5_not_ready.ntb_port_id = xdev->port_id;

	e5_ready.ready = PLX_LBP_E5_READY;
	e5_ready.pci_slot_id = PCI_SLOT(xdev->pdev->devfn);
	e5_ready.ntb_port_id = xdev->port_id;

	mutex_lock(xdev->lbp_lock);
	dev_dbg(&xdev->pdev->dev, "%s entering\n", __func__);

	if (!xdev->vca_csm_dev) {
		dev_err(&xdev->pdev->dev, "%s VCA_CSM device not ready \n",
				__func__);
		err = -LBP_INTERNAL_ERROR;
		goto exit;
	}
	e5_waiting.CPUID = e5_ready.CPUID = xdev->vca_csm_dev->index;

	vcapxe_force_detach(xdev->pxe_dev);

	/* clear CMD - just to be sure */
	plx_write_spad(xdev, PLX_LBP_SPAD_i7_CMD, cmd_invalid.value);

	/* 0. need to reinit plx_lbp_irq 'semaphore' to get ready for next available IRQ */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 13, 0)
		reinit_completion(&xdev->lbp.card_wait);
#else
		init_completion(&xdev->lbp.card_wait);
#endif

	/* 1. indicate readiness for handshake interrupt */
	dev_dbg(&xdev->pdev->dev, "%s writing %x to spad no %x\n", __func__,
			e5_waiting.value, PLX_LBP_SPAD_E5_READY);
	plx_write_spad(xdev, PLX_LBP_SPAD_E5_READY, e5_waiting.value);

	/* 2. wait for interrupt from BIOS */
	err = plx_lbp_wait_for_i7_up(xdev, xdev->lbp.parameters.i7_irq_timeout_ms);
	if (err) {
		if (err < 0)
			err = -LBP_IRQ_TIMEOUT;
		goto exit;
	}

	e7_ready.value = plx_read_spad(xdev, PLX_LBP_SPAD_i7_READY);
	if (e7_ready.version != PLX_LBP_PROTOCOL_VERSION_CURRENT) {
		dev_err(&xdev->pdev->dev, "%s protocol version mismatch. Host" \
				" %x.%x , card %x.%x\n", __func__,
				PLX_LBP_PROTOCOL_GET_MAJOR(PLX_LBP_PROTOCOL_VERSION_CURRENT),
				PLX_LBP_PROTOCOL_GET_MINOR(PLX_LBP_PROTOCOL_VERSION_CURRENT),
				PLX_LBP_PROTOCOL_GET_MAJOR(e7_ready.version),
				PLX_LBP_PROTOCOL_GET_MINOR(e7_ready.version)
				);
	}

	/* 3. read card memory size */
	xdev->lbp.i7_ddr_size_mb = plx_read_spad(xdev, PLX_LBP_SPAD_DATA_LOW);

	/* 4. assign card with CPUID */
	plx_write_spad(xdev, PLX_LBP_SPAD_DATA_LOW, xdev->vca_csm_dev->index);
	plx_write_spad(xdev, PLX_LBP_SPAD_DATA_HIGH, 0);

	/* 5. change E5 state to ready */
	plx_write_spad(xdev, PLX_LBP_SPAD_E5_READY, e5_ready.value);
	wmb();

	/* 6. wait for card to acknowledge */
	err = plx_lbp_wait_for_i7_state(xdev, PLX_LBP_i7_READY,
		xdev->lbp.parameters.i7_irq_timeout_ms);
	if (err < 0)
		err = -LBP_CMD_TIMEOUT;
exit:
	if (err >= 0) {
		if (plx_lbp_copy_bios_info(xdev, PLX_LBP_PARAM_BIOS_BUILD_DATE, &xdev->bios_information_date) != 0 ||
			plx_lbp_copy_bios_info(xdev, PLX_LBP_PARAM_BIOS_VER, &xdev->bios_information_version) != 0 ||
			plx_lbp_copy_bios_info(xdev, PLX_LBP_PARAM_SGX, &xdev->bios_cfg_sgx) != 0 ||
			plx_lbp_copy_bios_info(xdev, PLX_LBP_PARAM_GPU, &xdev->bios_cfg_gpu) != 0 ||
			plx_lbp_copy_bios_info(xdev, PLX_LBP_PARAM_GPU_APERTURE, &xdev->bios_cfg_gpu_aperture) != 0 ||
			plx_lbp_copy_bios_info(xdev, PLX_LBP_PARAM_TDP, &xdev->bios_cfg_tdp) != 0 ||
			plx_lbp_copy_bios_info(xdev, PLX_LBP_PARAM_HT, &xdev->bios_cfg_hyper_threading) != 0)

			dev_err(&xdev->pdev->dev, "Can't retrieve bios information\n");
	}

	mutex_unlock(xdev->lbp_lock);
	if (err >= 0)
		return LBP_STATE_OK;
	else {
		plx_write_spad(xdev, PLX_LBP_SPAD_E5_READY, e5_not_ready.value);
		return -err;
	}
}

static int vca_lbp_sync_dma(struct plx_device *xdev, dma_addr_t dst,
		dma_addr_t src, size_t len)
{
	int err = 0;
	struct dma_device *ddev;
	struct dma_async_tx_descriptor *tx;
	struct dma_chan *dma_ch = xdev->dma_ch;

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
		dev_err_once_dbg_later(&xdev->pdev->dev, "%s %d err %d\n",
			__func__, __LINE__, err);
	return err;
}

static enum vca_lbp_retval plx_lbp_send_ramdisk(struct plx_device *xdev,
 void __user * img, size_t img_size)
{
	int err;
	size_t alloc_size;
	u32 i7_error;
	u64 ramdisk_ph;
	void* temp_buff;
	dma_addr_t temp_buff_da = 0;
	u32 temp_buff_size;
	u32 temp_buff_pages_order;
	u64 offset;
	u32 chunk_size;
	struct plx_lbp_i7_cmd cmd_map_ramdisk;
	void* remapped = NULL;
	dma_addr_t chunk_dst;
	cmd_map_ramdisk.cmd = PLX_LBP_CMD_MAP_RAMDISK;
	cmd_map_ramdisk.param = PLX_LBP_PARAM_BAR23;

	dev_dbg(&xdev->pdev->dev, "%s entering\n", __func__);

	if (xdev->dma_ch
#ifdef FORCE_USE_MEMCPY
        && 0
#endif
	) {
		dev_dbg(&xdev->pdev->dev, "%s: LBP Copy image by DMA \n", __func__);
	} else {
		dev_warn(&xdev->pdev->dev, "%s: LBP Copy image by MEMCPY \n", __func__);
	}

	if (xdev->a_lut) {
		plx_a_lut_peer_enable(xdev);
	}

	if (plx_lbp_get_i7_status(xdev).ready != PLX_LBP_i7_READY) {
		dev_err(&xdev->pdev->dev, "%s card not ready \n", __func__);
		err = -LBP_INTERNAL_ERROR;
		goto exit_no_mem;
	}

	/* prepare intermediate buffer to be used as DMA source */
	temp_buff_size = LBP_DMA_BUFFER_SIZE;
	temp_buff_pages_order = get_order(temp_buff_size);
	temp_buff= (void *)__get_free_pages(GFP_KERNEL | GFP_DMA, temp_buff_pages_order);
	if (!temp_buff) {
		/* Try allocate Once page */
		temp_buff_size = PAGE_SIZE;
		temp_buff_pages_order = get_order(temp_buff_size);
		dev_info(&xdev->pdev->dev, "%s temp buffer switching to smaller size %d", __func__, temp_buff_size);
		temp_buff= (void *)__get_free_pages(GFP_KERNEL | GFP_DMA, temp_buff_pages_order);
	}

	if (!temp_buff) {
		dev_err(&xdev->pdev->dev, "%s: cannot alloc temporary buffer\n", __func__);
		err = -LBP_INTERNAL_ERROR;
		goto exit_no_mem;
	}

	if (xdev->dma_ch
#ifdef FORCE_USE_MEMCPY
        && 0
#endif
        ){
		temp_buff_da = dma_map_single(xdev->dma_ch->device->dev,
				temp_buff, temp_buff_size, DMA_TO_DEVICE);
		if ((err = dma_mapping_error(xdev->dma_ch->device->dev, temp_buff_da))) {
			dev_err(&xdev->pdev->dev, "%s: cannot DMA mapping temporary buffer\n", __func__);
			temp_buff_da = 0;
			err = -LBP_INTERNAL_ERROR;
			goto exit;
		}
	}

	/* write allocation size to DATA spad - size is represented in
	 * 1024*1024 bytes */
	alloc_size = DIV_ROUND_UP(img_size, 1024*1024);
	plx_write_spad(xdev, PLX_LBP_SPAD_DATA_LOW, alloc_size);
	plx_write_spad(xdev, PLX_LBP_SPAD_DATA_HIGH, 0);

	/* issue MAP_RAMDISK command */
	plx_write_spad(xdev, PLX_LBP_SPAD_i7_CMD, cmd_map_ramdisk.value);

	/* wait for the card */
	err = plx_lbp_wait_for_i7_state(xdev,
			PLX_LBP_i7_DONE | PLX_LBP_i7_ANY_ERROR,
			xdev->lbp.parameters.i7_alloc_timeout_ms);
	if (err < 0) {
		dev_err(&xdev->pdev->dev, "%s error during ramdisk mapping:" \
			       " %d card ready: %x\n", __func__, err,
			       plx_lbp_get_i7_status(xdev).value);
		err = -LBP_ALLOC_TIMEOUT;
		goto exit;
	}

	if (err & PLX_LBP_i7_ANY_ERROR)
		goto exit;

	/* read ramdisk address */
	ramdisk_ph = plx_read_spad(xdev, PLX_LBP_SPAD_DATA_HIGH);
	ramdisk_ph <<= 32ULL;
	ramdisk_ph += plx_read_spad(xdev, PLX_LBP_SPAD_DATA_LOW);

	/* copy image to ramdisk */
	offset = 0;
	chunk_size = temp_buff_size;
	while(offset < img_size) {

		if(img_size - offset < chunk_size) {
			chunk_size = (img_size - offset);
		}

		/* copy chunk of the image to intermediate buffer */
		if(copy_from_user(temp_buff, img + offset, chunk_size)) {
			dev_err(&xdev->pdev->dev, "%s copy_from_user failed! \n", __func__);
			err = -LBP_INTERNAL_ERROR;
			goto exit;
		}
		wmb();

		/* recalculate ramdisk address inside of NTB aperture or remap a-lut */
		remapped = plx_ioremap(xdev, ramdisk_ph + offset, chunk_size);
		if (!remapped) {
			dev_err(&xdev->pdev->dev, "%s: ioremap failed!\n", __func__);
			err = -LBP_INTERNAL_ERROR;
			goto exit;
		}
		chunk_dst = (u64)xdev->aper.pa + (remapped - xdev->aper.va);
		dev_dbg(&xdev->pdev->dev, "%s: remapped %p, chunk_dst %llx, ramdisk_ph "
			"%llx, offset %llx, temp_buff %p, chunk_size %x\n", __func__,
			remapped, chunk_dst, ramdisk_ph, offset, temp_buff, chunk_size);

		/* Copy chunk of the image from intermediate buffer to ramdisk */
		err = -1;

		if (xdev->dma_ch
#ifdef FORCE_USE_MEMCPY
				&& 0
#endif
		) {
			/* DMA path*/
			dev_dbg(&xdev->pdev->dev, "%s: Copy chunk by DMA \n", __func__);
			err = vca_lbp_sync_dma(xdev, chunk_dst, temp_buff_da, chunk_size);
		}
		if (err) {
			/* MEMCPY path*/
			dev_dbg(&xdev->pdev->dev, "%s: Copy chunk by MEMCPY \n", __func__);
			memcpy_toio(remapped, temp_buff, chunk_size);
			wmb();
			ioread8((void *)((uintptr_t)remapped + chunk_size - 1));
			err = 0;
		}

		/* unmap a-lut */
		plx_iounmap(xdev, remapped);
		remapped = NULL;

		if (err) {
			dev_err(&xdev->pdev->dev, "%s: DMA to ramdisk failed!\n", __func__);
			err = -LBP_INTERNAL_ERROR;
			goto exit;
		}
		offset += chunk_size;
	}

exit:
	if (xdev->dma_ch && temp_buff_da) {
		dma_unmap_single(xdev->dma_ch->device->dev,
					temp_buff_da, temp_buff_size,
					DMA_TO_DEVICE);
		temp_buff_da = 0;
	}
	free_pages((unsigned long)temp_buff, temp_buff_pages_order);

exit_no_mem:
	if (err < 0)
		return -err;

	if (err & PLX_LBP_i7_ANY_ERROR) {
		i7_error = plx_read_spad(xdev, PLX_LBP_SPAD_i7_ERROR);
		/* TODO: error reporting */
		dev_err(&xdev->pdev->dev, "%s: card error: %x card ready %x\n",
				__func__, i7_error, err);
		return LBP_INTERNAL_ERROR;
	}

	return LBP_STATE_OK;
}

#define OLD_BIOS 1
#ifndef OLD_BIOS
static enum vca_lbp_retval plx_lbp_unmap_ramdisk(struct plx_device *xdev)
{
	int err;
	u32 i7_error;
	struct plx_lbp_i7_cmd cmd_unmap_ramdisk;
	cmd_unmap_ramdisk.cmd = PLX_LBP_CMD_UNMAP_RAMDISK;
	cmd_unmap_ramdisk.param = PLX_LBP_PARAM_BAR23;

	/* 1] request card to unmap ramdisk */
	plx_write_spad(xdev, PLX_LBP_SPAD_i7_CMD, cmd_unmap_ramdisk.value);

	/* 2] wait for the card to acknowledge */
	err = plx_lbp_wait_for_i7_state(xdev,
			PLX_LBP_i7_READY | PLX_LBP_i7_ANY_ERROR,
			xdev->lbp.parameters.i7_cmd_timeout_ms);
	if (err < 0) {
		dev_err(&xdev->pdev->dev, "%s error during unmapping ramdisk: " \
				"%d\n", __func__, err);
		return LBP_CMD_TIMEOUT;
	}

	if (err & PLX_LBP_i7_ANY_ERROR) {
		i7_error = plx_read_spad(xdev, PLX_LBP_SPAD_i7_ERROR);
		/* TODO: error reporting */
		dev_err(&xdev->pdev->dev, "%s card error: %x card ready %x\n",
				__func__, i7_error, err);
		return LBP_INTERNAL_ERROR;
	}

	return LBP_STATE_OK;
}
#else
#endif

#if PLX_USE_CPU_DMA
bool plx_request_dma_chan(struct plx_device *xdev);
#endif

enum vca_lbp_retval plx_lbp_boot_ramdisk(struct plx_device *xdev,
 void __user * img, size_t img_size)
{
	int err;
	u32 i7_error;
	struct plx_lbp_i7_cmd cmd_boot_ramdisk;
#if PLX_USE_CPU_DMA
	if( !xdev->dma_ch)
		plx_request_dma_chan( xdev);
#endif
	if (plx_program_rid_lut_for_node(xdev)) {
		dev_err(&xdev->pdev->dev, "%s: could not program plx rid lut for node\n", __func__);
		return LBP_INTERNAL_ERROR;
	}
	cmd_boot_ramdisk.cmd = PLX_LBP_CMD_BOOT_RAMDISK;
	cmd_boot_ramdisk.param = 0;

	mutex_lock(xdev->lbp_lock);
	dev_dbg(&xdev->pdev->dev, "%s entering\n", __func__);

	/* 1] copy image to ramdisk */
	err = plx_lbp_send_ramdisk(xdev, img, img_size);
	if (err != LBP_STATE_OK) {
		mutex_unlock(xdev->lbp_lock);
		return (enum vca_lbp_retval)err;
	}

	/* 2] request card to start booting */
	plx_write_spad(xdev, PLX_LBP_SPAD_i7_CMD, cmd_boot_ramdisk.value);

	/* 3] wait for the card to acknowledge */
	err = plx_lbp_wait_for_i7_state(xdev,
			PLX_LBP_i7_BOOTING | PLX_LBP_i7_OS_READY | PLX_LBP_i7_ANY_ERROR,
			xdev->lbp.parameters.i7_cmd_timeout_ms);
	if (err < 0) {
		dev_err(&xdev->pdev->dev, "%s error during ramdisk booting: " \
				"%d\n", __func__, err);
		err = -LBP_CMD_TIMEOUT;
		goto exit;
	}
	if (err & PLX_LBP_i7_ANY_ERROR)
		goto exit;

exit:
	if (err < 0) {
		mutex_unlock(xdev->lbp_lock);
		return -err;
	}

	if (err & PLX_LBP_i7_ANY_ERROR) {
		i7_error = plx_read_spad(xdev, PLX_LBP_SPAD_i7_ERROR);
		/* TODO: error reporting */
		dev_err(&xdev->pdev->dev, "%s card error: %x card ready %x\n",
				__func__, i7_error, err);
		mutex_unlock(xdev->lbp_lock);
		return LBP_INTERNAL_ERROR;
	}

	mutex_unlock(xdev->lbp_lock);
	return LBP_STATE_OK;
}

static void plx_lbp_blkio_set_devpage(struct plx_device *xdev)
{
	u64 blkio_dp_ph;
	struct vca_bootparam *bootparam;
	/* read dev_page address */
	blkio_dp_ph = plx_read_spad(xdev, PLX_LBP_SPAD_DATA_HIGH);
	blkio_dp_ph <<= 32ULL;
	blkio_dp_ph += plx_read_spad(xdev, PLX_LBP_SPAD_DATA_LOW);

	bootparam = xdev->dp;
	bootparam->blockio_dp = blkio_dp_ph;

	/* Send doorbell to local backend instance of blockio */
	plx_mmio_write(&xdev->mmio, DB_TO_MASK(bootparam->blockio_ftb_db),
			xdev->reg_base + xdev->intr_reg_base + PLX_DBIS);
}

enum vca_lbp_retval plx_lbp_boot_blkdisk(struct plx_device *xdev)
{
	int err;
	u32 i7_error;
	struct plx_lbp_i7_cmd cmd_boot_blkdisk;
#if PLX_USE_CPU_DMA
	if( !xdev->dma_ch)
		plx_request_dma_chan( xdev);
#endif
	if (plx_program_rid_lut_for_node(xdev)) {
		dev_err(&xdev->pdev->dev, "%s: could not program plx rid lut for node\n", __func__);
		return LBP_INTERNAL_ERROR;
	}
	cmd_boot_blkdisk.cmd = PLX_LBP_CMD_BOOT_BLOCK_IO;
	cmd_boot_blkdisk.param = 0;

	mutex_lock(xdev->lbp_lock);
	dev_dbg(&xdev->pdev->dev, "%s entering\n", __func__);

	if (xdev->a_lut) {
		plx_a_lut_peer_enable(xdev);
	}

	if (plx_lbp_get_i7_status(xdev).ready != PLX_LBP_i7_READY) {
		dev_err(&xdev->pdev->dev, "%s card not ready \n", __func__);
		err = -LBP_INTERNAL_ERROR;
		mutex_unlock(xdev->lbp_lock);
		return (enum vca_lbp_retval)err;
	}


	/* 2] request card to start booting */
	plx_write_spad(xdev, PLX_LBP_SPAD_i7_CMD, cmd_boot_blkdisk.value);

	/* 3] wait for the card to acknowledge */
	err = plx_lbp_wait_for_i7_state(xdev,
			PLX_LBP_i7_BOOTING_BLOCK_IO | PLX_LBP_i7_OS_READY | PLX_LBP_i7_ANY_ERROR,
			xdev->lbp.parameters.i7_cmd_timeout_ms);
	if (err < 0) {
		dev_err(&xdev->pdev->dev, "%s error during blkdisk booting: " \
				"%d\n", __func__, err);
		err = -LBP_CMD_TIMEOUT;
		goto exit;
	}
	if (err & PLX_LBP_i7_ANY_ERROR)
		goto exit;

exit:
	if (err < 0) {
		mutex_unlock(xdev->lbp_lock);
		return -err;
	}

	if (err & PLX_LBP_i7_ANY_ERROR) {
		i7_error = plx_read_spad(xdev, PLX_LBP_SPAD_i7_ERROR);
		/* TODO: error reporting */
		dev_err(&xdev->pdev->dev, "%s card error: %x card ready %x\n",
				__func__, i7_error, err);
		mutex_unlock(xdev->lbp_lock);
		return LBP_INTERNAL_ERROR;
	}

	plx_lbp_blkio_set_devpage(xdev);

	mutex_unlock(xdev->lbp_lock);
	return LBP_STATE_OK;
}

enum vca_lbp_retval plx_lbp_boot_via_pxe(struct plx_device *xdev)
{
	int err;
	u32 i7_error;
	struct plx_lbp_i7_cmd cmd_boot_blkdisk;

	if (!vcapxe_is_ready_to_boot(xdev->pxe_dev)) {
		dev_err(&xdev->pdev->dev, "%s: PXE boot not activated\n", __func__);
		return LBP_BAD_PARAMETER_VALUE;
	}

	if (plx_program_rid_lut_for_node(xdev)) {
		dev_err(&xdev->pdev->dev, "%s: could not program plx rid lut for node\n", __func__);
		return LBP_INTERNAL_ERROR;
	}
	cmd_boot_blkdisk.cmd = PLX_LBP_CMD_BOOT_PXE;
	cmd_boot_blkdisk.param = 0;

	mutex_lock(xdev->lbp_lock);
	dev_dbg(&xdev->pdev->dev, "%s entering\n", __func__);

	if (xdev->a_lut) {
		plx_a_lut_peer_enable(xdev);
	}

	if (plx_lbp_get_i7_status(xdev).ready != PLX_LBP_i7_READY) {
		dev_err(&xdev->pdev->dev, "%s card not ready \n", __func__);
		err = -LBP_INTERNAL_ERROR;
		mutex_unlock(xdev->lbp_lock);
		return (enum vca_lbp_retval)err;
	}


	/* 2] request card to start booting */
	plx_write_spad(xdev, PLX_LBP_SPAD_i7_CMD, cmd_boot_blkdisk.value);

	/* 3] wait for the card to acknowledge */
	err = plx_lbp_wait_for_i7_state(xdev,
			PLX_LBP_i7_BOOTING_PXE | PLX_LBP_i7_OS_READY | PLX_LBP_i7_ANY_ERROR,
			xdev->lbp.parameters.i7_cmd_timeout_ms);
	if (err < 0) {
		dev_err(&xdev->pdev->dev, "%s error during PXE booting: " \
				"%d\n", __func__, err);
		err = -LBP_CMD_TIMEOUT;
		goto exit;
	}
	if (err & PLX_LBP_i7_ANY_ERROR)
		goto exit;

exit:
	if (err < 0) {
		mutex_unlock(xdev->lbp_lock);
		return -err;
	}

	if (err & PLX_LBP_i7_ANY_ERROR) {
		i7_error = plx_read_spad(xdev, PLX_LBP_SPAD_i7_ERROR);
		/* TODO: error reporting */
		dev_err(&xdev->pdev->dev, "%s card error: %x card ready %x\n",
				__func__, i7_error, err);
		mutex_unlock(xdev->lbp_lock);
		return LBP_INTERNAL_ERROR;
	}

	vcapxe_go(xdev->pxe_dev);

	mutex_unlock(xdev->lbp_lock);
	return LBP_STATE_OK;
}

static enum vca_lbp_retval plx_lbp_flash(struct plx_device *xdev,
 struct plx_lbp_i7_cmd cmd, void __user *file, size_t file_size,
 enum plx_lbp_flash_type flash_type)
{
	int err;
	u32 i7_error;
	u32 flash_timeout;

	if (cmd.cmd == PLX_LBP_CMD_FLASH_FW)
		flash_timeout = xdev->lbp.parameters.i7_mac_write_timeout_ms;
	else
		flash_timeout = xdev->lbp.parameters.i7_cmd_timeout_ms;

	/* 1] copy image to ramdisk */
	err = plx_lbp_send_ramdisk(xdev, file, file_size);
	if (err != LBP_STATE_OK)
		return (enum vca_lbp_retval)err;

	/* 2] request card to start flashing */
	plx_write_spad(xdev, PLX_LBP_SPAD_i7_CMD, cmd.value);

	/* 3] wait for the card to start flashing */
	err = plx_lbp_wait_for_i7_state(xdev,
		PLX_LBP_i7_FLASHING | PLX_LBP_i7_ANY_ERROR,
		xdev->lbp.parameters.i7_cmd_timeout_ms);
	if (err < 0) {
		dev_err(&xdev->pdev->dev, "%s error before %s: " \
			"%d\n", __func__,
			plx_lbp_flash_type_to_string(xdev, flash_type), err);
		err = -LBP_CMD_TIMEOUT;
		goto exit;
	}
	if (err & PLX_LBP_i7_ANY_ERROR)
		goto exit;

	/* 4] wait for the card to acknowledge */
	err = plx_lbp_wait_for_i7_state(xdev,
			PLX_LBP_i7_DONE | PLX_LBP_i7_ANY_ERROR,
#if OLD_BIOS
			300000); /* can take long, now about 10 minutes,
						but need measurements */
#else
			flash_timeout);
#endif

	if (err < 0) {
		dev_err(&xdev->pdev->dev, "%s error during %s: " \
			"%d\n", __func__,
			plx_lbp_flash_type_to_string(xdev, flash_type), err);
		err = -LBP_CMD_TIMEOUT;
		goto exit;
	}
	if (err & PLX_LBP_i7_ANY_ERROR)
		goto exit;

#if OLD_BIOS
	/* right now we can only reset via vcactrl */
#else
	/* should be done when BIOS will support it*/

	/* 5] unmap ramdisk */
	err = plx_lbp_unmap_ramdisk(xdev);
	if (err != LBP_STATE_OK)
		return (enum vca_lbp_retval)err;
#endif
exit:
	if (err < 0)
		return -err;

	if (err & PLX_LBP_i7_ANY_ERROR) {
		i7_error = plx_read_spad(xdev, PLX_LBP_SPAD_i7_ERROR);
		/* TODO: error reporting */
		dev_err(&xdev->pdev->dev, "%s card error: %x card ready %x, %s\n",
			__func__, i7_error, err,
			plx_lbp_flash_type_to_string(xdev, flash_type));
		return LBP_INTERNAL_ERROR;
	}

	return LBP_STATE_OK;
}

enum vca_lbp_retval plx_lbp_flash_bios(struct plx_device *xdev,
 void __user * bios_file, size_t bios_file_size)
{
	int ret;
	struct plx_lbp_i7_cmd cmd_flash_bios;
	cmd_flash_bios.cmd = PLX_LBP_CMD_FLASH_BIOS;
	cmd_flash_bios.param = 0;

	mutex_lock(xdev->lbp_lock);
	dev_dbg(&xdev->pdev->dev, "%s entering\n", __func__);

	ret = plx_lbp_flash(xdev, cmd_flash_bios, bios_file, bios_file_size, PLX_LBP_FLASH_BIOS);

	mutex_unlock(xdev->lbp_lock);
	return ret;
}

enum vca_lbp_retval plx_lbp_clear_smb_event_log(struct plx_device *xdev,
 void __user * file, size_t file_size)
{
	int ret;
	struct plx_lbp_i7_cmd cmd_clear_smb_event_log;
	cmd_clear_smb_event_log.cmd = PLX_LBP_CMD_FLASH_BIOS;
	cmd_clear_smb_event_log.param = 0;

	mutex_lock(xdev->lbp_lock);
	dev_dbg(&xdev->pdev->dev, "%s entering\n", __func__);

	ret = plx_lbp_flash(xdev, cmd_clear_smb_event_log, file, file_size, PLX_LBP_FLASH_SMB_EVENTS);

	mutex_unlock(xdev->lbp_lock);
	return ret;
}

enum vca_lbp_retval plx_lbp_update_mac_addr(struct plx_device *xdev,
 void __user * file, size_t file_size)
{
	int ret;
	struct plx_lbp_i7_cmd cmd_update_mac_addr;
	cmd_update_mac_addr.cmd = PLX_LBP_CMD_FLASH_FW;
	cmd_update_mac_addr.param = 0;

	mutex_lock(xdev->lbp_lock);
	dev_dbg(&xdev->pdev->dev, "%s entering\n", __func__);

	ret = plx_lbp_flash(xdev, cmd_update_mac_addr, file, file_size, PLX_LBP_FLASH_MAC);

	mutex_unlock(xdev->lbp_lock);
	return ret;
}

enum vca_lbp_retval plx_lbp_set_serial_number(struct plx_device *xdev,
 void __user * file, size_t file_size)
{
	int ret;
	struct plx_lbp_i7_cmd cmd_update_sn;
	cmd_update_sn.cmd = PLX_LBP_CMD_FLASH_FW;
	cmd_update_sn.param = 0;

	mutex_lock(xdev->lbp_lock);
	dev_dbg(&xdev->pdev->dev, "%s entering\n", __func__);

	ret = plx_lbp_flash(xdev, cmd_update_sn, file, file_size, PLX_LBP_FLASH_SN);

	mutex_unlock(xdev->lbp_lock);
	return ret;
}

enum vca_lbp_retval plx_lbp_boot_from_usb(struct plx_device * xdev)
{
	int err;
	struct plx_lbp_i7_cmd cmd_boot_from_usb;
#if PLX_USE_CPU_DMA
	if( !xdev->dma_ch)
		plx_request_dma_chan( xdev);
#endif
	if (plx_program_rid_lut_for_node(xdev)) {
		dev_err(&xdev->pdev->dev, "%s: could not program plx rid lut for node\n", __func__);
		return LBP_INTERNAL_ERROR;
	}
	cmd_boot_from_usb.cmd = PLX_LBP_CMD_BOOT_LOADER;
	cmd_boot_from_usb.param = 0;
	mutex_lock(xdev->lbp_lock);

	plx_write_spad(xdev, PLX_LBP_SPAD_i7_CMD, cmd_boot_from_usb.value);

	err = plx_lbp_wait_for_i7_state(xdev,
			PLX_LBP_i7_BOOTING | PLX_LBP_i7_ANY_ERROR,
			xdev->lbp.parameters.i7_cmd_timeout_ms);
	if (err < 0) {
		dev_err(&xdev->pdev->dev, "%s error during booting from usb: " \
				"%d\n", __func__, err);
		err = LBP_CMD_TIMEOUT;
	} else {
		err = LBP_STATE_OK;
	}

	mutex_unlock(xdev->lbp_lock);

	return err;
}

enum vca_lbp_retval plx_lbp_flash_firmware(struct plx_device *xdev)
{
	mutex_lock(xdev->lbp_lock);
	dev_err(&xdev->pdev->dev, "%s not implemented\n", __func__);
	mutex_unlock(xdev->lbp_lock);
	return -ENOSYS;
}

enum vca_lbp_retval plx_lbp_set_param(struct plx_device *xdev,
 enum vca_lbp_param param, unsigned int val)
{
	enum vca_lbp_retval ret = LBP_STATE_OK;
	unsigned int min_val;
	unsigned int max_val;

	mutex_lock(xdev->lbp_lock);
	switch(param) {
		case VCA_LBP_PARAM_i7_IRQ_TIMEOUT_MS:
			min_val = 0;
			max_val = 300000;
			break;
		case VCA_LBP_PARAM_i7_CMD_TIMEOUT_MS:
		case VCA_LBP_PARAM_i7_ALLOC_TIMEOUT_MS:
			min_val = 0;
			max_val = 1000;
			break;
		case VCA_LBP_PARAM_i7_MAC_WRITE_TIMEOUT_MS:
			min_val = 0;
			max_val = 10000;
			break;
		default:
			dev_err(&xdev->pdev->dev, "%s unknown param %u\n",
					__func__, param);
			ret = LBP_UNKNOWN_PARAMETER;
			goto exit;
	}

	if (val < min_val || val > max_val) {
		dev_err(&xdev->pdev->dev, "%s parameter %d out of range : %u, " \
			"expected range [%u, %u]\n", __func__, param,
			val, min_val, max_val);
		ret = LBP_BAD_PARAMETER_VALUE;
		goto exit;
	}

	switch(param) {
		case VCA_LBP_PARAM_i7_IRQ_TIMEOUT_MS:
			xdev->lbp.parameters.i7_irq_timeout_ms = val;
			break;
		case VCA_LBP_PARAM_i7_ALLOC_TIMEOUT_MS:
			xdev->lbp.parameters.i7_alloc_timeout_ms = val;
			break;
		case VCA_LBP_PARAM_i7_CMD_TIMEOUT_MS:
			xdev->lbp.parameters.i7_cmd_timeout_ms = val;
		case VCA_LBP_PARAM_i7_MAC_WRITE_TIMEOUT_MS:
			xdev->lbp.parameters.i7_mac_write_timeout_ms = val;
		default:
			goto exit;
	}

exit:
	mutex_unlock(xdev->lbp_lock);
	return ret;
}

enum vca_lbp_states plx_lbp_get_state(struct plx_device *xdev)
{
	int status;
	if (!(xdev->card_type & VCA_PRODUCTION))
		return VCA_OS_READY;

	if(xdev->lbp_resetting)
		return VCA_RESETTING;
	status = plx_lbp_get_i7_status(xdev).ready;
	switch(status) {
	case PLX_LBP_i7_DOWN:
		return VCA_BIOS_DOWN;
	case PLX_LBP_i7_UP:
		return VCA_BIOS_UP;
	case PLX_LBP_i7_READY:
		return VCA_BIOS_READY;
	case PLX_LBP_i7_BUSY:
		return VCA_BUSY;
	case PLX_LBP_i7_BOOTING:
		return VCA_BOOTING;
	case PLX_LBP_i7_FLASHING:
		return VCA_FLASHING;
	case PLX_LBP_i7_DONE:
		return VCA_DONE;
	case PLX_LBP_i7_OS_READY:
		return VCA_OS_READY;
	case PLX_LBP_i7_NET_DEV_READY:
		return VCA_NET_DEV_READY;
	case PLX_LBP_i7_UEFI_ERROR:
	case PLX_LBP_i7_GENERAL_ERROR:
		return VCA_ERROR;
	case PLX_LBP_i7_NET_DEV_UP:
		return VCA_NET_DEV_UP;
	case PLX_LBP_i7_NET_DEV_DOWN:
		return VCA_NET_DEV_DOWN;
	case PLX_LBP_i7_NET_DEV_NO_IP:
		return VCA_NET_DEV_NO_IP;
	case PLX_LBP_i7_DRV_PROBE_DONE:
		return VCA_DRV_PROBE_DONE;
	case PLX_LBP_i7_DRV_PROBE_ERROR:
		return VCA_DRV_PROBE_ERROR;
	case PLX_LBP_i7_DHCP_IN_PROGRESS:
		return VCA_DHCP_IN_PROGRESS;
	case PLX_LBP_i7_DHCP_DONE:
		return VCA_DHCP_DONE;
	case PLX_LBP_i7_DHCP_ERROR:
		return VCA_DHCP_ERROR;
	case PLX_LBP_i7_NFS_MOUNT_DONE:
		return VCA_NFS_MOUNT_DONE;
	case PLX_LBP_i7_NFS_MOUNT_ERROR:
		return VCA_NFS_MOUNT_ERROR;
	case PLX_LBP_i7_SOFTWARE_NODE_DOWN:
		return VCA_LINK_DOWN;
	case PLX_LBP_i7_BOOTING_BLOCK_IO:
		return VCA_BOOTING_BLOCKIO;
	case PLX_LBP_i7_OS_REBOOTING:
		return VCA_OS_REBOOTING;
	case PLX_LBP_i7_UP | PLX_LBP_i7_AFTER_REBOOT:
		return VCA_AFTER_REBOOT;
	case PLX_LBP_i7_POWER_DOWN:
		return VCA_POWER_DOWN;
	// additional status 'net_dev_down' is not suspected value on spad, but it should be included due to
	// BIOS changes. It should be fixed in the future
	case PLX_LBP_i7_POWERING_DOWN | PLX_LBP_i7_NET_DEV_DOWN:
		return VCA_POWERING_DOWN;
	case PLX_LBP_i7_BOOTING_PXE:
		return VCA_BOOTING_PXE;
	default:
		if (status & PLX_LBP_i7_POWER_DOWN) {
			dev_dbg(&xdev->pdev->dev, "%s: detected state '%s', spad value - %d \n",
				__func__, VCA_POWER_DOWN_TEXT, status);
			return VCA_POWER_DOWN;
		}
		dev_dbg( &xdev->pdev->dev, "unknown state %x at %s\n", status, __func__);
	};
	return VCA_ERROR;
}

enum vca_lbp_retval plx_lbp_set_state(struct plx_device *xdev, enum vca_lbp_states state)
{
	struct plx_lbp_i7_ready i7_state;
	if (!(xdev->card_type & VCA_PRODUCTION))
		return LBP_STATE_OK;

	if(xdev->lbp_resetting)
		return LBP_INTERNAL_ERROR;
	i7_state = plx_lbp_get_i7_status(xdev);

	switch (state)
	{
	case VCA_BIOS_DOWN:
		i7_state.ready = PLX_LBP_i7_DOWN;
		break;
	case VCA_BIOS_UP:
		i7_state.ready = PLX_LBP_i7_UP;
		break;
	case VCA_BIOS_READY:
		i7_state.ready = PLX_LBP_i7_READY;
		break;
	case VCA_BUSY:
		i7_state.ready = PLX_LBP_i7_BUSY;
		break;
	case VCA_BOOTING:
		i7_state.ready = PLX_LBP_i7_BOOTING;
		break;
	case VCA_FLASHING:
		i7_state.ready = PLX_LBP_i7_FLASHING;
		break;
	case VCA_DONE:
		i7_state.ready = PLX_LBP_i7_DONE;
		break;
	case VCA_OS_READY:
		i7_state.ready = PLX_LBP_i7_OS_READY;
		break;
	case VCA_NET_DEV_READY:
		i7_state.ready = PLX_LBP_i7_NET_DEV_READY;
		break;
	case VCA_ERROR:
		i7_state.ready = PLX_LBP_i7_GENERAL_ERROR;
		break;
	case VCA_NET_DEV_UP:
		i7_state.ready = PLX_LBP_i7_NET_DEV_UP;
		break;
	case VCA_NET_DEV_DOWN:
		i7_state.ready = PLX_LBP_i7_NET_DEV_DOWN;
		break;
	case VCA_NET_DEV_NO_IP:
		i7_state.ready = PLX_LBP_i7_NET_DEV_NO_IP;
		break;
	case VCA_DRV_PROBE_DONE:
		i7_state.ready = PLX_LBP_i7_DRV_PROBE_DONE;
		break;
	case VCA_DRV_PROBE_ERROR:
		i7_state.ready = PLX_LBP_i7_DRV_PROBE_ERROR;
		break;
	case VCA_DHCP_IN_PROGRESS:
		i7_state.ready = PLX_LBP_i7_DHCP_IN_PROGRESS;
		break;
	case VCA_DHCP_DONE:
		i7_state.ready = PLX_LBP_i7_DHCP_DONE;
		break;
	case VCA_DHCP_ERROR:
		i7_state.ready = PLX_LBP_i7_DHCP_ERROR;
		break;
	case VCA_NFS_MOUNT_DONE:
		i7_state.ready = PLX_LBP_i7_NFS_MOUNT_DONE;
		break;
	case VCA_NFS_MOUNT_ERROR:
		i7_state.ready = PLX_LBP_i7_NFS_MOUNT_ERROR;
		break;
	case VCA_BOOTING_BLOCKIO:
		i7_state.ready = PLX_LBP_i7_BOOTING_BLOCK_IO;
		break;
	case VCA_OS_REBOOTING:
		i7_state.ready = PLX_LBP_i7_OS_REBOOTING;
		break;
	case VCA_AFTER_REBOOT:
		i7_state.ready = PLX_LBP_i7_UP | PLX_LBP_i7_AFTER_REBOOT;
		break;
	case VCA_POWER_DOWN:
		i7_state.ready = PLX_LBP_i7_POWER_DOWN;
		break;
	case VCA_POWERING_DOWN:
		i7_state.ready = PLX_LBP_i7_POWERING_DOWN;
		break;
	case VCA_BOOTING_PXE:
		i7_state.ready = PLX_LBP_i7_BOOTING_PXE;
		break;
	default:
		return LBP_BAD_PARAMETER_VALUE;
	};

	plx_lbp_set_i7_status(xdev, i7_state);

	return LBP_STATE_OK;
}

enum vca_lbp_rcvy_states plx_lbp_get_rcvy_state(struct plx_device *xdev)
{
	struct plx_lbp_i7_ready rcvy_state;
	struct plx_lbp_i7_ready i7_ready;

	if (!(xdev->card_type & VCA_PRODUCTION))
		return VCA_RCVY_NON_READABLE;

	if (xdev->lbp_resetting)
		return VCA_RCVY_NON_READABLE;

	i7_ready.value = plx_read_spad(xdev, PLX_LBP_SPAD_i7_READY);
	if (i7_ready.version < PLX_LBP_PROTOCOL_VERSION_0_4) {
		dev_err(&xdev->pdev->dev, "%s protocol version doesn't support" \
			" reading rcvy value, required version: %x.%x"
			", current version on card %x.%x\n", __func__,
			PLX_LBP_PROTOCOL_GET_MAJOR(PLX_LBP_PROTOCOL_VERSION_0_4),
			PLX_LBP_PROTOCOL_GET_MINOR(PLX_LBP_PROTOCOL_VERSION_0_4),
			PLX_LBP_PROTOCOL_GET_MAJOR(i7_ready.version),
			PLX_LBP_PROTOCOL_GET_MINOR(i7_ready.version)
			);
		return VCA_RCVY_NON_READABLE;
	}
	{
	int const state= plx_lbp_get_i7_status(xdev).ready;
	switch( state) {
	case PLX_LBP_i7_UP:
	case PLX_LBP_i7_UP | PLX_LBP_i7_AFTER_REBOOT:
	case PLX_LBP_i7_READY:
		rcvy_state = plx_lbp_get_i7_rcvy_status(xdev);

		if (rcvy_state.value & PLX_GOLD_IMAGE)
			return VCA_RCVY_JUMPER_OPEN;
		else
			return VCA_RCVY_JUMPER_CLOSE;

	default:
		dev_dbg(&xdev->pdev->dev, "%s unknown state %x\n", __func__, state);
		return VCA_RCVY_NON_READABLE;
	};
	}
}

enum vca_lbp_retval plx_lbp_get_mac_addr(struct plx_device *xdev,
 char out_mac_addr[6])
{
	int err;
	struct plx_lbp_i7_ready i7_ready;
	struct plx_lbp_i7_cmd cmd_get_mac_addr;
	cmd_get_mac_addr.cmd = PLX_LBP_CMD_GET_MAC_ADDR;
	cmd_get_mac_addr.param = 0;

	mutex_lock(xdev->lbp_lock);

	i7_ready = plx_lbp_get_i7_status(xdev);
	if (i7_ready.ready != PLX_LBP_i7_READY) {
		dev_err(&xdev->pdev->dev, "%s invalid i7 state: %x\n",
			__func__, i7_ready.ready);
		err = LBP_SPAD_i7_WRONG_STATE;
		goto unlock;
	}

	plx_write_spad(xdev, PLX_LBP_SPAD_i7_CMD, cmd_get_mac_addr.value);
	err = plx_lbp_wait_for_i7_cmd_consumed(xdev,
		xdev->lbp.parameters.i7_cmd_timeout_ms);
	if (err < 0) {
		dev_err(&xdev->pdev->dev, "%s error waiting for card: " \
				"%d\n", __func__, err);
		err = LBP_CMD_TIMEOUT;
		goto unlock;
	}

	err = plx_lbp_wait_for_i7_state(xdev,
			PLX_LBP_i7_READY,
			xdev->lbp.parameters.i7_cmd_timeout_ms);
	if (err < 0) {
		dev_err(&xdev->pdev->dev, "%s error waiting for card ready: " \
			"%d\n", __func__, err);
		err = LBP_CMD_TIMEOUT;
		goto unlock;
	}

	*(u32*)&out_mac_addr[0] = plx_read_spad(xdev, PLX_LBP_SPAD_DATA_HIGH);
	*(u16*)&out_mac_addr[4] = (u16)plx_read_spad(xdev, PLX_LBP_SPAD_DATA_LOW);

	/* TODO: check if MAC address is 0 and return error if so */

	err = LBP_STATE_OK;
unlock:
	mutex_unlock(xdev->lbp_lock);

	return err;
}

enum vca_lbp_retval plx_lbp_set_time(struct plx_device *xdev)
{
	int err = 0;
	ktime_t t;
	struct rtc_time curr_tm;
	struct plx_lbp_time time;
	struct plx_lbp_timezone timezone;
	struct plx_lbp_i7_ready i7_ready;
	struct plx_lbp_i7_cmd cmd_set_time;
	cmd_set_time.cmd = PLX_LBP_CMD_SET_TIME;
	cmd_set_time.param = 0;

	mutex_lock(xdev->lbp_lock);

	i7_ready = plx_lbp_get_i7_status(xdev);
	if (i7_ready.ready != PLX_LBP_i7_READY) {
		dev_err(&xdev->pdev->dev, "%s invalid i7 state: %x\n",
			__func__, i7_ready.ready);
		err = LBP_SPAD_i7_WRONG_STATE;
		goto unlock;
	}

	timezone.minuteswest = sys_tz.tz_minuteswest;
	cmd_set_time.param = *(u16*)&timezone;

	t = ktime_get_real();
	curr_tm = rtc_ktime_to_tm(t);
	time.year = 1900 + curr_tm.tm_year;
	time.month = 1 + curr_tm.tm_mon;
	time.day = curr_tm.tm_mday;
	time.hour = curr_tm.tm_hour;
	time.minutes = curr_tm.tm_min;
	time.seconds = curr_tm.tm_sec;
	time.miliseconds = ktime_to_timeval(t).tv_usec / 1000; // sub-second precision is unused elsewhere in code (and dmesg uses microseconds, not nanoseconds). Is it worth the added complexity compared to: 'curr_tm = rtc_ktime_to_tm(ktime_get_real());'?

	dev_dbg(&xdev->pdev->dev, "Time to set is: year %d month %d day %d hour %d "
		"min %d seconds %d miliseconds %d\n",
		time.year, time.month, time.day, time.hour, time.minutes,
		time.seconds, time.miliseconds);

	dev_dbg(&xdev->pdev->dev, "Minutes west to set is: %d\n",
		timezone.minuteswest);

	plx_write_spad(xdev, PLX_LBP_SPAD_DATA_HIGH, (u32)((*(u64*)&time) >> 32));
	plx_write_spad(xdev, PLX_LBP_SPAD_DATA_LOW, *(u32*)&time);

	plx_write_spad(xdev, PLX_LBP_SPAD_i7_CMD, cmd_set_time.value);
	err = plx_lbp_wait_for_i7_cmd_consumed(xdev,
		xdev->lbp.parameters.i7_cmd_timeout_ms);
	if (err < 0) {
		dev_err(&xdev->pdev->dev, "%s error waiting for card: " \
				"%d\n", __func__, err);
		err = LBP_CMD_TIMEOUT;
		goto unlock;
	}

	err = plx_lbp_wait_for_i7_state(xdev,
			PLX_LBP_i7_READY,
			xdev->lbp.parameters.i7_cmd_timeout_ms);
	if (err < 0) {
		dev_err(&xdev->pdev->dev, "%s error waiting for card ready: " \
			"%d\n", __func__, err);
		err = LBP_CMD_TIMEOUT;
		goto unlock;
	}

	err = LBP_STATE_OK;
unlock:
	mutex_unlock(xdev->lbp_lock);

	return err;
}

int plx_lbp_set_time_no_mutex(struct plx_device *xdev)
{
	int err = LBP_STATE_OK;
	ktime_t t;
	struct rtc_time curr_tm;
	struct plx_lbp_time time;
	struct plx_lbp_timezone timezone;
	struct plx_lbp_i7_ready i7_ready;
	struct plx_lbp_i7_cmd cmd_set_time;
	cmd_set_time.cmd = PLX_LBP_CMD_SET_TIME;
	cmd_set_time.param = 0;

	i7_ready = plx_lbp_get_i7_status(xdev);
	if (i7_ready.ready != PLX_LBP_i7_READY) {
		dev_err(&xdev->pdev->dev, "%s invalid i7 state: %x\n",
			__func__, i7_ready.ready);
		err = LBP_SPAD_i7_WRONG_STATE;
		goto exit;
	}

	timezone.minuteswest = sys_tz.tz_minuteswest;
	cmd_set_time.param = *(u16*)&timezone;

	t = ktime_get_real();
	curr_tm = rtc_ktime_to_tm(t);
	time.year = 1900 + curr_tm.tm_year;
	time.month = 1 + curr_tm.tm_mon;
	time.day = curr_tm.tm_mday;
	time.hour = curr_tm.tm_hour;
	time.minutes = curr_tm.tm_min;
	time.seconds = curr_tm.tm_sec;
	time.miliseconds = ktime_to_timeval(t).tv_usec / 1000; // DEAR REVIEWERS: sub-second precision is unused elsewhere in code. Is it worth the added complexity compared to: 'curr_tm = rtc_ktime_to_tm(ktime_get_real());'?

	dev_dbg(&xdev->pdev->dev, "Time to set is: year %d month %d day %d hour %d "
		"min %d seconds %d miliseconds %d\n",
		time.year, time.month, time.day, time.hour, time.minutes,
		time.seconds, time.miliseconds);

	dev_dbg(&xdev->pdev->dev, "Minutes west to set is: %d\n",
		timezone.minuteswest);

	plx_write_spad(xdev, PLX_LBP_SPAD_DATA_HIGH, (u32)((*(u64*)&time) >> 32));
	plx_write_spad(xdev, PLX_LBP_SPAD_DATA_LOW, *(u32*)&time);

	plx_write_spad(xdev, PLX_LBP_SPAD_i7_CMD, cmd_set_time.value);

	err = plx_lbp_wait_for_i7_cmd_consumed(xdev,
		xdev->lbp.parameters.i7_cmd_timeout_ms);
	if (err < 0) {
		dev_err(&xdev->pdev->dev, "%s error waiting for card: " \
			"%d\n", __func__, err);
		err = LBP_CMD_TIMEOUT;
		goto exit;
	}
exit:
	return err;
}

bool plx_lbp_clear_error(struct plx_device *xdev)
{
	int err = 0;
	struct plx_lbp_i7_cmd cmd_clear_error;
	cmd_clear_error.cmd = PLX_LBP_CMD_CLEAR_ERROR;
	cmd_clear_error.param = 0;

	plx_write_spad(xdev, PLX_LBP_SPAD_i7_ERROR, PLX_LBP_ERROR_NO_ERROR);

	err = plx_lbp_wait_for_i7_state(xdev,
			PLX_LBP_i7_READY,
			xdev->lbp.parameters.i7_cmd_timeout_ms);
	if (err < 0) {
		dev_err(&xdev->pdev->dev, "%s error waiting for card ready: " \
			"%d\n", __func__, err);
		return false;
	}

	plx_write_spad(xdev, PLX_LBP_SPAD_i7_CMD, cmd_clear_error.value);
	err = plx_lbp_wait_for_i7_cmd_consumed(xdev,
		xdev->lbp.parameters.i7_cmd_timeout_ms);
	if (err < 0) {
		dev_err(&xdev->pdev->dev, "%s error waiting for card: " \
				"%d\n", __func__, err);
		return false;
	}
	return true;
}

static const char * plx_lbp_param_to_string(enum PLX_LBP_PARAM param)
{
	switch (param) {
	case PLX_LBP_PARAM_BIOS_BUILD_DATE:
		return "BIOS BUILD_DATE";
	case PLX_LBP_PARAM_BIOS_VER:
		return "BIOS VERSION";
	case PLX_LBP_PARAM_CPU_MAX_FREQ_NON_TURBO:
		return "CPU_MAX_FREQ_NON_TURBO";
	case PLX_LBP_PARAM_SGX:
		return "SGX";
	case PLX_LBP_PARAM_EPOCH_0:
		return "EPOCH0";
	case PLX_LBP_PARAM_EPOCH_1:
		return "EPOCH1";
	case PLX_LBP_PARAM_SGX_OWNER_EPOCH_TYPE:
		return "EPOCH_TYPE";
	case PLX_LBP_PARAM_SGX_MEM:
		return "PRM";
	case PLX_LBP_PARAM_GPU_APERTURE:
		return "GPU_APERTURE";
	case PLX_LBP_PARAM_TDP:
		return "TDP";
	case PLX_LBP_PARAM_HT:
		return "HT";
	case PLX_LBP_PARAM_GPU:
		return "GPU";
	default:
		return "UNKNOWN PARAM";
	};
}

static bool check_if_supported(struct plx_device *xdev,
 struct plx_lbp_i7_ready i7_ready, enum PLX_LBP_PARAM param,
 unsigned int protocol_version)
{
	if (i7_ready.version < protocol_version) {
		dev_err(&xdev->pdev->dev, "%s parameter %s need protocol version %x.%x "
			"or higher, card is %x.%x\n",
			__func__,
			plx_lbp_param_to_string(param),
			PLX_LBP_PROTOCOL_GET_MAJOR(protocol_version),
			PLX_LBP_PROTOCOL_GET_MINOR(protocol_version),
			PLX_LBP_PROTOCOL_GET_MAJOR(i7_ready.version),
			PLX_LBP_PROTOCOL_GET_MINOR(i7_ready.version)
			);
		return false;
	}
	return true;
}

#define str(s) #s
int plx_lbp_copy_bios_info(struct plx_device *xdev, enum PLX_LBP_PARAM param, u64 * dest)
 {
	 int err = 0;
	 struct plx_lbp_i7_cmd cmd_get_param;
	 struct plx_lbp_i7_ready i7_ready;
	 unsigned int input_value;

	 i7_ready.value = plx_read_spad(xdev, PLX_LBP_SPAD_i7_READY);
	 cmd_get_param.cmd = PLX_LBP_CMD_GET_PARAM;

	 switch (param) {
	 case PLX_LBP_PARAM_BIOS_BUILD_DATE:
	 case PLX_LBP_PARAM_BIOS_VER:
		 if (!check_if_supported(xdev, i7_ready, param, PLX_LBP_PROTOCOL_VERSION_1_1)) {
			 err = LBP_PROTOCOL_VERSION_MISMATCH;
			 goto exit;
		 }
		 break;
	 case PLX_LBP_PARAM_CPU_MAX_FREQ_NON_TURBO:
		 if (!check_if_supported(xdev, i7_ready, param, PLX_LBP_PROTOCOL_VERSION_0_2)) {
			 err = LBP_PROTOCOL_VERSION_MISMATCH;
			 goto exit;
		 }
		 break;
	 case PLX_LBP_PARAM_SGX:
	 case PLX_LBP_PARAM_SGX_MEM:
	 case PLX_LBP_PARAM_SGX_OWNER_EPOCH_TYPE:
	 case PLX_LBP_PARAM_EPOCH_0:
	 case PLX_LBP_PARAM_EPOCH_1:
	 case PLX_LBP_PARAM_SGX_TO_FACTORY:
		 if (!check_if_supported(xdev, i7_ready, param, PLX_LBP_PROTOCOL_VERSION_1_2)) {
			 err = LBP_PROTOCOL_VERSION_MISMATCH;
			 goto exit;
		 }
		 break;
	 case PLX_LBP_PARAM_HT:
		 if (!check_if_supported(xdev, i7_ready, param, PLX_LBP_PROTOCOL_VERSION_2_0)) {
			 err = LBP_PROTOCOL_VERSION_MISMATCH;
			 goto exit;
		 }
		 break;
	 case PLX_LBP_PARAM_GPU:
		 if (!check_if_supported(xdev, i7_ready, param, PLX_LBP_PROTOCOL_VERSION_2_0)) {
			 err = LBP_PROTOCOL_VERSION_MISMATCH;
			 goto exit;
		 }
		 break;
	 case PLX_LBP_PARAM_GPU_APERTURE:
		 if (!check_if_supported(xdev, i7_ready, param, PLX_LBP_PROTOCOL_VERSION_1_2)) {
			 err = LBP_PROTOCOL_VERSION_MISMATCH;
			 goto exit;
		 }
		 break;
	 case PLX_LBP_PARAM_TDP:
		 if (!check_if_supported(xdev, i7_ready, param, PLX_LBP_PROTOCOL_VERSION_1_2)) {
			 err = LBP_PROTOCOL_VERSION_MISMATCH;
			 goto exit;
		 }
		 break;
	 default:
		 dev_err(&xdev->pdev->dev, "%s unknown parameter %u\n",
			 __func__, param);
		 err = -LBP_UNKNOWN_PARAMETER;
		 goto exit;
	 };

	 cmd_get_param.param = param;
	 plx_write_spad(xdev, PLX_LBP_SPAD_i7_CMD, cmd_get_param.value);
	 err = plx_lbp_wait_for_i7_cmd_consumed(xdev,
		 xdev->lbp.parameters.i7_cmd_timeout_ms);

	 if (err < 0) {
		 dev_err(&xdev->pdev->dev, "%s error waiting for card: " \
			 "%d\n", __func__, err);
		 err = LBP_CMD_TIMEOUT;
		 goto exit;
	 }

	 input_value = *dest;
	 ((u32*)dest)[0] = plx_read_spad(xdev, PLX_LBP_SPAD_DATA_LOW);
	 ((u32*)dest)[1] = (PLX_LBP_PARAM_CPU_MAX_FREQ_NON_TURBO != param) ? plx_read_spad(xdev, PLX_LBP_SPAD_DATA_HIGH) : 0;

	 if ((PLX_LBP_PARAM_CPU_MAX_FREQ_NON_TURBO == param) && (PLX_LBP_PROTOCOL_VERSION_0_2 >= i7_ready.version)) {
		 /* in case when input value is equal to 0, which should start turbo,
		 but for protocol version 2 turbo is being enabled on 17, so if 17
		 is actually in bios spads, then no changes required */
		 if (input_value == 0 && *dest == 17) {
			 *dest = input_value;
			 goto exit;
		 }
		 if (*dest < PLX_LBP_PARAM_VERSION_0_2_CPU_MAX_FREQ_NON_TURBO_MIN_VAL ||
			 *dest > PLX_LBP_PARAM_VERSION_0_2_CPU_MAX_FREQ_NON_TURBO_MAX_VAL) {
			 err = LBP_BAD_PARAMETER_VALUE;
			 goto exit;
		 }
	 }
	 //call this function only to set correct status(GOLD BIOS) into registers by BIOS
	 err = plx_lbp_set_time_no_mutex(xdev);
 exit:
	 return err;
}

int plx_lbp_copy_bios_info_from_cache(struct plx_device *xdev,
  enum PLX_LBP_PARAM param, u64 * dest)
 {
	 dev_dbg(&xdev->pdev->dev, "BIOS info from cache");
	 if (param == PLX_LBP_PARAM_BIOS_BUILD_DATE &&
		 xdev->bios_information_date != 0) {
		 *dest = xdev->bios_information_date;
		 return 0;
	 }
	 else if (param == PLX_LBP_PARAM_BIOS_VER &&
		 xdev->bios_information_version != 0) {
		 *dest = xdev->bios_information_version;
		 return 0;
	 }
	 else if (param == PLX_LBP_PARAM_SGX &&
		 xdev->bios_cfg_sgx != -1) {
		 *dest = xdev->bios_cfg_sgx;
		 return 0;
	 }
	 else if (param == PLX_LBP_PARAM_HT &&
		 xdev->bios_cfg_hyper_threading != -1) {
		 *dest = xdev->bios_cfg_hyper_threading;
		 return 0;
	 }
	 else if (param == PLX_LBP_PARAM_GPU &&
		 xdev->bios_cfg_gpu != -1) {
		 *dest = xdev->bios_cfg_gpu;
		 return 0;
	 }
	 else if (param == PLX_LBP_PARAM_GPU_APERTURE &&
		 xdev->bios_cfg_gpu_aperture != -1) {
		 *dest = xdev->bios_cfg_gpu_aperture;
		 return 0;
	 }
	 else if (param == PLX_LBP_PARAM_TDP &&
		 xdev->bios_cfg_tdp != -1) {
		 *dest = xdev->bios_cfg_tdp;
		 return 0;
	 }
	 else {
		 dev_err(&xdev->pdev->dev, "Can't read data from cache \n");
		 return LBP_BIOS_INFO_CACHE_EMPTY;
	 }
}

enum vca_lbp_retval handle_plx_lbp_error(struct plx_device * xdev,
 const char * caller, int err)
{
	u32 i7_error = plx_read_spad(xdev, PLX_LBP_SPAD_i7_ERROR);
	/* TODO: error reporting */
	dev_err(&xdev->pdev->dev, "%s card error: %x card ready %x\n",
		caller, i7_error, err);

	if (!plx_lbp_clear_error(xdev))
		return LBP_CMD_TIMEOUT;
	return LBP_INTERNAL_ERROR;
}

enum vca_lbp_retval plx_lbp_set_bios_param(struct plx_device *xdev,
 enum PLX_LBP_PARAM param, u64 value)
{
	int err = LBP_STATE_OK;
	struct plx_lbp_i7_ready i7_ready;
	u32 i7_error;

	mutex_lock(xdev->lbp_lock);

	i7_ready.value = plx_read_spad(xdev, PLX_LBP_SPAD_i7_READY);

	switch(param) {
	case PLX_LBP_PARAM_CPU_MAX_FREQ_NON_TURBO:
		if (!check_if_supported(xdev, i7_ready, param, PLX_LBP_PROTOCOL_VERSION_0_2)) {
			err = LBP_PROTOCOL_VERSION_MISMATCH;
			goto unlock;
		}
		break;
	case PLX_LBP_PARAM_SGX:
	case PLX_LBP_PARAM_SGX_MEM:
	case PLX_LBP_PARAM_SGX_OWNER_EPOCH_TYPE:
	case PLX_LBP_PARAM_EPOCH_0:
	case PLX_LBP_PARAM_EPOCH_1:
	case PLX_LBP_PARAM_SGX_TO_FACTORY:
		if (!check_if_supported(xdev, i7_ready, param, PLX_LBP_PROTOCOL_VERSION_1_2)) {
			err = LBP_PROTOCOL_VERSION_MISMATCH;
			goto unlock;
		}
		break;
	case PLX_LBP_PARAM_HT:
		if (!check_if_supported(xdev, i7_ready, param, PLX_LBP_PROTOCOL_VERSION_2_0)) {
			err = LBP_PROTOCOL_VERSION_MISMATCH;
			goto unlock;
		}
		break;
	case PLX_LBP_PARAM_GPU:
		if (!check_if_supported(xdev, i7_ready, param, PLX_LBP_PROTOCOL_VERSION_2_0)) {
			err = LBP_PROTOCOL_VERSION_MISMATCH;
			goto unlock;
		}
		break;
	case PLX_LBP_PARAM_GPU_APERTURE:
		if (!check_if_supported(xdev, i7_ready, param, PLX_LBP_PROTOCOL_VERSION_1_2)) {
			err = LBP_PROTOCOL_VERSION_MISMATCH;
			goto unlock;
		}
		break;
	case PLX_LBP_PARAM_TDP:
		if (!check_if_supported(xdev, i7_ready, param, PLX_LBP_PROTOCOL_VERSION_1_2)) {
			err = LBP_PROTOCOL_VERSION_MISMATCH;
			goto unlock;
		}
		break;
	default:
		dev_err(&xdev->pdev->dev, "%s unknown parameter %u\n",
					__func__, param);
			err = -LBP_UNKNOWN_PARAMETER;
		goto exit;
	};

	i7_ready = plx_lbp_get_i7_status(xdev);
	if (i7_ready.ready != PLX_LBP_i7_READY) {
		dev_err(&xdev->pdev->dev, "%s invalid i7 state: %x\n",
			__func__, i7_ready.ready);
		err = LBP_SPAD_i7_WRONG_STATE;
		goto unlock;
	}

	if (param == PLX_LBP_PARAM_CPU_MAX_FREQ_NON_TURBO && value == PLX_LBP_PARAM_CPU_START_TURBO &&
	    PLX_LBP_PROTOCOL_VERSION_0_2 >= i7_ready.version) {
		value = PLX_LBP_PARAM_VERSION_0_2_CPU_START_TURBO;
		dev_err(&xdev->pdev->dev, "%s protocol version is NOT up to date, "
			"current is %x.%x, but card has %x.%x",
			__func__,
			PLX_LBP_PROTOCOL_GET_MAJOR(PLX_LBP_PROTOCOL_VERSION_CURRENT),
			PLX_LBP_PROTOCOL_GET_MINOR(PLX_LBP_PROTOCOL_VERSION_CURRENT),
			PLX_LBP_PROTOCOL_GET_MAJOR(i7_ready.version),
			PLX_LBP_PROTOCOL_GET_MINOR(i7_ready.version));
	}

	plx_write_spad(xdev, PLX_LBP_SPAD_DATA_LOW, ((u32*)&value)[0]);
	plx_write_spad(xdev, PLX_LBP_SPAD_DATA_HIGH, ((u32*)&value)[1]);
	{
		struct plx_lbp_i7_cmd cmd_set_param;
		cmd_set_param.cmd = PLX_LBP_CMD_SET_PARAM;
		cmd_set_param.param = param;
		plx_write_spad(xdev, PLX_LBP_SPAD_i7_CMD, cmd_set_param.value);
	}
	err = plx_lbp_wait_for_i7_cmd_consumed(xdev,
		xdev->lbp.parameters.i7_cmd_timeout_ms);
	if (err < 0) {
		dev_err(&xdev->pdev->dev, "%s error waiting for card: " \
				"%d\n", __func__, err);
		err = LBP_CMD_TIMEOUT;
		goto unlock;
	}

exit:

	if (err < 0) {
		err = -err;
		goto unlock;
	}

	i7_error = plx_read_spad(xdev, PLX_LBP_SPAD_i7_ERROR);
	if(i7_error) {
		/* TODO: error reporting */
		dev_err(&xdev->pdev->dev, "%s card error: %x card ready %x\n",
			__func__, i7_error, err);

		if (!plx_lbp_clear_error(xdev))
			err = LBP_CMD_TIMEOUT;
		else
			err = LBP_INTERNAL_ERROR;
	}

unlock:
	mutex_unlock(xdev->lbp_lock);

	return err;
}

enum vca_lbp_retval plx_lbp_get_bios_param(struct plx_device *xdev,
 enum PLX_LBP_PARAM param, u64 * value)
{
	int err = LBP_STATE_OK;
	struct plx_lbp_i7_ready i7_ready;

	mutex_lock(xdev->lbp_lock);
	i7_ready.value = plx_read_spad(xdev, PLX_LBP_SPAD_i7_READY);

	if (param == PLX_LBP_PARAM_BIOS_BUILD_DATE || param == PLX_LBP_PARAM_BIOS_VER) {
		if (!check_if_supported(xdev, i7_ready, param, PLX_LBP_PROTOCOL_VERSION_1_1)) {//case 1: protcol==0.0, no-communication
			if (i7_ready.major == 0 && i7_ready.minor == 0 ) {
				err = plx_lbp_copy_bios_info_from_cache(xdev, param, value);
				goto unlock;
			}
			err = LBP_PROTOCOL_VERSION_MISMATCH;
			goto unlock;
		}
		i7_ready = plx_lbp_get_i7_status(xdev);
		if ((i7_ready.ready != PLX_LBP_i7_READY) && (i7_ready.ready != PLX_LBP_i7_UP)) {//case 2: protocol ok, incorrect status
			err = plx_lbp_copy_bios_info_from_cache(xdev, param, value);
			goto unlock;
		}
		else//case 3: protocol ok, status ok, load directly from device
			err = plx_lbp_copy_bios_info(xdev, param, value);
	}
	else {
		if(param == PLX_LBP_PARAM_SGX ||
		   param == PLX_LBP_PARAM_SGX_OWNER_EPOCH_TYPE ||
		   param == PLX_LBP_PARAM_EPOCH_0 ||
		   param == PLX_LBP_PARAM_EPOCH_1 ||
		   param == PLX_LBP_PARAM_SGX_TO_FACTORY ||
		   param == PLX_LBP_PARAM_SGX_MEM ||
		   param == PLX_LBP_PARAM_GPU_APERTURE ||
		   param == PLX_LBP_PARAM_TDP ||
		   param == PLX_LBP_PARAM_HT ||
		   param == PLX_LBP_PARAM_GPU) {
		if (!check_if_supported(xdev, i7_ready, param, PLX_LBP_PROTOCOL_VERSION_1_2)) {	//case 1: protcol==0.0, no-communication
			if (i7_ready.major == 0 && i7_ready.minor == 0) {
				err = plx_lbp_copy_bios_info_from_cache(xdev, param, value);
				goto unlock;
			}
			err = LBP_PROTOCOL_VERSION_MISMATCH;
			goto unlock;
		}
		i7_ready = plx_lbp_get_i7_status(xdev);
		if ((i7_ready.ready != PLX_LBP_i7_READY) && (i7_ready.ready != PLX_LBP_i7_UP)) {	//case 2: protocol ok, incorrect status
			err = plx_lbp_copy_bios_info_from_cache(xdev, param, value);
			goto unlock;
		}
		else	//case 3: protocol ok, status ok, load directly from device
			err = plx_lbp_copy_bios_info(xdev, param, value);
	}
	else if(param == PLX_LBP_PARAM_CPU_MAX_FREQ_NON_TURBO) {
		if (i7_ready.ready != PLX_LBP_i7_READY) {
			dev_err(&xdev->pdev->dev, "%s invalid i7 state: %x\n",
				__func__, i7_ready.ready);
			err = LBP_SPAD_i7_WRONG_STATE;
				goto exit;
		}
		err = plx_lbp_copy_bios_info(xdev, param, value);
	}
	else {
		dev_err(&xdev->pdev->dev, "%s unknown parameter %u\n",
					__func__, param);
			err = -LBP_UNKNOWN_PARAMETER;
		goto exit;
	}
	}

exit:

	if (err < 0) {
		err = -err;
		goto unlock;
	}

	if (err & PLX_LBP_i7_ANY_ERROR) {
		u32 i7_error = plx_read_spad(xdev, PLX_LBP_SPAD_i7_ERROR);
		/* TODO: error reporting */
		dev_err(&xdev->pdev->dev, "%s card error: %x card ready %x\n",
			__func__, i7_error, err);

		if (!plx_lbp_clear_error(xdev))
			err = LBP_CMD_TIMEOUT;
		else
			err = LBP_INTERNAL_ERROR;
	}

unlock:
	mutex_unlock(xdev->lbp_lock);

	return err;
}

const char * plx_lbp_flash_type_to_string(struct plx_device *xdev, enum plx_lbp_flash_type flash_type)
{
	switch (flash_type)
	{
	case PLX_LBP_FLASH_BIOS:
		return "flashing bios";
	case PLX_LBP_FLASH_MAC:
		return "flashing mac addr";
	case PLX_LBP_FLASH_SMB_EVENTS:
		return "clearing SMBios event log";
	default:
		dev_err(&xdev->pdev->dev, "%s unknown parameter: %x\n",
			__func__, flash_type);
		return NULL;
	}
}

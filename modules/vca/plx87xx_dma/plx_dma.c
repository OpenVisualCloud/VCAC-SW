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
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/version.h>
#include "plx_dma.h"
#include <linux/vmalloc.h>

#include "../common/vca_common.h"
#include "../plx87xx/plx_intr.h"

#ifdef PLX_DMA_DEBUG
struct plx_debug *plx_dbg;
int plx_dbg_count;
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 0, 0)
#define DMA_MIN_COOKIE	1
#define DMA_MAX_COOKIE	INT_MAX

inline static struct plx_dma_chan *
completed_cookie_container(struct dma_chan *chan)
{
	return to_plx_dma_chan(chan);
}
#else /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0) */
inline static struct dma_chan *
completed_cookie_container(struct dma_chan *chan)
{
	return chan;
}
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0) */

u32 plx_dma_reg_read(struct plx_dma_device *pdma, u32 offset)
{
	return ioread32(pdma->reg_base + offset);
}

void plx_dma_reg_write(struct plx_dma_device *pdma,
		       u32 offset, u32 value)
{
	iowrite32(value, pdma->reg_base + offset);
}

u32 plx_dma_ch_reg_read(struct plx_dma_chan *ch, u32 offset)
{
	return plx_dma_reg_read(to_plx_dma_dev(ch), offset + ch->ch_base_addr);
}

static void plx_dma_ch_reg_write(struct plx_dma_chan *ch,
				 u32 offset, u32 value)
{
	plx_dma_reg_write(to_plx_dma_dev(ch), offset + ch->ch_base_addr, value);
}

static inline u32 plx_dma_ring_inc(u32 val)
{
	return (val + 1) & (PLX_DMA_DESC_RX_SIZE - 1);
}

static u32 desc_ring_da_to_index(struct plx_dma_chan *ch, dma_addr_t addr)
{
	return (addr / sizeof(struct plx_dma_desc)) &
		(PLX_DMA_DESC_RX_SIZE - 1);
}

static inline u32 plx_dma_ring_dec(u32 val)
{
	return val ? val - 1 : PLX_DMA_DESC_RX_SIZE - 1;
}

static inline void plx_dma_inc_head(struct plx_dma_chan *ch)
{
	ch->head = plx_dma_ring_inc(ch->head);
}

static int plx_dma_alloc_desc_ring(struct plx_dma_chan *ch)
{
	/* Desc size must be >= depth of ring + prefetch size + 1 */
	u64 desc_ring_size = PLX_DMA_DESC_RX_SIZE * sizeof(*ch->desc_ring);
	struct device *dev = to_plx_dma_dev(ch)->dma_dev.dev;

	BUILD_BUG_ON(sizeof(*ch->desc_ring) != 16);

	desc_ring_size = ALIGN(desc_ring_size, PLX_DMA_ALIGN_BYTES);
	ch->desc_ring = dma_alloc_coherent(dev, desc_ring_size, &ch->desc_ring_da, GFP_KERNEL);

	if (!ch->desc_ring)
		return -ENOMEM;

	CHECK_DMA_ZONE(dev, ch->desc_ring_da);
	memset(ch->desc_ring, 0, desc_ring_size);

	dev_dbg(dev, "DMA desc ring VA = %p PA 0x%llx size 0x%llx\n",
		ch->desc_ring, ch->desc_ring_da, desc_ring_size);
	ch->tx_array = vzalloc(PLX_DMA_DESC_RX_SIZE * sizeof(*ch->tx_array));
	if (!ch->tx_array)
		goto tx_error;
	return 0;
tx_error:
	dma_free_coherent(dev, desc_ring_size, ch->desc_ring, ch->desc_ring_da);
	return -ENOMEM;
}

/*
 * Clearing desc ring registers seems to get rid of DMA engine hang (HW tail
 * stuck at 0) we sometimes see after unloading/reloading the drivers.
 */
static void plx_dma_chan_clear_desc_ring(struct plx_dma_chan *ch)
{
	plx_dma_ch_reg_write(ch, PLX_DMA_BLK_SRC_LOWER, 0x0);
	plx_dma_ch_reg_write(ch, PLX_DMA_BLK_SRC_UPPER, 0x0);
	plx_dma_ch_reg_write(ch, PLX_DMA_BLK_DST_LOWER, 0x0);
	plx_dma_ch_reg_write(ch, PLX_DMA_BLK_DST_UPPER, 0x0);
	plx_dma_ch_reg_write(ch, PLX_DMA_BLK_TRSFR_SIZE, 0x0);

	plx_dma_ch_reg_write(ch, PLX_DMA_DESC_RING_ADDR_LOW, 0x0);
	plx_dma_ch_reg_write(ch, PLX_DMA_DESC_RING_ADDR_HIGH, 0x0);
	plx_dma_ch_reg_write(ch, PLX_DMA_DESC_RING_SIZE, 0x0);
	plx_dma_ch_reg_write(ch, PLX_DMA_NEXT_DESC_ADDR_LOW, 0x0);
}

static inline void plx_dma_disable_chan(struct plx_dma_chan *ch)
{
	struct device *dev = plx_dma_ch_to_device(ch);
	u32 ctrl_reg = plx_dma_ch_reg_read(ch, PLX_DMA_CTRL_STATUS);
	int i;

	if (!(ctrl_reg & PLX_DMA_CTRL_START))
		goto exit;

	/* set graceful pause bit and clear start bit*/
	ctrl_reg |= PLX_DMA_CTRL_PAUSED;
	ctrl_reg &= ~PLX_DMA_CTRL_START;
	plx_dma_ch_reg_write(ch, PLX_DMA_CTRL_STATUS, ctrl_reg);

	for (i = 0; i < PLX_DMA_PAUSE_TO; i++) {
		ctrl_reg = plx_dma_ch_reg_read(ch, PLX_DMA_CTRL_STATUS);

		if (ctrl_reg & PLX_DMA_CTRL_PAUSE_DONE_STATUS)
			goto exit;

		mdelay(1000);
	}
	dev_err(dev, "%s graceful pause timed out 0x%x head %d tail %d\n",
		__func__, ctrl_reg, ch->head, ch->last_tail);
exit:
	plx_dma_chan_clear_desc_ring(ch);
}

static inline void plx_dma_enable_chan(struct plx_dma_chan *ch)
{
	u32 ctrl_reg = plx_dma_ch_reg_read(ch, PLX_DMA_CTRL_STATUS);

	/* Check if dma hang happen before, if yes,
	 * then hardware will not work correctly until PLX is reset.
	 */
	if (ctrl_reg & PLX_DMA_CTRL_IN_PROGRESS) {
		struct device *dev = plx_dma_ch_to_device(ch);
		dev_err(dev, "%s dma hang detected 0x%x\n", __func__, ctrl_reg);
		ch->dma_hang = true;
	}

	/* Copy bits with configuration [29:13], Source Maximum Transfer Size,
	 * Relaxed, Traffic Class etc.
	 */
	ch->ctrl_reg = ctrl_reg & 0x3FFFE000UL;

	/* Enable Write Back */
	ch->ctrl_reg |= PLX_DMA_CTRL_WB_ENABLE;

	/* Enable SGL off-chip mode */
	ch->ctrl_reg &= ~PLX_DMA_CTRL_MODE_BITS;
	ch->ctrl_reg |= PLX_DMA_CTRL_OFFCHIP_MODE;

	/* clear graceful pause status and pause bit */
	ctrl_reg = ch->ctrl_reg;
	ctrl_reg |= PLX_DMA_CTRL_PAUSE_DONE_STATUS;
	ctrl_reg &= ~PLX_DMA_CTRL_PAUSED;
	plx_dma_ch_reg_write(ch, PLX_DMA_CTRL_STATUS, ctrl_reg);
}

static inline void plx_dma_chan_mask_intr(struct plx_dma_chan *ch)
{
	u32 intr_reg = plx_dma_ch_reg_read(ch, PLX_DMA_INTR_CTRL_STATUS);

	intr_reg &= ~(PLX_DMA_ALL_INTR_EN);
	plx_dma_ch_reg_write(ch, PLX_DMA_INTR_CTRL_STATUS, intr_reg);
}

static inline void plx_dma_chan_unmask_intr(struct plx_dma_chan *ch)
{
	u32 intr_reg = plx_dma_ch_reg_read(ch, PLX_DMA_INTR_CTRL_STATUS);

	intr_reg &= ~(PLX_DMA_ALL_INTR_EN);
	intr_reg |= PLX_DMA_ERROR_INTR_EN;
	intr_reg |= PLX_DMA_ABORT_DONE_INTR_EN;
	plx_dma_ch_reg_write(ch, PLX_DMA_INTR_CTRL_STATUS, intr_reg);
}

static void plx_dma_chan_set_desc_ring(struct plx_dma_chan *ch)
{
	plx_dma_chan_clear_desc_ring(ch);

	/* Set up the descriptor ring base address and size */
	plx_dma_ch_reg_write(ch, PLX_DMA_DESC_RING_ADDR_LOW,
			     ch->desc_ring_da & 0xffffffff);
	plx_dma_ch_reg_write(ch, PLX_DMA_DESC_RING_ADDR_HIGH,
			     (ch->desc_ring_da >> 32) & 0xffffffff);
	plx_dma_ch_reg_write(ch, PLX_DMA_DESC_RING_SIZE, PLX_DMA_DESC_RX_SIZE);
	/* Set up the next descriptor to the base of the descriptor ring */
	plx_dma_ch_reg_write(ch, PLX_DMA_NEXT_DESC_ADDR_LOW,
			     ch->desc_ring_da & 0xffffffff);
}

static inline void plx_dma_issue_pending(struct dma_chan *ch)
{
	struct plx_dma_chan *plx_ch = to_plx_dma_chan(ch);
	u32 ctrl_reg = plx_ch->ctrl_reg;

	/* Clear any active status bits ([31,12,10:8]) */
	ctrl_reg |= PLX_DMA_CTRL_HEADER_LOG | PLX_DMA_CTRL_STATUS_BITS; //Bit4

	/* DMA Start */
	ctrl_reg |= PLX_DMA_CTRL_START;

	plx_dma_ch_reg_write(plx_ch, PLX_DMA_CTRL_STATUS, ctrl_reg);
}

static void plx_dma_cleanup(struct plx_dma_chan *ch)
{
	struct dma_async_tx_descriptor *tx;
	u32 last_tail;
	u32 cached_head;

	spin_lock(&ch->cleanup_lock);
	/*
	 * Use a cached head rather than using ch->head directly, since a
	 * different thread can be updating ch->head potentially leading to an
	 * infinite loop below.
	 */
	cached_head = READ_ONCE(ch->head);
	/*
	 * This is the barrier pair for smp_wmb() in fn.
	 * plx_dma_tx_submit_unlock. It's required so that we read the
	 * updated cookie value from tx->cookie.
	 */
	smp_rmb();

	for (last_tail = ch->last_tail; last_tail != cached_head;) {
		/* Break out of the loop if this desc is not yet complete */
		if (READ_ONCE(ch->desc_ring[last_tail].dw0) & PLX_DESC_VALID) {
			/*
			 * Read register by PCI, wait on write back from PLX Chip.
			 * If interrupt received before finished transfer by PCI
			 * packed with write back to the descriptor, cleanup and call
			 * callback for some finished descriptors
			 * can be missing (or wait to next DMA traffic).
			 * To protect this issue Read by PCI register from PLX
			 * to be shore that all write back packaged sent before received,
			 * and check finish result again.
			 */
			u32 last_hw_tail =  plx_get_hw_last_desc(ch);
			if (READ_ONCE(ch->desc_ring[last_tail].dw0) & PLX_DESC_VALID) {
				if (last_hw_tail == last_tail) {
					/*
					 * When actual descriptor still not finished and this
					 * is last descriptor fetch by HW, check that HW register
					 * known that they still have scheduled work.
					 * From unknown reason when run multiple small buffers
					 * traffic, happens that HW not fetch correct next
					 * descriptor and stop transfers.
					 * To fix this issue, in the last undone descriptor
					 * detect that HW still known that some work waiting.
					 * If not set work in progress start transfer register again.
					 **/
					u32 ctrl_reg = plx_dma_ch_reg_read(ch, PLX_DMA_CTRL_STATUS);
					if (!(ctrl_reg & PLX_DMA_CTRL_IN_PROGRESS)) {
						struct plx_dma_device *plx_dma_dev = to_plx_dma_dev(ch);
						struct device *dev = plx_dma_dev->dma_dev.dev;
						dev_dbg(dev, "%s Detect DMA HW work in progress stopped!"
								" ch->last_tail %u last_tail %u ctrl_reg 0x%x",
								__func__, ch->last_tail, last_tail, ctrl_reg);
						ch->dbg_dma_hold_cnt++;
						plx_dma_issue_pending(&ch->chan);
					}
				}
				break;
			}
		}

		tx = &ch->tx_array[last_tail];
		if (tx->cookie) {
			BUG_ON(tx->cookie < DMA_MIN_COOKIE);
			completed_cookie_container(tx->chan)->completed_cookie = tx->cookie;
			tx->cookie = 0;
			if (tx->callback) {
				tx->callback(tx->callback_param);
				tx->callback = NULL;
			}
		}
		last_tail = plx_dma_ring_inc(last_tail);
	}
	/* finish all completion callbacks before incrementing tail */
	smp_mb();
	ch->last_tail = last_tail;
	spin_unlock(&ch->cleanup_lock);
}

static void plx_dma_chan_setup(struct plx_dma_chan *ch)
{
	struct plx_dma_device *plx_dma_dev = to_plx_dma_dev(ch);

	/*
	 * Disable timeout on DMA. For a variety stress traffic to all nodes,
	 * can happens that some node will be starved for longer that few seconds.
	 * Effect of on not handle timeout is dma_hand and reset system.
	 * */
	plx_dma_reg_write(plx_dma_dev, PCI_DEV_CTL2_REG, PCI_DEV_CTL2_CPL_TO_DIS);

	plx_dma_chan_set_desc_ring(ch);
	ch->last_tail = 0;
	ch->head = 0;
	ch->cleanup = plx_dma_cleanup;
	plx_dma_chan_unmask_intr(ch);
	plx_dma_enable_chan(ch);
}

static void plx_dma_free_desc_ring(struct plx_dma_chan *ch)
{
	u64 desc_ring_size = PLX_DMA_DESC_RX_SIZE * sizeof(*ch->desc_ring);
	struct device *dev = to_plx_dma_dev(ch)->dma_dev.dev;

	vfree(ch->tx_array);
	desc_ring_size = ALIGN(desc_ring_size, PLX_DMA_ALIGN_BYTES);
	dma_free_coherent(dev, desc_ring_size, ch->desc_ring, ch->desc_ring_da);
	ch->desc_ring = NULL;
}

#define RESET_PLX_ON_DMA_INIT

#ifdef RESET_PLX_ON_DMA_INIT
static void pci_reset_keep_registers(struct plx_dma_device *plx_dma_dev)
{
	u32 registers_to_keep[] = {
		0x0, 0x70, 0x22c, 0x238, 0x258, 0x25c, 0x260, 0x2a4, 0x2a8,
	};
	u32 register_values[sizeof(registers_to_keep)];

	int i;
	for (i = 0; i < ARRAY_SIZE(registers_to_keep); ++i)
		register_values[i] = plx_dma_reg_read(plx_dma_dev, registers_to_keep[i]);

	pci_reset_function(plx_dma_dev->pdev);

	for (i = 0; i < ARRAY_SIZE(registers_to_keep); ++i)
		plx_dma_reg_write(plx_dma_dev, registers_to_keep[i], register_values[i]);
}
#endif // RESET_PLX_ON_DMA_INIT

static int plx_dma_chan_init(struct plx_dma_chan *ch)
{
	struct device *dev = plx_dma_ch_to_device(ch);
	int rc;
#ifdef RESET_PLX_ON_DMA_INIT
	struct plx_dma_device *plx_dma_dev = to_plx_dma_dev(ch);
	/*
	 * PLXFIX: This reset helps to clear out some state in the
	 * PLX device which results in dmatest to run without issues.
	 * Remove this reset for now since it nukes the EEPROM
	 * performance settings. Investigate the following options:
	 * a) Can the reset can be removed completely?
	 * b) Can the reset added back and performance settings be
	 *    restored by the driver.
	 */
	uint32_t plx_caps = plx_dma_reg_read(plx_dma_dev, PLX_DMA_CAPABILITY_REG);
	if (plx_caps & PLX_DMA_FUNCTIONAL_RESET_BIT) {
		dev_info(dev, "%s: plx_dma_capabilities: 0x%x, resetting PLX functions\n", __func__, plx_caps);
		pci_reset_keep_registers(plx_dma_dev);
	} else
		dev_info(dev, "%s: plx_dma_capabilities: 0x%x, skipping reset\n", __func__, plx_caps);
#endif // RESET_PLX_ON_DMA_INIT
	rc = plx_dma_alloc_desc_ring(ch);
	if (!rc) {
		plx_dma_chan_setup(ch);
		plx_debug_init();
	}
	else dev_err(dev, "%s ret %d\n", __func__, rc);
	return rc;
}

static int plx_dma_alloc_chan_resources(struct dma_chan *ch)
{
	struct plx_dma_chan *plx_ch = to_plx_dma_chan(ch);
	struct device *dev = plx_dma_ch_to_device(plx_ch);
	int rc = plx_dma_chan_init(plx_ch);

	if (rc) {
		dev_err(dev, "%s ret %d\n", __func__, rc);
		return rc;
	}
	return PLX_DMA_DESC_RX_SIZE;
}

u32 plx_get_hw_last_desc(struct plx_dma_chan *ch)
{
	u32 last_desc = plx_dma_ch_reg_read(ch, PLX_DMA_LAST_DESC_ADDR_LOW);
	dma_addr_t last_dma_off = last_desc - (ch->desc_ring_da & 0xffffffffUL);

	return desc_ring_da_to_index(ch, last_dma_off);
}

u32 plx_get_hw_next_desc(struct plx_dma_chan *ch)
{
	u32 next_desc = plx_dma_ch_reg_read(ch, PLX_DMA_NEXT_DESC_ADDR_LOW);
	dma_addr_t next_dma_off = next_desc - (ch->desc_ring_da & 0xffffffffUL);

	return desc_ring_da_to_index(ch, next_dma_off);
}

static void plx_dma_free_chan_resources(struct dma_chan *ch)
{
	struct plx_dma_chan *plx_ch = to_plx_dma_chan(ch);

	plx_dma_disable_chan(plx_ch);
	plx_dma_chan_mask_intr(plx_ch);
	plx_dma_cleanup(plx_ch);
	plx_dma_free_desc_ring(plx_ch);
	plx_debug_destroy();
}

/**
 * plx_dma_cookie_status - report cookie status
 * @chan: dma channel
 * @cookie: cookie we are interested in
 * @state: dma_tx_state structure to return last/used cookies
 *
 * Report the status of the cookie, filling in the state structure if
 * non-NULL.  No locking is required.
 */
static inline enum dma_status plx_dma_cookie_status(struct dma_chan *chan,
	  dma_cookie_t cookie, dma_cookie_t *last, dma_cookie_t *used)
{
	dma_cookie_t last_complete, last_used;

	last_used = chan->cookie;
	last_complete = completed_cookie_container(chan)->completed_cookie;

	barrier();
	if (last)
		*last = last_complete;
	if (used)
		*used = last_used;

	return dma_async_is_complete(cookie, last_complete, last_used);
}

static enum dma_status
plx_dma_is_tx_complete(struct dma_chan *ch, dma_cookie_t cookie,
		  dma_cookie_t *last, dma_cookie_t *used)
{
	struct plx_dma_chan *plx_ch = to_plx_dma_chan(ch);
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 13, 0)
	if (DMA_SUCCESS != plx_dma_cookie_status(ch, cookie, last, used))
#else
	if (DMA_COMPLETE != plx_dma_cookie_status(ch, cookie, last, used))
#endif
		plx_dma_cleanup(plx_ch);
	return plx_dma_cookie_status(ch, cookie, last, used);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0)
static enum dma_status
plx_dma_tx_status(struct dma_chan *ch, dma_cookie_t cookie,
		  struct dma_tx_state *txstate)
{
	dma_cookie_t *last = NULL;
	dma_cookie_t *used = NULL;

	if (txstate) {
		last = &txstate->last;
		used = &txstate->used;
		txstate->residue = 0;
	}
	return plx_dma_is_tx_complete(ch, cookie, last, used);
}
#endif

static int plx_dma_avail_desc_ring_space(struct plx_dma_chan *ch, int required)
{
	struct device *dev = plx_dma_ch_to_device(ch);
	int count;

	count = plx_dma_ring_count(ch->head, ch->last_tail);
	if (count < required) {
		dev_err(dev, "%s ::: %d %d %d %d\n", __func__, count, required,
				ch->head, ch->last_tail);

		plx_dma_issue_pending(&ch->chan);
		plx_dma_cleanup(ch);
		count = plx_dma_ring_count(ch->head, ch->last_tail);
	}

	if (count < required) {
		dev_info(dev, "%s count %d reqd %d head %d tail %d\n",
			 __func__, count, required, ch->head, ch->last_tail);
		return -ENOMEM;
	}
	return count;
}

/* PLXFIX: Extended descriptor support required */
static void
plx_dma_memcpy_desc(struct plx_dma_desc *desc, dma_addr_t src_phys,
		    dma_addr_t dst_phys, u64 size, int flags, bool desc_valid)
{
	u32 dw0;

	if (desc->dw0 & PLX_DESC_VALID) {
		printk(KERN_ERR "%s Descriptor in USE!!! desc 0x%p",__func__, desc);
	}

	dw0 = (size & PLX_DESC_SIZE_MASK);
	desc->dw1 = ((src_phys >> 16) & PLX_DESC_HALF_HIGH_MASK) | (dst_phys >> 32);
	desc->dw2 = dst_phys & PLX_DESC_DWORD_MASK;
	desc->dw3 = src_phys & PLX_DESC_DWORD_MASK;

	if (desc_valid)
		dw0 |= PLX_DESC_VALID;

	if (flags)
		dw0 |=  PLX_DESC_INTR_ENABLE;

	/*
	 * Update DW1, DW2, DW3 before DW0 since DW0 has the valid bit and the
	 * descriptor might be picked up by the hardware DMA engine
	 * immediately if it finds a valid descriptor.
	 */
	wmb();

	desc->dw0 = dw0;
	/*
	 * This is not smp_wmb() on purpose since we are also publishing the
	 * descriptor updates to a dma device
	 */
	wmb();
}

static void plx_dma_valid_desc(struct plx_dma_desc *desc)
{
	desc->dw0 |= PLX_DESC_VALID;
	wmb();
}

static int plx_dma_prog_memcpy_desc(struct plx_dma_chan *ch, dma_addr_t src,
				    dma_addr_t dst, size_t len, int flags)
{
	struct device *dev = plx_dma_ch_to_device(ch);
	size_t send_len;
	size_t max_xfer_size = to_plx_dma_dev(ch)->max_xfer_size;
	int num_desc = DIV_ROUND_UP(len, max_xfer_size);
	bool valid_descr = true;
	int ret;

	if (!len) {
		/* Send empty descriptor */
		num_desc = 1;
	}

	ret = plx_dma_avail_desc_ring_space(ch, num_desc);
	if (ret <= 0) {
		dev_err(dev, "%s desc ring full ret %d\n", __func__, ret);
		return ret;
	}

	while (num_desc) {
		send_len = min(len, max_xfer_size);
		--num_desc;

		/*
		 * If flags set, not valid last descriptor with fence/interrupt.
		 * They will be valid in unlock.
		 */
		valid_descr = num_desc || !flags;

		/*
		 * Set flags not 0 only for last descriptor, we don't want memcpy
		 * descriptors to generate interrupts, a separate descriptor will be
		 * programmed for fence/interrupt during submit, after a callback is
		 * guaranteed to have been assigned
		 */
		plx_dma_memcpy_desc(&ch->desc_ring[ch->head],
				src, dst, send_len, num_desc? 0:flags, valid_descr);

		plx_debug(ch, &ch->desc_ring[ch->head], ret);

		if (valid_descr)
			plx_dma_inc_head(ch);

		len -= send_len;
		dst = dst + send_len;
		src = src + send_len;
	}
	return 0;
}

static dma_cookie_t plx_dma_tx_submit_unlock(struct dma_async_tx_descriptor *tx)
{
	struct dma_chan *chan = tx->chan;
	struct plx_dma_chan *plx_ch = to_plx_dma_chan(chan);
	dma_cookie_t cookie;

	cookie = chan->cookie + 1;
	if (cookie < DMA_MIN_COOKIE)
		cookie = DMA_MIN_COOKIE;
	tx->cookie = chan->cookie = cookie;

	/* Program the fence/interrupt desc in submit */
	if (tx->flags) {
		plx_dma_valid_desc(&plx_ch->desc_ring[plx_ch->head]);
		plx_dma_inc_head(plx_ch);
		tx->flags = 0;
	}

	spin_unlock(&plx_ch->prep_lock);
	return cookie;
}

static inline struct dma_async_tx_descriptor *
allocate_tx(struct plx_dma_chan *ch, unsigned long flags)
{
	/* index of the final desc which is or will be programmed */
	u32 idx = flags ? ch->head : plx_dma_ring_dec(ch->head);
	struct dma_async_tx_descriptor *tx = &ch->tx_array[idx];

	dma_async_tx_descriptor_init(tx, &ch->chan);
	tx->tx_submit = plx_dma_tx_submit_unlock;
	tx->flags = flags;
	return tx;
}

static struct dma_async_tx_descriptor *
plx_dma_prep_memcpy_lock(struct dma_chan *ch, dma_addr_t dma_dest,
			 dma_addr_t dma_src, size_t len, unsigned long flags)
{
	struct plx_dma_chan *plx_ch = to_plx_dma_chan(ch);
	struct device *dev = plx_dma_ch_to_device(plx_ch);
	int result;

	if ((dma_src & PLX_DMA_ALIGN_MASK) ||
		(dma_dest & PLX_DMA_ALIGN_MASK) ||
		(len & PLX_DMA_ALIGN_MASK)) {
		printk_ratelimited(KERN_WARNING "%s: DMA transfer not alignment to %i bytes. "
				"Performance drop, dma_src 0x%llx dma_dest 0x%llx len 0x%zx\n",
				__func__, PLX_DMA_ALIGN_BYTES, dma_src, dma_dest, len);
	}

	spin_lock(&plx_ch->prep_lock);
	result = plx_dma_prog_memcpy_desc(plx_ch, dma_src, dma_dest, len, flags);
	if (result >= 0)
		return allocate_tx(plx_ch, flags);

	dev_err(dev, "Error enqueueing dma, error=%d\n", result);
	spin_unlock(&plx_ch->prep_lock);
	return NULL;
}

static struct dma_async_tx_descriptor *
plx_dma_prep_interrupt_lock(struct dma_chan *ch, unsigned long flags)
{
	return plx_dma_prep_memcpy_lock(ch, 0, 0, 0, flags);
}

static u32 plx_dma_ack_interrupt(struct plx_dma_chan *ch)
{
	u32 intr_reg = plx_dma_ch_reg_read(ch, PLX_DMA_INTR_CTRL_STATUS);

	plx_dma_ch_reg_write(ch, PLX_DMA_INTR_CTRL_STATUS, intr_reg);
	return intr_reg;
}

static irqreturn_t plx_dma_thread_fn(int irq, void *data)
{
	struct plx_dma_device *plx_dma_dev = ((struct plx_dma_device *)data);
	bool intr_status = false, error = false;
	struct plx_dma_chan *ch = &plx_dma_dev->plx_chan;
	struct device *dev = plx_dma_ch_to_device(ch);
	u32 reg;

	if (ch->dma_hang)
		return IRQ_HANDLED;

	plx_dma_cleanup(&plx_dma_dev->plx_chan);

	reg = plx_dma_ack_interrupt(&plx_dma_dev->plx_chan);
	intr_status = !!(reg & PLX_DMA_DESC_DONE_INTR_STATUS);
	error = !!(reg & PLX_DMA_ERROR_STATUS);
	if (error) {
		/* dma engine is stuck, PLX soft or fundamental reset
		 * is required to recover from this */
		dev_err(dev, "%s dma hang detect 0x%x\n", __func__, reg);
		ch->dma_hang = true;
		return IRQ_HANDLED;
	}

	if (ch->dbg_flush) {
		dev_info(dev, "%s %d: flush interrupt!\n", __func__, __LINE__);
		ch->dbg_flush = false;
	}

	if (intr_status) {
		/* Call second time if thread triggered by HW IRQ.
		 * Some not handled tasks could be done between
		 * previous call plx_dma_cleanup() and turning on IRQ
		 * in plx_dma_ack_interrupt().*/
		plx_dma_cleanup(&plx_dma_dev->plx_chan);
	} else {
		dev_dbg(dev, "%s dma unexpected IRQ thread 0x%x\n", __func__, reg);
	}

	/* If thread exit with IRQ_WAKE_THREAD then Kernel 4.4 report issue. */
	return IRQ_HANDLED;
}

static irqreturn_t plx_dma_intr_handler(int irq, void *data)
{
	/* ((struct plx_dma_device *)data);*/
	return IRQ_WAKE_THREAD;
}

static int plx_dma_setup_irq(struct plx_dma_device *plx_dma_dev)
{
	struct pci_dev *pdev = plx_dma_dev->pdev;
	struct device *dev = &pdev->dev;
	int rc = pci_enable_msi(pdev);

	dev_info(plx_dma_dev->dma_dev.dev,
		"%s %d Entering \n", __func__, __LINE__);
	if (rc)
		pci_intx(pdev, 1);

	rc = devm_request_threaded_irq(dev, pdev->irq, plx_dma_intr_handler,
					 plx_dma_thread_fn, IRQF_SHARED,
					 "plx-dma-intr", plx_dma_dev);
	if (rc)
		return rc;

	rc = plx_irq_set_affinity_node_hint(pdev);

	return rc;
}

static inline void plx_dma_free_irq(struct plx_dma_device *plx_dma_dev)
{
	struct pci_dev *pdev = plx_dma_dev->pdev;
	struct device *dev = &pdev->dev;

	devm_free_irq(dev, pdev->irq, plx_dma_dev);
	if (pci_dev_msi_enabled(pdev))
		pci_disable_msi(pdev);
}

static int plx_dma_init(struct plx_dma_device *plx_dma_dev)
{
	struct plx_dma_chan *ch;
	int rc = 0;

	rc = plx_dma_setup_irq(plx_dma_dev);
	if (rc) {
		dev_err(plx_dma_dev->dma_dev.dev,
			"%s %d func error line %d\n", __func__, __LINE__, rc);
		goto error;
	}
	ch = &plx_dma_dev->plx_chan;
	ch->ch_base_addr = 0x200;
	spin_lock_init(&ch->cleanup_lock);
	spin_lock_init(&ch->prep_lock);
error:
	return rc;
}

static void plx_dma_uninit(struct plx_dma_device *plx_dma_dev)
{
	plx_dma_free_irq(plx_dma_dev);
}

static int plx_register_dma_device(struct plx_dma_device *plx_dma_dev)
{
	struct dma_device *dma_dev = &plx_dma_dev->dma_dev;
	struct plx_dma_chan *ch;

	dma_cap_zero(dma_dev->cap_mask);

	/* PLXFIX: Remove private flag from caps if running dmatest */
	dma_cap_set(DMA_PRIVATE, dma_dev->cap_mask);
	dma_cap_set(DMA_MEMCPY, dma_dev->cap_mask);

	dma_dev->device_alloc_chan_resources = plx_dma_alloc_chan_resources;
	dma_dev->device_free_chan_resources = plx_dma_free_chan_resources;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0)
	dma_dev->device_tx_status = plx_dma_tx_status;
#else
	dma_dev->device_is_tx_complete = plx_dma_is_tx_complete;
#endif
	dma_dev->device_prep_dma_memcpy = plx_dma_prep_memcpy_lock;
	dma_dev->device_prep_dma_interrupt = plx_dma_prep_interrupt_lock;
	dma_dev->device_issue_pending = plx_dma_issue_pending;
	INIT_LIST_HEAD(&dma_dev->channels);

	/* Associate dma_dev with dma_chan */
	ch = &plx_dma_dev->plx_chan;
	ch->chan.device = dma_dev;
	ch->chan.cookie = DMA_MIN_COOKIE;
	completed_cookie_container(&ch->chan)->completed_cookie = DMA_MIN_COOKIE;

	list_add_tail(&ch->chan.device_node, &dma_dev->channels);
	return dma_async_device_register(dma_dev);
}

int plx_dma_probe(struct plx_dma_device *plx_dma_dev)
{
	struct dma_device *dma_dev;
	int rc = -EINVAL;

	dma_dev = &plx_dma_dev->dma_dev;
	dma_dev->dev = &plx_dma_dev->pdev->dev;

	/* PLX 87XX has one DMA channel per function */
	dma_dev->chancnt = PLX_8733_NUM_CHAN;

	plx_dma_dev->max_xfer_size = PLX_DESC_SIZE_MASK;

	rc = plx_dma_init(plx_dma_dev);
	if (rc) {
		dev_err(&plx_dma_dev->pdev->dev,
			"%s %d rc %d\n", __func__, __LINE__, rc);
		goto init_error;
	}

	rc = plx_register_dma_device(plx_dma_dev);
	if (rc) {
		dev_err(&plx_dma_dev->pdev->dev,
			"%s %d rc %d\n", __func__, __LINE__, rc);
		goto reg_error;
	}
	plx_debugfs_init(plx_dma_dev);
	return rc;
reg_error:
	plx_dma_uninit(plx_dma_dev);
init_error:
	return rc;
}

static void plx_unregister_dma_device(struct plx_dma_device *plx_dma_dev)
{
	dma_async_device_unregister(&plx_dma_dev->dma_dev);
}

void plx_dma_remove(struct plx_dma_device *plx_dma_dev)
{
	plx_unregister_dma_device(plx_dma_dev);
	plx_dma_uninit(plx_dma_dev);
}

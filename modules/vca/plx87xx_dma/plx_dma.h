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
#ifndef PLX_DMA_H
#define PLX_DMA_H

#include <linux/dmaengine.h>
#include <linux/seq_file.h>
#include <linux/debugfs.h>
#include <linux/version.h>

extern struct dentry *plx_dma_dbg;

#define PLX_PCI_VENDOR_ID_PLX 0x10B5

#define PLX_8733_NUM_CHAN	1
#define PLX_DMA_DESC_RX_SIZE	(2 * 1024)
#define PLX_DMA_ALIGN_SHIFT	6
#define PLX_DMA_ALIGN_BYTES	(1 << (PLX_DMA_ALIGN_SHIFT))
#define PLX_DMA_ALIGN_MASK	((PLX_DMA_ALIGN_BYTES) - 1)
#define PLX_POLL_TIMEOUT	500000
#define PLX_DMA_PAUSE_TO	0x05

/* DMA Descriptor related flags */
#define PLX_DESC_VALID		(1UL << 31)
#define PLX_DESC_INTR_ENABLE	(1UL << 30)
#define PLX_DESC_SRC_LINK_ERR	(1UL << 29)
#define PLX_DESC_DST_LINK_ERR	(1UL << 28)
#define PLX_DESC_STD_DESC	(0UL << 27)
#define PLX_DESC_EXTD_DESC	(1UL << 27)
#define PLX_DESC_DWORD_MASK	(0xffffffffUL)
#define PLX_DESC_HALF_LOW_MASK	(0xffff)
#define PLX_DESC_HALF_HIGH_MASK	(0xffff<<16)
#define PLX_DESC_SIZE_MASK	((1UL << 27) - 1)

#define PLX_DMA_GLOBAL_CTL			0x1F8
/* DMA Per Channel reg offset */
#define PLX_DMA_BLK_SRC_LOWER			0x00
#define PLX_DMA_BLK_SRC_UPPER			0x04
#define PLX_DMA_BLK_DST_LOWER			0x08
#define PLX_DMA_BLK_DST_UPPER			0x0C
#define PLX_DMA_BLK_TRSFR_SIZE			0x10
#define PLX_DMA_DESC_RING_ADDR_LOW		0x14
#define PLX_DMA_DESC_RING_ADDR_HIGH		0x18
#define PLX_DMA_NEXT_DESC_ADDR_LOW		0x1C
#define PLX_DMA_DESC_RING_SIZE			0x20
#define PLX_DMA_LAST_DESC_ADDR_LOW		0x24
#define PLX_DMA_LAST_DESC_XFER_SIZE		0x28
#define PLX_DMA_MAX_PREFETCH			0x34
#define PLX_DMA_CTRL_STATUS			0x38
#define PLX_DMA_CTRL_PAUSED			(1UL << 0)
#define PLX_DMA_CTRL_ABORT			(1UL << 1)
#define PLX_DMA_CTRL_WB_ENABLE			(1UL << 2)
#define PLX_DMA_CTRL_START			(1UL << 3)
#define PLX_DMA_CTRL_RING_STOP			(1UL << 4)
#define PLX_DMA_CTRL_BLOCK_MODE			(0UL << 5)
#define PLX_DMA_CTRL_ONCHIP_MODE		(1UL << 5)
#define PLX_DMA_CTRL_OFFCHIP_MODE		(2UL << 5)
#define PLX_DMA_CTRL_DESC_INVLD_STATUS		(1UL << 8)
#define PLX_DMA_CTRL_PAUSE_DONE_STATUS		(1UL << 9)
#define PLX_DMA_CTRL_ABORT_DONE_STATUS		(1UL << 10)
#define PLX_DMA_CTRL_FACTORY_TEST		(1UL << 11)
#define PLX_DMA_CTRL_IMM_PAUSE_DONE_STATUS	(1UL << 12)
#define PLX_DMA_CTRL_IN_PROGRESS		(1UL << 30)
#define PLX_DMA_CTRL_HEADER_LOG			(1UL << 31)
#define PLX_DMA_CTRL_MODE_BITS			(3UL << 5)
#define PLX_DMA_CTRL_STATUS_BITS		(0x17UL << 8)
#define PLX_DMA_INTR_CTRL_STATUS		0x3C
#define PLX_DMA_ERROR_INTR_EN			(1UL << 0)
#define PLX_DMA_INVLD_DESC_INTR_EN		(1UL << 1)
#define PLX_DMA_ABORT_DONE_INTR_EN		(1UL << 3)
#define PLX_DMA_GRACE_PAUSE_INTR_EN		(1UL << 4)
#define PLX_DMA_IMM_PAUSE_INTR_EN		(1UL << 5)
#define PLX_DMA_IRQ_PIN_INTR_EN			(1UL << 15)
#define PLX_DMA_ERROR_INTR_STATUS		(1UL << 16)
#define PLX_DMA_INVLD_DESC_INTR_STATUS		(1UL << 17)
#define PLX_DMA_DESC_DONE_INTR_STATUS		(1UL << 18)
#define PLX_DMA_ABORT_DONE_INTR_STATUS		(1UL << 19)
#define PLX_DMA_GRACE_PAUSE_INTR_STATUS		(1UL << 20)
#define PLX_DMA_IMM_PAUSE_INTR_STATUS		(1UL << 21)
#define PLX_DMA_ALL_INTR_EN	(PLX_DMA_ERROR_INTR_EN |  \
			PLX_DMA_INVLD_DESC_INTR_EN	 |  \
			PLX_DMA_ABORT_DONE_INTR_EN	 |  \
			PLX_DMA_GRACE_PAUSE_INTR_EN	 |  \
			PLX_DMA_IMM_PAUSE_INTR_EN)
#define PLX_DMA_ERROR_STATUS	(PLX_DMA_ERROR_INTR_STATUS | \
				 PLX_DMA_INVLD_DESC_INTR_STATUS | \
				 PLX_DMA_ABORT_DONE_INTR_STATUS | \
				 PLX_DMA_IMM_PAUSE_INTR_STATUS)
#define PLX_DMA_STA_RAM_THRESH			0x58
#define PLX_DMA_STA0_HDR_RAM			0x5C
#define PLX_DMA_STA0_PLD_RAM			0x60
#define PLX_DMA_STA1_HDR_RAM			0xA4
#define PLX_DMA_STA1_PLD_RAM			0xA8
#define PLX_DMA_CAPABILITY_REG		0x6C
#define PLX_DMA_FUNCTIONAL_RESET_BIT	(1UL << 28)

/* PCIe Device Control 2 register */
#define PCI_DEV_CTL2_REG			0x90
#define PCI_DEV_CTL2_CPL_TO_MASK		0x0000000F
#define PCI_DEV_CTL2_CPL_16MS			(0UL << 0)
#define PCI_DEV_CTL2_CPL_64US			(1UL << 0)
#define PCI_DEV_CTL2_CPL_8MS			(2UL << 0)
#define PCI_DEV_CTL2_CPL_256US			(3UL << 0)
#define PCI_DEV_CTL2_CPL_512US			(4UL << 0)
#define PCI_DEV_CTL2_CPL_32MS			(5UL << 0)
#define PCI_DEV_CTL2_CPL_128MS			(6UL << 0)
#define PCI_DEV_CTL2_CPL_512MS			(9UL << 0)
#define PCI_DEV_CTL2_CPL_2S			(0xAUL << 0)
#define PCI_DEV_CTL2_CPL_8S			(0xDUL << 0)
#define PCI_DEV_CTL2_CPL_32S			(0xEUL << 0)
#define PCI_DEV_CTL2_CPL_TO_DIS			(1UL << 4)

#ifndef READ_ONCE
#define READ_ONCE(x) ACCESS_ONCE(x)
#endif
#ifndef WRITE_ONCE
#define WRITE_ONCE(x, val) ( ACCESS_ONCE(x) = (val) )
#endif

/* Flag inform about dma hang */
#define DMA_MODE_HANG				(1U<<0)
/* Flag to return error when try start dma transfer */
#define DMA_MODE_FORCE_FAIL			(1U<<1)
/* Flag to return error when check dma transfer result */
#define DMA_MODE_FAULT_INJECTION	(1U<<2)
/* Flag to return error transfers during abort */
#define DMA_MODE_ABORT				(1U<<3)

/* HW dma desc */
struct plx_dma_desc {
	u32 dw0;
	u32 dw1;
	u32 dw2;
	u32 dw3;
};

struct plx_dma_watchdog {
	volatile bool watchdog_thread_run;
	struct completion watchdog_thread_done;
	wait_queue_head_t watchdog_event;
	u32 callback_wait_repeat;
	unsigned long callback_wait_ms;
	u8 watchdog_enable;
};

/*
 * plx_dma_chan - PLX DMA channel specific data structures
 *
 * @ch_num: channel number
 * @last_tail: cached value of descriptor ring tail
 * @head: index of next descriptor in desc_ring
 * @chan: dma engine api channel
 * @desc_ring: dma descriptor ring
 * @desc_ring_da: DMA address of desc_ring
 * @ch_base_addr: MMIO base address for the channel
 * @ctrl_reg: Configuration bits in register PLX_DMA_CTRL_STATUS
 * @tx_array: array of async_tx
 * @cleanup_lock: lock held when processing completed tx
 * @prep_lock: lock held in prep_memcpy & released in tx_submit
 * @dma_hang_mode: detected dma hang error, set fail force mode, fault injection.
 * @cleanup: cleanup function to move the SW tail upto HW the tail
 */
struct plx_dma_chan {
	int ch_num;
	u32 last_tail;
	u32 head;
	struct dma_chan chan;
	struct plx_dma_desc *desc_ring;
	dma_addr_t desc_ring_da;
	u64 ch_base_addr;
	u32 ctrl_reg;
	struct dma_async_tx_descriptor *tx_array;
	spinlock_t cleanup_lock;
	spinlock_t prep_lock;
	bool dbg_flush;
	u32 dbg_dma_hold_cnt;
	u32 dma_hang_mode;
	struct plx_dma_watchdog watchdog;
	void (*cleanup)(struct plx_dma_chan *ch);
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 0, 0)
	dma_cookie_t completed_cookie;
#endif
};

/*
 * plx_dma_device - Per PLX DMA device driver specific data structures
 *
 * @pdev: PCIe device
 * @dma_dev: underlying dma device
 * @reg_base: virtual address of the mmio space
 * @plx_chan: Array of PLX DMA channels
 * @max_xfer_size: maximum transfer size per dma descriptor
 * @dbg_dir: debugfs directory
 */
struct plx_dma_device {
	struct pci_dev *pdev;
	struct dma_device dma_dev;
	void __iomem *reg_base;
	struct plx_dma_chan plx_chan;
	size_t max_xfer_size;
	struct dentry *dbg_dir;
	atomic_t intx; // PLX_DMA_INTR_CTRL_STATUS from plx_dma_intr_handler
};

static inline struct plx_dma_device *to_plx_dma_dev(struct plx_dma_chan *ch)
{
	return
	container_of((const typeof(((struct plx_dma_device *)0)->plx_chan)*)
		     (ch - ch->ch_num), struct plx_dma_device, plx_chan);
}

static inline struct plx_dma_chan *to_plx_dma_chan(struct dma_chan *ch)
{
	return container_of(ch, struct plx_dma_chan, chan);
}

static inline struct device *plx_dma_ch_to_device(struct plx_dma_chan *ch)
{
	return to_plx_dma_dev(ch)->dma_dev.dev;
}

static inline bool plx_get_dma_hang(struct plx_dma_chan *ch)
{
	return ch->dma_hang_mode & DMA_MODE_HANG;
}

static inline bool plx_get_dma_mode_force_fail(struct plx_dma_chan *ch)
{
	return ch->dma_hang_mode & DMA_MODE_FORCE_FAIL;
}

static inline bool plx_get_dma_fault_injection(struct plx_dma_chan *ch)
{
	return ch->dma_hang_mode & DMA_MODE_FAULT_INJECTION;
}

static inline int plx_dma_ring_count(u32 head, u32 tail)
{
	int count;

	if (head >= tail)
		count = (tail - 0) + (PLX_DMA_DESC_RX_SIZE - head);
	else
		count = tail - head;
	return count - 1;
}

int plx_dma_probe(struct plx_dma_device *device);
void plx_dma_remove(struct plx_dma_device *device);
u32 plx_dma_reg_read(struct plx_dma_device *pdma, u32 offset);
void plx_dma_reg_write(struct plx_dma_device *pdma, u32 offset,
		       u32 value);
u32 plx_dma_ch_reg_read(struct plx_dma_chan *ch, u32 offset);
u32 plx_get_hw_last_desc(struct plx_dma_chan *ch);
u32 plx_get_hw_next_desc(struct plx_dma_chan *ch);
void plx_debugfs_init(struct plx_dma_device *plx_dma_dev);
void plx_set_abort(struct plx_dma_chan *ch);
void plx_set_dma_mode(struct plx_dma_chan *ch, u32 mask, u32 value);

#ifdef PLX_DMA_DEBUG
struct plx_debug {
	struct plx_dma_desc desc;
	u32 head;
	u32 tail;
	u32 hw_tail;
	u32 space;
};

extern struct plx_debug *plx_dbg;
extern int plx_dbg_count;
#define PLX_DEBUG_ENTRIES 2000
static inline void
plx_debug(struct plx_dma_chan *ch, struct plx_dma_desc *desc, int space)
{
	if (plx_dbg_count >= PLX_DEBUG_ENTRIES)
		plx_dbg_count = 0;
	plx_dbg[plx_dbg_count].desc.qw0 = desc->qw0;
	plx_dbg[plx_dbg_count].desc.qw1 = desc->qw1;
	plx_dbg[plx_dbg_count].head = ch->head;
	plx_dbg[plx_dbg_count].hw_tail =
			plx_dma_ch_reg_read(ch, PLX_DMA_LAST_DESC_ADDR_LOW);
	plx_dbg[plx_dbg_count].space = space;
	plx_dbg[plx_dbg_count++].tail = ch->last_tail;
}

static inline void plx_debug_init(void)
{
	plx_dbg = vzalloc(PLX_DEBUG_ENTRIES * sizeof(*plx_dbg));
	plx_dbg_count = 0x0;
}

static inline void plx_debug_destroy(void)
{
	vfree(plx_dbg);
	plx_dbg = NULL;
}
#else
static inline void
plx_debug(struct plx_dma_chan *ch, struct plx_dma_desc *desc, int space)
{
}

static inline void plx_debug_init(void)
{
}

static inline void plx_debug_destroy(void)
{
}
#endif /* PLX_DMA_DEBUG */

#endif /* PLX_DMA_H */

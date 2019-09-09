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
#include <linux/debugfs.h>
#include <linux/freezer.h>
#include <linux/delay.h>
#include <linux/version.h>
#include <linux/pci.h>
#include <linux/uaccess.h>
#include "plx_dma.h"

#ifdef VCA_IN_KERNEL_BUILD
#include <linux/vca_dev_common.h>
#else
#include "../common/vca_dev_common.h"
#endif

#if !defined(U32_MAX)
#define U32_MAX	((u32)~0U)
#endif

#define DMA_REG_BEG	(0x1f0)
#define DMA_REG_END	(0x2b0)
#define DMA_REG_IGN(addr)	((addr>= 0x248 && addr <= 0x250) || \
		(addr>= 0x264 && addr <= 0x268) || addr>= 0x2ac)
#define DMA_REG_ALL_BEG	(0x0)
#define DMA_REG_ALL_END	(0x400)

static int plx_dma_reg_seq_show(struct seq_file *s, void *pos)
{
	struct plx_dma_device *plx_dma_dev = s->private;
	struct plx_dma_chan *ch = &plx_dma_dev->plx_chan;
	u32 ch_ctr_status;
	u32 intr_ctr_status;
	u32 reg;
	u32 reg_desc_ring_low = plx_dma_ch_reg_read(ch, PLX_DMA_DESC_RING_ADDR_LOW);
	u32 reg_desc_next_low = plx_dma_ch_reg_read(ch, PLX_DMA_NEXT_DESC_ADDR_LOW);
	u32 reg_desc_last_low = plx_dma_ch_reg_read(ch, PLX_DMA_LAST_DESC_ADDR_LOW);

	seq_printf(s, "head %d tail %d hw %d space %d dma_hold_cnt %u\n",
		   ch->head, ch->last_tail,
		   plx_get_hw_last_desc(ch),
		   plx_dma_ring_count(ch->head, ch->last_tail),
		   ch->dbg_dma_hold_cnt);
	seq_printf(s, "PLX_DMA_GLOBAL_CONTROL(0x1F8) 0X%-10X\n",
		   plx_dma_reg_read(plx_dma_dev, PLX_DMA_GLOBAL_CTL));
	seq_printf(s, "PLX_DMA_DESC_RING_ADDR_LOW(0x14) 0X%-10X\n",
			reg_desc_ring_low);
	seq_printf(s, "PLX_DMA_DESC_RING_ADDR_HIGH(0x18) 0X%-10X\n",
		   plx_dma_ch_reg_read(ch, PLX_DMA_DESC_RING_ADDR_HIGH));
	seq_printf(s, "PLX_DMA_NEXT_DESC_ADDR_LOW(0x1C) 0X%-10X id: %lu\n",
		    reg_desc_next_low,
		   (reg_desc_next_low - reg_desc_ring_low)/sizeof(struct plx_dma_desc));
	seq_printf(s, "PLX_DMA_DESC_RING_SIZE(0x20) 0X%X = %u\n",
		   plx_dma_ch_reg_read(ch, PLX_DMA_DESC_RING_SIZE),
		   plx_dma_ch_reg_read(ch, PLX_DMA_DESC_RING_SIZE));
	seq_printf(s, "PLX_DMA_LAST_DESC_ADDR_LOW(0x24) 0X%X id: %lu\n",
		    reg_desc_last_low,
		   (reg_desc_last_low - reg_desc_ring_low)/sizeof(struct plx_dma_desc));
	seq_printf(s, "PLX_DMA_LAST_DESC_XFER_SIZE(0x28) 0X%X\n",
		   plx_dma_ch_reg_read(ch, PLX_DMA_LAST_DESC_XFER_SIZE));

	intr_ctr_status = plx_dma_ch_reg_read(ch, PLX_DMA_INTR_CTRL_STATUS);
	seq_printf(s, "PLX_DMA_INTR_CTRL_STATUS(0x3C) = 0X%X\n",
		   intr_ctr_status);
	seq_printf(s, " PLX_DMA_ERROR_INTR_EN	(1 << 0)   %d\n",
		   !!(PLX_DMA_ERROR_INTR_EN & intr_ctr_status));
	seq_printf(s, " PLX_DMA_INVLD_DESC_INTR_EN	(1 << 1)  %d\n",
		   !!(PLX_DMA_INVLD_DESC_INTR_EN & intr_ctr_status));
	seq_printf(s, " PLX_DMA_ABORT_DONE_INTR_EN	(1 << 3)  %d\n",
		   !!(PLX_DMA_ABORT_DONE_INTR_EN & intr_ctr_status));
	seq_printf(s, " PLX_DMA_GRACE_PAUSE_INTR_EN	(1 << 4) %d\n",
		   !!(PLX_DMA_GRACE_PAUSE_INTR_EN & intr_ctr_status));
	seq_printf(s, " PLX_DMA_IMM_PAUSE_INTR_EN	(1 << 5)   %d\n",
		   !!(PLX_DMA_IMM_PAUSE_INTR_EN & intr_ctr_status));
	seq_printf(s, " PLX_DMA_IRQ_PIN_INTR_EN	(1 << 15)    %d\n",
		   !!(PLX_DMA_IRQ_PIN_INTR_EN & intr_ctr_status));
	seq_printf(s, " PLX_DMA_ERROR_INTR_STATUS	(1 << 16)  %d\n",
		   !!(PLX_DMA_ERROR_INTR_STATUS & intr_ctr_status));
	seq_printf(s, " PLX_DMA_INVLD_DESC_INTR_STATUS	(1 << 17) %d\n",
		   !!(PLX_DMA_INVLD_DESC_INTR_STATUS & intr_ctr_status));
	seq_printf(s, " PLX_DMA_DESC_DONE_INTR_STATUS	(1 << 18) %d\n",
		   !!(PLX_DMA_DESC_DONE_INTR_STATUS & intr_ctr_status));
	seq_printf(s, " PLX_DMA_ABORT_DONE_INTR_STATUS	(1 << 19) %d\n",
		   !!(PLX_DMA_ABORT_DONE_INTR_STATUS & intr_ctr_status));
	seq_printf(s, " PLX_DMA_GRACE_PAUSE_INTR_STATUS	(1 << 20) %d\n",
		   !!(PLX_DMA_GRACE_PAUSE_INTR_STATUS & intr_ctr_status));
	seq_printf(s, " PLX_DMA_IMM_PAUSE_INTR_STATUS	(1 << 21) %d\n",
		   !!(PLX_DMA_IMM_PAUSE_INTR_STATUS & intr_ctr_status));

	ch_ctr_status = plx_dma_ch_reg_read(ch, PLX_DMA_CTRL_STATUS);
	seq_printf(s, " PLX_DMA_CTRL_STATUS_REG(0x38) = 0X%X\n",
		   ch_ctr_status);
	seq_printf(s, " PLX_DMA_CTRL_PAUSED	(1 << 0) %d\n",
		   !!(PLX_DMA_CTRL_PAUSED & ch_ctr_status));
	seq_printf(s, " PLX_DMA_CTRL_START	(1 << 3) %d\n",
		   !!(PLX_DMA_CTRL_START & ch_ctr_status));
	seq_printf(s, " PLX_DMA_CTRL_IN_PROGRESS (1 << 30) %d\n",
		   !!(PLX_DMA_CTRL_IN_PROGRESS & ch_ctr_status));

	reg = plx_dma_ch_reg_read(ch, PLX_DMA_BLK_SRC_LOWER);
	seq_printf(s, "PLX_DMA_BLK_SRC_LOWER(0x0) 0x%x\n", reg);
	reg = plx_dma_ch_reg_read(ch, PLX_DMA_BLK_SRC_UPPER);
	seq_printf(s, "PLX_DMA_BLK_SRC_UPPER(0x4) 0x%x\n", reg);
	reg = plx_dma_ch_reg_read(ch, PLX_DMA_BLK_DST_LOWER);
	seq_printf(s, "PLX_DMA_BLK_DST_LOWER(0x8) 0x%x\n", reg);
	reg = plx_dma_ch_reg_read(ch, PLX_DMA_BLK_DST_UPPER);
	seq_printf(s, "PLX_DMA_BLK_DST_UPPER(0xC) 0x%x\n", reg);
	reg = plx_dma_ch_reg_read(ch, PLX_DMA_BLK_TRSFR_SIZE);
	seq_printf(s, "PLX_DMA_BLK_TRSFR_SIZE(0x10) 0x%x\n", reg);
	reg = plx_dma_ch_reg_read(ch, PLX_DMA_MAX_PREFETCH);
	seq_printf(s, "PLX_MAX_PREFETCH(0x34) 0x%x\n", reg);
	reg = plx_dma_ch_reg_read(ch, PLX_DMA_STA_RAM_THRESH);
	seq_printf(s, "PLX_DMA_STA_RAM_THRESH(0x58) 0x%x\n", reg);
	reg = plx_dma_ch_reg_read(ch, PLX_DMA_STA0_HDR_RAM);
	seq_printf(s, "PLX_DMA_STA0_HDR_RAM(0x5C) 0x%x\n", reg);
	reg = plx_dma_ch_reg_read(ch, PLX_DMA_STA0_PLD_RAM);
	seq_printf(s, "PLX_DMA_STA0_PLD_RAM(0x60) 0x%x\n", reg);
	reg = plx_dma_ch_reg_read(ch, PLX_DMA_STA1_HDR_RAM);
	seq_printf(s, "PLX_DMA_STA1_HDR_RAM(0xA4) 0x%x\n", reg);
	reg = plx_dma_ch_reg_read(ch, PLX_DMA_STA1_PLD_RAM);
	seq_printf(s, "PLX_DMA_STA1_PLD_RAM(0xA8) 0x%x\n", reg);
	return 0;
}

static int plx_dma_reg_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, plx_dma_reg_seq_show, inode->i_private);
}

static const struct file_operations plx_dma_reg_ops = {
	.owner   = THIS_MODULE,
	.open    = plx_dma_reg_debug_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = single_release
};

static int plx_dma_reg_map_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

static int plx_dma_reg_map_release(struct inode *inode, struct file *file)
{
	return 0;
}

static char *plx_dma_print_reg_bin(struct plx_dma_device *plx_dma_dev,
		u32 offset, char *tmp, char *end)
{
	u32 reg;
	int i;

	reg = plx_dma_reg_read(plx_dma_dev, offset);
	tmp += snprintf(tmp, end - tmp, "[0x%04x]: 0x%08x    ", offset, reg);
	for (i=31; i>=0; --i) {
		if (reg & 1<<(i))
			tmp += snprintf(tmp, end - tmp, "1");
		else
			tmp += snprintf(tmp, end - tmp, "0");
		if (!(i%8))
			tmp += snprintf(tmp, end - tmp, " ");
	}
	tmp += snprintf(tmp, end - tmp, "\n");
	return tmp;
}

static ssize_t plx_dma_reg_map_read(struct file *file,
		 char __user * buf, size_t count, loff_t * pos)
{
	unsigned size = 4096;
	char *tmp_buff = kmalloc(size, GFP_KERNEL);
	char *end = tmp_buff + size;
	char *tmp = tmp_buff;

	struct plx_dma_device *plx_dma_dev = file->private_data;
	struct plx_dma_chan *ch = &plx_dma_dev->plx_chan;
	u32 offset;

	if (!tmp_buff)
		return 0;

#if PLX_MEM_DEBUG
	tmp += snprintf(tmp, end - tmp, "Write: [0xOffset] [0xMask_val] [0xValue]\n");
#endif /* PLX_MEM_DEBUG */

	tmp += snprintf(tmp, end - tmp, "Registers DMA Channel 0x%xh-0x%xh "
			"base_addr: head 0x%08Lx\n", DMA_REG_BEG, DMA_REG_END,
			ch->ch_base_addr);
	tmp += snprintf(tmp, end - tmp, "[OFFSET]    VALUE       ");
	tmp += snprintf(tmp, end - tmp, "31       23       15       7      0\n");

	/* Print DMA channel registers. */
	for (offset = DMA_REG_BEG; offset <= DMA_REG_END && tmp < end - 1;
			offset += 4) {
		/* Ignore reserved addresses */
		if (DMA_REG_IGN(offset))
			continue;

		tmp = plx_dma_print_reg_bin(plx_dma_dev, offset, tmp, end);
	}

	/* Print special control status registers. */
	for (offset = 0xFB8; offset <= 0xFDC && tmp < end - 1; offset += 4) {
		tmp = plx_dma_print_reg_bin(plx_dma_dev, offset, tmp, end);
	}

	tmp += snprintf(tmp, end - tmp, "END\n");
	size = simple_read_from_buffer(buf, count, pos, tmp_buff, tmp - tmp_buff);
	kfree(tmp_buff);
	return size;
}

#if PLX_MEM_DEBUG
static ssize_t plx_dma_reg_map_write(struct file* file, const char* __user buff,
		size_t count, loff_t * ppos)
{
	struct plx_dma_device *plx_dma_dev = file->private_data;
	char temp[128];
	u32 offset = U32_MAX;
	u32 mask = U32_MAX;
	u32 value = U32_MAX;
	u32 reg;
	int res;

	/*
	 * Usage update registers: echo "0xAddr 0xMask 0xValue" > dma_reg_map
	 * new_reg_val = (old_reg_val & ~Mask) | (Value & Mask)
	 */

	if(count >= sizeof(temp)) {
		printk(KERN_INFO "%s:%d too much params\n", __FILE__, __LINE__);
		return -E2BIG;
	}

	if(copy_from_user(temp, buff, count)) {
		printk(KERN_INFO "%s:%d copy_from_user err\n", __FILE__, __LINE__);
		return -EFAULT;
	}

	temp[count] = '\0';
	res = sscanf(temp, "0x%x 0x%x 0x%x", &offset, &mask, &value);

	if (res != 3) {
		printk(KERN_INFO "%s:%d Can't parse params: 0x%x 0x%x 0x%x params: %i\n",
				__FILE__, __LINE__, offset, mask, value, res);
		return -ENOEXEC;
	}

	if (offset & 3) {
		printk(KERN_INFO "%s:%d Offset not aligment to 4 0x%x\n", __FILE__,
				__LINE__, offset);
		return -ENOEXEC;
	}

	if (offset < DMA_REG_ALL_BEG || offset > DMA_REG_ALL_END) {
		printk(KERN_INFO "%s:%d Offset out of a range [0x%x-0x%x] 0x%x\n",
				__FILE__, __LINE__, DMA_REG_ALL_BEG, DMA_REG_ALL_END, offset);
		return -ENOEXEC;
	}

	reg = plx_dma_reg_read(plx_dma_dev, offset);
	plx_dma_reg_write(plx_dma_dev, offset, (reg & ~mask) | (value & mask));

	return count;
}
#endif /* PLX_MEM_DEBUG */

static const struct file_operations plx_dma_reg_map_ops = {
	.owner   = THIS_MODULE,
	.open    = plx_dma_reg_map_open,
	.read    = plx_dma_reg_map_read,
#if PLX_MEM_DEBUG
	.write   = plx_dma_reg_map_write,
#endif /* PLX_MEM_DEBUG */
	.release = plx_dma_reg_map_release
};

static int plx_dma_desc_ring_seq_show(struct seq_file *s, void *pos)
{
	struct plx_dma_device *plx_dma_dev = s->private;
	int i;
	struct plx_dma_chan *ch;
	struct plx_dma_desc *desc;
	u64 src, dst;
	struct dma_async_tx_descriptor *tx;

	ch = &plx_dma_dev->plx_chan;
	seq_printf(s, "desc ring da 0x%llx\n", ch->desc_ring_da);
	seq_printf(s, "head %d tail %d hw last %d hw next %d"
		   " used %d space %d dma_hold_cnt %u\n", ch->head, ch->last_tail,
		   plx_get_hw_last_desc(ch), plx_get_hw_next_desc(ch),
		   plx_dma_ring_count(ch->last_tail, ch->head),
		   plx_dma_ring_count(ch->head, ch->last_tail),
		   ch->dbg_dma_hold_cnt);

	if (!ch->desc_ring)
		return 0;

	for (i = 0; i < PLX_DMA_DESC_RX_SIZE; i++) {
		desc = &ch->desc_ring[i];
		src = (u64)desc->dw3 | ((u64)desc->dw1 & PLX_DESC_HALF_HIGH_MASK) << 16;
		dst = (u64)desc->dw2 | (u64)(desc->dw1 & PLX_DESC_HALF_LOW_MASK) << 32;

		tx = &ch->tx_array[i];
		seq_printf(s, "[%04d] v 0x%x in 0x%x serr 0x%x derr 0x%x df 0x%x",
			   i, !!(desc->dw0 & PLX_DESC_VALID),
			   !!(desc->dw0 & PLX_DESC_INTR_ENABLE),
			   !!(desc->dw0 & PLX_DESC_SRC_LINK_ERR),
			   !!(desc->dw0 & PLX_DESC_DST_LINK_ERR),
			   !!(desc->dw0 & PLX_DESC_STD_DESC));
		seq_printf(s, " src 0x%012llx dst 0x%012llx sz 0x%04lx",
			   src, dst, desc->dw0 & PLX_DESC_SIZE_MASK);

		seq_printf(s, " dw0 0x%012x dw1 0x%012x dw2 0x%012x dw3 0x%012x cookie %i\n",
				desc->dw0, desc->dw1, desc->dw2, desc->dw3, tx->cookie);

	}
	return 0;
}

static int plx_dma_desc_ring_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, plx_dma_desc_ring_seq_show, inode->i_private);
}

static const struct file_operations plx_dma_desc_ring_ops = {
	.owner   = THIS_MODULE,
	.open    = plx_dma_desc_ring_debug_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = single_release
};

struct dmatest_done {
	bool                    done;
	wait_queue_head_t       *wait;
};

static void dmatest_callback(void *arg)
{
	struct dmatest_done *done = arg;

	done->done = true;
	wake_up_interruptible_all(done->wait);
}

static int plx_dma_intr_test_seq_show(struct seq_file *s, void *pos)
{
	struct plx_dma_device *plx_dma_dev = s->private;
	struct dma_chan *chan = &plx_dma_dev->plx_chan.chan;
	struct dma_device *ddev;
	struct dma_async_tx_descriptor *tx = NULL;
	dma_cookie_t cookie;

	DECLARE_WAIT_QUEUE_HEAD_ONSTACK(done_wait);
	struct dmatest_done done = { .wait = &done_wait };
	enum dma_status status;

	if (!chan) {
		seq_puts(s, "Interrupt Test Error no channel!\n");
		goto done;
	}

	ddev = chan->device;
	tx = ddev->device_prep_dma_interrupt(chan, DMA_PREP_INTERRUPT);
	if (!tx) {
		seq_puts(s, "Interrupt Test Error tx!\n");
		goto release;
	}
	done.done = false;
	tx->callback = dmatest_callback;
	tx->callback_param = &done;
	cookie = tx->tx_submit(tx);
	if (dma_submit_error(cookie)) {
		seq_puts(s, "Interrupt Test Error submit!\n");
		goto release;
	}
	dma_async_issue_pending(chan);
	if (wait_event_interruptible_timeout(done_wait, done.done,
		     msecs_to_jiffies(1000)) < 0)
		goto release;

	status = dma_async_is_tx_complete(chan, cookie, NULL, NULL);
	if (!done.done) {
		seq_puts(s, "Interrupt Test Error no interrupt!\n");
		goto release;
	}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 13, 0)
	else if (status != DMA_COMPLETE) {
#else
	else if (status != DMA_SUCCESS) {
#endif
		seq_puts(s, "Interrupt Test Error status!\n");
		goto release;
	}
	tx = ddev->device_prep_dma_memcpy(chan, 0, 0, 0, DMA_PREP_FENCE);
	if (!tx) {
		seq_puts(s, "Interrupt Test Error prep memcpy!\n");
		goto release;
	}
	cookie = tx->tx_submit(tx);
	if (dma_submit_error(cookie)) {
		seq_puts(s, "Interrupt Test Error submit memcpy!\n");
		goto release;
	}
	dma_async_issue_pending(chan);
	msleep(100);
	status = dma_async_is_tx_complete(chan, cookie, NULL, NULL);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 13, 0)
	if (status != DMA_COMPLETE) {
#else
	if (status != DMA_SUCCESS) {
#endif
		seq_puts(s, "Interrupt Test Error status memcpy!\n");
		goto release;
	}
	seq_puts(s, "Interrupt Test passed!\n");
	return 0;
release:
	seq_puts(s, "Interrupt Test failed\n");
done:
	return 0;
}

static int plx_dma_intr_test_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, plx_dma_intr_test_seq_show, inode->i_private);
}

static const struct file_operations plx_dma_intr_test_ops = {
	.owner   = THIS_MODULE,
	.open    = plx_dma_intr_test_debug_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = single_release
};

static int plx_dma_dbg_trace_seq_show(struct seq_file *s, void *pos)
{
#ifdef PLX_DMA_DEBUG
	int i;
	struct plx_dma_desc *desc;
	u64 src, dst;

	if (!plx_dbg)
		return 0;

	for (i = 0; i < plx_dbg_count; i++) {
		struct plx_debug *dbg = &plx_dbg[i];

		desc = &dbg->desc;
		src = (desc->qw1 >> 32) | (desc->qw0 >> 48) << 32;
		dst = (desc->qw1 & PLX_DESC_LOW_MASK);
		dst |= (((desc->qw0 >> 32) & 0xffff) << 32);
		seq_printf(s, "[%d] v 0x%x in 0x%x df 0x%x sz 0x%llx",
			   i, !!(desc->qw0 & PLX_DESC_VALID),
			   !!(desc->qw0 & PLX_DESC_INTR_ENABLE),
			   !!(desc->qw0 & PLX_DESC_STD_DESC),
			   desc->qw0 & PLX_DESC_SIZE_MASK);
		seq_printf(s, " src 0x%llx dst 0x%llx head %d tail %d",
			   src, dst, dbg->head, dbg->tail);
		seq_printf(s, " hw_tail 0x%x space %d\n",
			   dbg->hw_tail, dbg->space);
	}
#endif
	return 0;
}

static int
plx_dma_dbg_trace_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, plx_dma_dbg_trace_seq_show, inode->i_private);
}

static const struct file_operations plx_dma_dbg_trace_ops = {
	.owner   = THIS_MODULE,
	.open    = plx_dma_dbg_trace_debug_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = single_release
};

static int plx_dma_flush_seq_show(struct seq_file *s, void *pos)
{
	struct plx_dma_device *plx_dma_dev = s->private;
	struct dma_chan *chan = &plx_dma_dev->plx_chan.chan;
	struct dma_device *ddev = chan->device;
	struct dma_async_tx_descriptor *tx = NULL;
	DECLARE_WAIT_QUEUE_HEAD_ONSTACK(done_wait);
	struct dmatest_done done = { .done = false, .wait = &done_wait };
	dma_cookie_t cookie;
	enum dma_status status;
	int res = 0;

	if (!chan) {
		seq_puts(s, "Interrupt Test Error no channel!\n");
		res = -ENXIO;
		goto err;
	}

	tx = ddev->device_prep_dma_interrupt(chan, DMA_PREP_INTERRUPT);
	if (!tx) {
		seq_puts(s, "Flush DMA prepare failed\n");
		res = -EIO;
		goto err;
	}
	plx_dma_dev->plx_chan.dbg_flush = true;
	tx->callback = dmatest_callback;
	tx->callback_param = &done;
	cookie = tx->tx_submit(tx);
	if (dma_submit_error(cookie)) {
		res = -EIO;
		goto err;
	}
	dma_async_issue_pending(chan);
	msleep(1000);
	status = dma_async_is_tx_complete(chan, cookie, NULL, NULL);

	if (READ_ONCE(plx_dma_dev->plx_chan.dbg_flush)) {
		seq_puts(s, "ERROR: DMA handler interrupt pending\n");
		res = -EAGAIN;
	} else {
		seq_puts(s, "DMA handler interrupt received\n");
	}

	if (!done.done) {
		seq_puts(s, "ERROR: Flush DMA interrupt pending\n");
		res = -EAGAIN;
	} else {
		seq_puts(s, "Flush DMA interrupt received\n");
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 13, 0)
	if (status == DMA_COMPLETE) {
#else
	if (status == DMA_SUCCESS) {
#endif
		seq_puts(s, "Flush DMA tx complete!\n");
	} else {
		seq_puts(s, "ERROR: Flush DMA tx pending!\n");
		res = -EAGAIN;
	}

	return 0;
err:
	seq_puts(s, "Flush attempt failed\n");
	return res;
}

static int plx_dma_flush_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, plx_dma_flush_seq_show, inode->i_private);
}

static const struct file_operations plx_dma_flush_ops = {
	.owner   = THIS_MODULE,
	.open    = plx_dma_flush_debug_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = single_release
};

static int plx_dma_issue_pending_show(struct seq_file *s, void *pos)
{
	struct plx_dma_device *plx_dma_dev = s->private;
	struct dma_chan *chan = &plx_dma_dev->plx_chan.chan;
	dma_async_issue_pending(chan);
	seq_puts(s, "dma_async_issue_pending() complete!\n");
	return 0;
}

static int plx_dma_issue_pending_open(struct inode *inode, struct file *file)
{
	return single_open(file, plx_dma_issue_pending_show, inode->i_private);
}

static const struct file_operations plx_dma_issue_pending_ops = {
	.owner   = THIS_MODULE,
	.open    = plx_dma_issue_pending_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = single_release
};

static int plx_dma_cleanup_seq_show(struct seq_file *s, void *pos)
{
	struct plx_dma_device *plx_dma_dev = s->private;
	struct plx_dma_chan *plx_chan = &plx_dma_dev->plx_chan;

	plx_chan->cleanup(plx_chan);
	return 0;
}

static int plx_dma_cleanup_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, plx_dma_cleanup_seq_show, inode->i_private);
}

static const struct file_operations plx_dma_cleanup_ops = {
	.owner   = THIS_MODULE,
	.open    = plx_dma_cleanup_debug_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = single_release
};

static int plx_dma_dump_regs_single(struct seq_file *s, void *pos)
{
	struct plx_dma_device *plx_dma_dev = s->private;
	int i;
	u32* reg_ptr = plx_dma_dev->reg_base;

	for (i = DMA_REG_ALL_BEG; i < DMA_REG_ALL_END; i++)
		seq_printf(s, "[0x%04x] = %08x\n", i*4, reg_ptr[i]);
	return 0;
}

static int plx_dma_dump_regs_single_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, plx_dma_dump_regs_single, inode->i_private);
}

static const struct file_operations plx_dma_dump_regs_single_ops = {
	.owner   = THIS_MODULE,
	.open    = plx_dma_dump_regs_single_debug_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = single_release
};

void plx_debugfs_init(struct plx_dma_device *plx_dma_dev)
{
	struct dma_device *dma_dev = &plx_dma_dev->dma_dev;

	if( !IS_ERR_OR_NULL( plx_dma_dbg)) {
		plx_dma_dev->dbg_dir =
			debugfs_create_dir(dev_name(dma_dev->dev),
					   plx_dma_dbg);
		if( !IS_ERR_OR_NULL( plx_dma_dev->dbg_dir)) {
			debugfs_create_file("dma_reg", 0444,
					    plx_dma_dev->dbg_dir, plx_dma_dev,
					    &plx_dma_reg_ops);
			debugfs_create_file("dma_reg_map", 0644,
					    plx_dma_dev->dbg_dir, plx_dma_dev,
					    &plx_dma_reg_map_ops);
			debugfs_create_file("desc_ring", 0444,
					    plx_dma_dev->dbg_dir, plx_dma_dev,
					    &plx_dma_desc_ring_ops);
			debugfs_create_file("intr_test", 0444,
					    plx_dma_dev->dbg_dir, plx_dma_dev,
					    &plx_dma_intr_test_ops);
			debugfs_create_file("debug_trace", 0444,
					    plx_dma_dev->dbg_dir, plx_dma_dev,
					    &plx_dma_dbg_trace_ops);
			debugfs_create_file("flush", 0444,
					    plx_dma_dev->dbg_dir, plx_dma_dev,
					    &plx_dma_flush_ops);
			debugfs_create_file("issue_pending", 0444,
						plx_dma_dev->dbg_dir, plx_dma_dev,
						&plx_dma_issue_pending_ops);
			debugfs_create_file("cleanup", 0444,
					    plx_dma_dev->dbg_dir, plx_dma_dev,
					    &plx_dma_cleanup_ops);
			debugfs_create_file("dump_regs_single", 0444,
					    plx_dma_dev->dbg_dir, plx_dma_dev,
					    &plx_dma_dump_regs_single_ops);
		}
	}
}

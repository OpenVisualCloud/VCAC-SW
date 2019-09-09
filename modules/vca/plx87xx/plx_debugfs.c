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
 *
 */
#include <linux/uaccess.h>
#include <linux/debugfs.h>
#include <linux/pci.h>
#include <linux/seq_file.h>
#include <linux/delay.h>

#ifdef VCA_IN_KERNEL_BUILD
#include <linux/vca_dev_common.h>
#else
#include "../common/vca_dev_common.h"
#endif
#include "plx_device.h"
#include "plx_hw.h"

/* Debugfs parent dir */
static struct dentry *plx_dbg;

enum plx_eep_retval eeprom_read32(struct plx_device *xdev, u32 offset, u32 *value_32);

static ssize_t plx_read_eeprom0(struct file *file, char __user * buf, size_t count, loff_t * pos) {
	if(( 4<= count)&& !( *pos& ~0x3ffc)) {
		struct plx_device*const xdev= file->private_data;
		u32 v;
		mutex_lock(&xdev->mmio_lock);
		if( PLX_EEP_STATUS_OK== eeprom_read32(xdev, *(u32*)pos, &v)) {
			mutex_unlock( &xdev->mmio_lock);
			put_user( v,(u32*) buf);
			(*pos)+= 4;
			return 4;
		}
		mutex_unlock( &xdev->mmio_lock);
	}
	return -EINVAL;
}

static int plx_msi_irq_info_show(struct seq_file *s, void *pos)
{
	struct plx_device *xdev  = s->private;
	int j;
	u16 entry;
	u16 vector;
	struct pci_dev *pdev = container_of(&xdev->pdev->dev,
		struct pci_dev, dev);

	if (pci_dev_msi_enabled(pdev)) {
		entry = 0;
		vector = pdev->irq;

		seq_printf(s, "%s %-10d %s %-10d\n",
			   "IRQ:", vector, "Entry:", entry);

		seq_printf(s, "%-10s", "offset:");
		for (j = (PLX_NUM_OFFSETS - 1); j >= 0; j--)
			seq_printf(s, "%4d ", j);
		seq_puts(s, "\n");

		seq_printf(s, "%-10s", "count:");
		for (j = (PLX_NUM_OFFSETS - 1); j >= 0; j--)
			seq_printf(s, "%4d ",
				   (xdev->irq_info.plx_msi_map[entry] &
				   BIT(j)) ? 1 : 0);
		seq_puts(s, "\n\n");
	} else {
		seq_puts(s, "MSI/MSIx interrupts not enabled\n");
	}

	return 0;
}

static int plx_msi_irq_info_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, plx_msi_irq_info_show, inode->i_private);
}

static const struct file_operations msi_irq_info_ops = {
	.owner   = THIS_MODULE,
	.open    = plx_msi_irq_info_debug_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = single_release
};

#if PLX_MEM_DEBUG
static int plx_memory_read_show(struct seq_file *s, void *pos)
{
	struct plx_device *xdev = s->private;
	u64 hi, lo;
	dma_addr_t daddr;
	void __iomem *va;
	void *laddr;
	int link_side = 0;
	u8 val;

	lo = plx_read_spad(xdev, PLX_DPLO_SPAD);
	hi = plx_read_spad(xdev, PLX_DPHI_SPAD);

	daddr = (hi << 32) | lo;

	lo = plx_read_spad(xdev, 2);
	hi = plx_read_spad(xdev, 3);

	link_side = lo & 0x1;
	laddr = (void *)((hi << 32) | (lo & ~0x1));

	seq_printf(s, "daddr 0x%llx\n", daddr);
	seq_printf(s, "laddr 0x%llx link_side local: %i remote: %i\n",
		(dma_addr_t)laddr, xdev->link_side?1:0, link_side);

	if ((xdev->link_side && link_side) || (!xdev->link_side && !link_side)){
		seq_printf(s, "value local 0x%lx\n", readq(laddr));
	} else {
		va = plx_ioremap(xdev, daddr, 4096);
		if (va) {
			seq_printf(s, "value remote 0x%lx\n", readq(va));
			val = *(u8 *)va;
			memset(va, val +1, 4096);
			seq_printf(s, "value remote new 0x%lx\n", readq(va));
		}
		else
			seq_printf(s, "va %p\n", va);
	}
	return 0;
}

static int plx_memory_read_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, plx_memory_read_show, inode->i_private);
}

static const struct file_operations plx_memory_read_ops = {
	.owner   = THIS_MODULE,
	.open    = plx_memory_read_debug_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = single_release
};

static int plx_memory_write_show(struct seq_file *s, void *pos)
{
	struct plx_device *xdev = s->private;
	void *va = kmalloc(4096, GFP_KERNEL);
	dma_addr_t daddr;
	int link_side = 0;

	if (xdev->link_side) {
		link_side = 1;
	}

	memset(va, 0xac, 4096);
	daddr = dma_map_single(&xdev->pdev->dev, va, 4096, DMA_BIDIRECTIONAL);

	CHECK_DMA_ZONE(&xdev->pdev->dev, daddr);

	plx_write_spad(xdev, 0, daddr);
	plx_write_spad(xdev, 1, daddr >> 32);

	plx_write_spad(xdev, 2, (dma_addr_t)((uintptr_t)va | link_side));
	plx_write_spad(xdev, 3, (dma_addr_t)va >> 32);

	seq_printf(s, "daddr 0x%llx\n", daddr);
	seq_printf(s, "laddr 0x%llx link_side: %i\n", (dma_addr_t)va, link_side);
	return 0;
}

static int plx_memory_write_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, plx_memory_write_show, inode->i_private);
}

static const struct file_operations plx_memory_write_ops = {
	.owner   = THIS_MODULE,
	.open    = plx_memory_write_debug_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = single_release
};

u64 read_offset;
u64 read_local_offset;

static int local_read_open(struct inode* inode, struct file *file)
{
	struct plx_device* xdev;

	file->private_data = inode->i_private;

        xdev = (struct plx_device *)file->private_data;

	return 0;
}

static ssize_t local_read_write(struct file* file, const char* __user buff,
		size_t count, loff_t * ppos)
{
	char temp[128];

	if(count >= 128) {
		return -E2BIG;
	}

	if(copy_from_user(temp, buff, count)) {
		return -EFAULT;
	}

	temp[count] = '\0';

	sscanf(temp, "%llx", &read_local_offset);

	return count;
}

static ssize_t local_read_read (struct file *file,
 char __user * buf, size_t count, loff_t * pos)
{
	unsigned int* vt = phys_to_virt(read_local_offset);
	unsigned int val[4];
	char temp[128] = {0,};
	int chars;

	if (!vt) {
		return 0;
	}

	memcpy(val, vt, 4*4);

	chars = snprintf(temp, 128, "%x %x %x %x\n",
		val[0], val[1], val[2], val[3]);

	return simple_read_from_buffer(buf, count, pos, temp, chars);
}

static const struct file_operations local_read_ops = {
	.owner = THIS_MODULE,
	.open = local_read_open,
	.read = local_read_read,
	.write = local_read_write
};

static int bar_read_open(struct inode* inode, struct file *file)
{
	struct plx_device* xdev;

	file->private_data = inode->i_private;

        xdev = (struct plx_device *)file->private_data;

	return 0;
}

static ssize_t bar_read_write(struct file* file, const char* __user buff,
		size_t count, loff_t * ppos)
{
	char temp[128];

	if(count >= 128) {
		return -E2BIG;
	}

	if(copy_from_user(temp, buff, count)) {
		return -EFAULT;
	}

	temp[count] = '\0';

	sscanf(temp, "%llx", &read_offset);

	return count;
}

static ssize_t bar_read_read (struct file *file,
 char __user * buf, size_t count, loff_t * pos)
{
	struct plx_device* xdev = (struct plx_device *)file->private_data;
	unsigned int* vt = plx_ioremap(xdev, read_offset, 4 * 4);
	unsigned int val[4];
	char temp[128] = {0,};
	int chars;

	if (!vt) {
		return 0;
	}

	memcpy(val, vt, 4*4);
	plx_iounmap(xdev, vt);
	vt = NULL;

	chars = snprintf(temp, 128, "%x %x %x %x\n",
		val[0], val[1], val[2], val[3]);

	return simple_read_from_buffer(buf, count, pos, temp, chars);
}

static const struct file_operations bar_read_ops = {
	.owner = THIS_MODULE,
	.open = bar_read_open,
	.read = bar_read_read,
	.write = bar_read_write };


static int bar_write_open(struct inode* inode, struct file *file)
{
	struct plx_device* xdev;

	file->private_data = inode->i_private;

        xdev = (struct plx_device *)file->private_data;
	return 0;
}

static ssize_t bar_write_write(struct file* file, const char* __user buff,
		size_t count, loff_t * ppos)
{
	struct plx_device* xdev = (struct plx_device *)file->private_data;
	char temp[128];
	unsigned int* vt;
	u64 write_offset;
	u32 write_val;

	if(count >= 128) {
		return -E2BIG;
	}

	if(copy_from_user(temp, buff, count)) {
		return -EFAULT;
	}

	temp[count] = '\0';

	sscanf(temp, "%llx %x", &write_offset, &write_val);

	vt = plx_ioremap(xdev, write_offset, 4);
	if (!vt)
		return 0;

	*vt = write_val;
	wmb();

	plx_iounmap(xdev, vt);
	vt = NULL;

	return count;
}

static const struct file_operations bar_write_ops = {
	.owner = THIS_MODULE,
	.open = bar_write_open,
	.write = bar_write_write
};

static char *gpio_output = NULL;
static size_t const gpio_output_size = 4*1024;
static char *gpio_output_write = NULL;
static size_t gpio_output_write_size;
enum gpio_command_mode{
	GPIO_MODE_EXECUTE,
	GPIO_MODE_SIMULATE,
	GPIO_MODE_PARSE
};

static int gpio_open(struct inode* inode, struct file *file)
{
	char *tmp = NULL;
	char *end = NULL;
	file->private_data = inode->i_private;
	if (gpio_output) {
		return 0;
	}
	gpio_output = kmalloc(gpio_output_size, GFP_KERNEL);
	if (!gpio_output) {
		return -ENOMEM;
	}

	tmp = gpio_output;
	end = gpio_output + gpio_output_size;
	tmp += snprintf(tmp, end - tmp,
		"GPIO control signals\n"
		"Read from gpio file returns result of last write\n"
		"Write to gpio file executes commands:\n"
		"    [MODE],s[VALUE]:[MASK]:[REG_ADDR],w[TIME_MS],"
		"s[VALUE]:[MASK]:[REG_ADDR],r[REG_ADDR]...\n"
		"    MODE: e - execute, s - simulate, p - parse\n"
		"    STEP: s - set bits in mask 's[VALUE]:[MASK]:[REG_ADDR]'"
		", w - wait 'w[TIME_MS]', r - read 'r[REG_ADDR]\n"
	);

	tmp += snprintf(tmp, end - tmp,
		"Examples: e/s/p,s0x[VAL]:0x[MASK]:0x[ADDR],w[TIME_MS],r0x[ADDR]\n"
		" 'echo s,r0x624,s0x4:0x4:0x624,w24,s0x0:0x4:0x624,r0x624 > gpio && "
		"cat gpio'\n"
		"       ^ Simulate command, to real change GPIO use e\n"
		"         ^ Read register 0x624 and print value\n"
		"                ^ Set bit 2 in register 0x624\n"
		"                               ^ Wait 24ms\n"
		"     Clear bit 2 in register 0x624 ^\n"
		"              Read register 0x624 and print value ^\n"
		"                            Read file return result of all command "
		"^\n");
	gpio_output_write = tmp;
	gpio_output_write_size = end - tmp;
	return 0;
}

static void gpio_clean(void)
{
	if (gpio_output) {
		kfree(gpio_output);
		gpio_output = NULL;
		gpio_output_write = NULL;
	}
}

static ssize_t gpio_read(struct file *file, char __user * buf,
		size_t count, loff_t * pos)
{
	size_t size;
	if (!gpio_output_write)
		return 0;

	gpio_output_write[gpio_output_write_size-1] = '\0';
	size = strlen(gpio_output);
	return simple_read_from_buffer(buf, count, pos,
			(char *)((uintptr_t)gpio_output + (uintptr_t)*pos),
			(uintptr_t)size - (uintptr_t)*pos);
}

static int gpio_run_step(struct plx_device *xdev, char **tmp, char *end,
		char *step, enum gpio_command_mode mode)
{
	char empty[256] = "";
	int ret = 0;
	int parse;

	switch (*step) {
	case 's': /* Set bit in register */
	{
		u32 value = 0x0;
		u32 mask = 0x0;
		u32 addr = 0x0;

		parse = sscanf(step, "s0x%x:0x%x:0x%x%255s",
				&value, &mask, &addr, empty);
		if (parse != 3 || strlen(empty)) {
			*tmp += snprintf(*tmp, end - *tmp, "ERROR: Wrong parse '%s': "
					"value 0x%x mask 0x%08x addr 0x%x, unexpected text '%s', "
					"parsed %u/3 ", step, value, mask, addr, empty, parse);
			ret = -1;
		} else {
			*tmp += snprintf(*tmp, end - *tmp, "Set value 0x%08x mask 0x%08x "
					"addr 0x%08x", value, mask, addr);
			if (value & ~mask) {
				*tmp += snprintf(*tmp, end - *tmp, " bits value are out of mask"
						" 0x%08x ", value & ~mask);
				ret = -1;
			} else if (GPIO_MODE_PARSE == mode) {
				*tmp += snprintf(*tmp, end - *tmp, " ");
			} else if (GPIO_MODE_EXECUTE == mode) {
				u32 data_read;
				u32 data;

				mutex_lock(&xdev->mmio_lock);
				data_read = plx_mmio_read(&xdev->mmio, addr);
				data = (data_read & (~mask)) | (value & mask);
				plx_mmio_write(&xdev->mmio, data, addr);
				mutex_unlock(&xdev->mmio_lock);
				dev_info( &xdev->pdev->dev, "[%06x]<-%x with mask %x\n", addr, value, mask );

				*tmp += snprintf(*tmp, end - *tmp, " read 0x%08x write "
						"0x%08x\n", data_read, data);
			} else {
				*tmp += snprintf(*tmp, end - *tmp, "\n");
			}
		}
		break;
	}
	case 'w': /* Wait */
	{
		unsigned int time = 0;
		parse = sscanf(step, "w%u%255s", &time, empty);
		if (parse != 1 || strlen(empty)) {
			*tmp += snprintf(*tmp, end - *tmp, "ERROR: Wrong parse '%s': "
					"time %u, unexpected text '%s', parsed %u/1 ",
					step, time, empty, parse);
			ret = -1;
		} else {
			*tmp += snprintf(*tmp, end - *tmp, "Wait: %u[ms]", time);
			if (GPIO_MODE_PARSE == mode) {
				*tmp += snprintf(*tmp, end - *tmp, " ");
			} else {
				*tmp += snprintf(*tmp, end - *tmp, "\n");
				msleep(time);
			}
		}
		break;
	}
	case 'r': /* Read register */
	{
		u32 addr = 0x0;

		parse = sscanf(step, "r0x%x%255s", &addr, empty);
		if (parse != 1 || strlen(empty)) {
			*tmp += snprintf(*tmp, end - *tmp, "ERROR: Wrong parse '%s':"
					" addr 0x%x, unexpected text '%s', parsed %u/2 ",
					step, addr, empty, parse);
			ret = -1;
		} else {
			*tmp += snprintf(*tmp, end - *tmp, "Read address 0x%08x", addr);

			if (GPIO_MODE_PARSE == mode) {
				*tmp += snprintf(*tmp, end - *tmp, " ");
			} else if (GPIO_MODE_EXECUTE == mode) {
				u32 data_read;
				mutex_lock(&xdev->mmio_lock);
				data_read = plx_mmio_read(&xdev->mmio, addr);
				mutex_unlock(&xdev->mmio_lock);
				*tmp += snprintf(*tmp, end - *tmp, ": 0x%08x\n", data_read);
			} else {
				*tmp += snprintf(*tmp, end - *tmp, ": 0x??simulate??\n");
			}
		}
		break;
	}
	case '\0':
	{
		*tmp += snprintf(*tmp, end - *tmp, "ERROR: Command is empty! ");
		ret= -1;
		break;
	}
	default:
		*tmp += snprintf(*tmp, end - *tmp, "ERROR: Unknown step: '%c' ", *step);
		ret = -1;
	}

	return ret;
}

static int gpio_run_commands(struct plx_device *xdev, char **tmp, char *end,
		char *cmd, enum gpio_command_mode mode)
{
	char *cmd_end = cmd + strlen(cmd);
	char step[256];
	int ret = 0;

	while (cmd < cmd_end)
	{
		char *next_sep = strchr(cmd,',');
		size_t size;

		if (next_sep) {
			size = next_sep - cmd;
		} else {
			size = cmd_end - cmd;
		}
		if (size >= sizeof(step)) {
			*tmp += snprintf(*tmp, end - *tmp, "ERROR: Too long step %lu, "
					"text '%s'\n", size, cmd);
			ret = -1;
			break;
		}
		strncpy(step, cmd, size);
		step[size] = '\0';

		if (GPIO_MODE_PARSE == mode) {
			int ret_tmp;
			*tmp += snprintf(*tmp, end - *tmp, "Parse step: '%s': ", step);
			ret_tmp = gpio_run_step(xdev, tmp, end, step, mode);
			if (ret_tmp) {
				ret = ret_tmp;
				*tmp += snprintf(*tmp, end - *tmp, "!ERROR!\n");
			} else {
				*tmp += snprintf(*tmp, end - *tmp, "DONE\n");
			}
		} else {
			ret = gpio_run_step(xdev, tmp, end, step, mode);
			if (ret)
				break;
		}
		cmd += size + 1;
	}
	return ret;
}

static ssize_t gpio_write(struct file* file, const char* __user buff,
		size_t count, loff_t * ppos)
{
	struct plx_device *xdev = file->private_data;
	char *tmp = gpio_output_write;
	char *end = gpio_output_write + gpio_output_write_size;
	char *cmd_buff = NULL;
	size_t cmd_buff_size = 4096;

	if (!gpio_output_write)
		return 0;

	/* Clean output */
	tmp[0] = '\0';

	tmp += snprintf(tmp, end - tmp, "================\n");

	if(!count) {
		tmp += snprintf(tmp, end - tmp, "ERROR: Empty command!\n");
		return -EINVAL;
	}

	if(count >= cmd_buff_size) {
		tmp += snprintf(tmp, end - tmp,
				"ERROR: Too long command %lu, max %lu\n", count, cmd_buff_size);
		return -E2BIG;
	}

	cmd_buff = kmalloc(cmd_buff_size, GFP_KERNEL);
	if (!cmd_buff)
		return -ENOMEM;

	if(copy_from_user(cmd_buff, buff, count)) {
		kfree(cmd_buff);
		tmp += snprintf(tmp, end - tmp,
				"ERROR: %s:%d copy_from_user err\n", __FILE__, __LINE__);
		return -EFAULT;
	}
	cmd_buff_size = count;
	if (cmd_buff[cmd_buff_size - 1] == '\n') {
		--cmd_buff_size;
	}
	cmd_buff[cmd_buff_size] = '\0';

	tmp += snprintf(tmp, end - tmp, "OUTPUT FOR COMMAND: '%s'\n", cmd_buff);
	/* Parse input data BEGIN */
	{
		enum gpio_command_mode mode;
		if (cmd_buff_size < 3) {
			tmp += snprintf(tmp, end - tmp, "ERROR: command too short\n");
			goto end_parse;
		}
		if (cmd_buff[0] == 'e') {
			mode = GPIO_MODE_EXECUTE;
			tmp += snprintf(tmp, end - tmp, "Mode: EXECUTE\n");
		} else if (cmd_buff[0] == 's') {
			mode = GPIO_MODE_SIMULATE;
			tmp += snprintf(tmp, end - tmp, "Mode: SIMULATE ONLY\n");
		} else if (cmd_buff[0] == 'p') {
			mode = GPIO_MODE_PARSE;
			tmp += snprintf(tmp, end - tmp, "Mode: PARSE ONLY\n");
		} else {
			tmp += snprintf(tmp, end - tmp, "ERROR: unknown mode '%c' "
					"expected e/s/p\n", cmd_buff[0] );
			goto end_parse;
		}
		if (cmd_buff[1] != ',') {
			tmp += snprintf(tmp, end - tmp, "ERROR: after mode '%c' "
					"expected separator ',' but got '%c'\n",
					cmd_buff[0], cmd_buff[1]);
			goto end_parse;
		}

		cmd_buff += 2;
		if (gpio_run_commands(xdev, &tmp, end, cmd_buff, GPIO_MODE_PARSE)) {
			tmp += snprintf(tmp, end - tmp, "ERROR: Parse command failed\n");
			goto end_parse;
		}
		tmp += snprintf(tmp, end - tmp, "Start execute:\n");
		if (mode != GPIO_MODE_PARSE && gpio_run_commands(xdev, &tmp, end,
					cmd_buff, mode)) {
			tmp += snprintf(tmp, end - tmp, "ERROR: Run command failed\n");
			goto end_parse;
		}
		tmp += snprintf(tmp, end - tmp, "Finish successful\n");
	}
	end_parse:
	/* Parse input data END */
	tmp += snprintf(tmp, end - tmp, "END\n");
	/* Close buffer */
	*(min(tmp, end -1)) = '\0';
	kfree(cmd_buff);
	return count;
}

static const struct file_operations gpio_ops = {
	.owner = THIS_MODULE,
	.open = gpio_open,
	.read = gpio_read,
	.write = gpio_write
};

#endif /* PLX_MEM_DEBUG */

static int plx_spad_show(struct seq_file *s, void *pos)
{
	struct plx_device *xdev = s->private;
	int i;
	for (i = 0; i < 8; i++) {
		u32 reg = plx_read_spad(xdev, i);
		seq_printf(s, "%d:\t%08x\n", i, reg);
	}
	return 0;
}

static int plx_spad_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, plx_spad_show, inode->i_private);
}

static const struct file_operations spad_ops = {
	.owner   = THIS_MODULE,
	.open    = plx_spad_debug_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = single_release
};

static int plx_alm_show(struct seq_file *s, void *pos)
{
	struct plx_device *xdev = s->private;
	struct plx_alm *alm = &xdev->a_lut_manager;
	unsigned int i;

	seq_printf(s, "current mappings: bits:%016llx\n", ~((u64)alm->segment_size - 1));
	for (i=0; i<alm->segments_num; i++) {
		seq_printf(s, "[%02x] from:%p to:%016llx ref_cnt:%x start:%x segments_count:%x\n",
			/* ID */                i,
			/*from*/                (char*)xdev->aper.va + i * alm->segment_size,
			/* to */                alm->entries[i].value,
			/* ref_cnt*/            alm->entries[i].counter,
			/* start */             alm->entries[i].start,
			/* segments_count */    alm->entries[i].segments_count);
	}

	return 0;
}

static int plx_alm_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, plx_alm_show, inode->i_private);
}

static const struct file_operations alm_ops = {
	.owner   = THIS_MODULE,
	.open    = plx_alm_debug_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = single_release
};

static irqreturn_t plx_test_db(int irq, void *data)
{
	u64 i = (u64)data;

	pr_info("Doorbell interrupt fired for %lld\n", i);
	return IRQ_HANDLED;
}

static struct vca_irq *cookie[16];

static int plx_intr_req_show(struct seq_file *s, void *pos)
{
	struct plx_device *xdev = s->private;
	int i;
	u64 db;

	for (i = 0; i < 16; i++) {
		db = plx_next_db(xdev);
		cookie[i] = plx_request_threaded_irq(xdev, plx_test_db,
						     NULL, "plx-test",
						     (void *)db, db);
	}
	return 0;
}

static int plx_intr_req_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, plx_intr_req_show, inode->i_private);
}

static const struct file_operations intr_req_ops = {
	.owner   = THIS_MODULE,
	.open    = plx_intr_req_debug_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = single_release
};

static int plx_intr_free_show(struct seq_file *s, void *pos)
{
	struct plx_device *xdev = s->private;
	int i;

	for (i = 0; i < 16; i++)
		plx_free_irq(xdev, cookie[i], xdev);
	return 0;
}

static int plx_intr_free_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, plx_intr_free_show, inode->i_private);
}

static const struct file_operations intr_free_ops = {
	.owner   = THIS_MODULE,
	.open    = plx_intr_free_debug_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = single_release
};

static int plx_intr_send_show(struct seq_file *s, void *pos)
{
	struct plx_device *xdev = s->private;
	int i;

	for (i = 0; i < 16; i++)
		plx_send_intr(xdev, i);
	return 0;
}

static int plx_intr_send_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, plx_intr_send_show, inode->i_private);
}

static const struct file_operations intr_send_ops = {
	.owner   = THIS_MODULE,
	.open    = plx_intr_send_debug_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = single_release
};

static int plx_dump_regs_complete(struct seq_file *s, void *pos)
{
	struct plx_device *xdev = s->private;
	int i;
	u32* reg_ptr = xdev->mmio.va;

	for (i = 0; i < 0x10000; i++)
		seq_printf(s, "[0x%05x] = %08x\n", i*4, reg_ptr[i]);
	return 0;
}

static int plx_dump_regs_complete_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, plx_dump_regs_complete, inode->i_private);
}

static const struct file_operations dump_regs_complete_ops = {
	.owner   = THIS_MODULE,
	.open    = plx_dump_regs_complete_debug_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = single_release
};

static int plx_dump_regs_single(struct seq_file *s, void *pos)
{
	struct plx_device *xdev = s->private;
	int i;
	u32* reg_ptr = xdev->mmio.va + xdev->reg_base;

	for (i = 0; i < 0x400; i++)
		seq_printf(s, "[0x%04x] = %08x\n", i*4, reg_ptr[i]);
	return 0;
}

static int plx_dump_regs_single_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, plx_dump_regs_single, inode->i_private);
}

static const struct file_operations dump_regs_single_ops = {
	.owner   = THIS_MODULE,
	.open    = plx_dump_regs_single_debug_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = single_release
};

static int plx_e3s10_gpio_single_open(struct seq_file *s, void *pos)
{
	struct plx_device *const xdev = s->private;
	u32 const straps= plx_read_straps( xdev);
	u32 const direction= plx_mmio_read( &xdev->mmio, PLX_GPIO_DIRECTION1); // for 0-9 only
	u32 const trough= plx_mmio_read( &xdev->mmio, PLX_GPIO_INPUT);
	int i;
	for( i= 9; i>= 0; --i) {
		static char const* const direction_[ 8]= {
			"input", "interupt", "reserved", "reserved",
			"outputGpio624", "outputGpio", "shp_perst", "reserved" };
		static const char* const trough_[ 2]= {
			"zero", "voltage" };
		static char const * const desc[ 10]={
			"caterr",
			"reset cpu",
			"reset fpga",
			"unused",
			"bios recovery",
			"board id",
			"board id",
			"board id",
			"cpu power good",
			"pch power button" };
		seq_printf(s, "%2u\t%16s\t%s\t%x\t%s\n",
				i,
				direction_[ 7 & (direction>>(i*3))],
				trough_[ 1 & (trough>>i)],
				1 & (straps>>i),
				desc[ i]);
	}
	return 0;
}

static int plx_e3s10_gpio_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, plx_e3s10_gpio_single_open, inode->i_private);
}

static const struct file_operations e3s10_gpio_ops = {
	.owner   = THIS_MODULE,
	.open    = plx_e3s10_gpio_debug_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = single_release
};


static ssize_t plx_read_base(struct file* f, char __user* u, size_t s, loff_t* o) {
    struct plx_device* xdev= f->private_data;
    if(( s>= 4)&&!( *o& ~0xffc)) // must be aligned to 4 and be on 1 page
    {
        put_user( plx_mmio_read( &xdev->mmio, xdev->reg_base+ *o),(u32 __user*) u);
        *o+= 4;
        return 4;
    }
    return ( *o== PAGE_SIZE)? 0: -EIO;
}


static ssize_t plx_read_peer(struct file* f, char __user* u, size_t s, loff_t* o) {
    struct plx_device* xdev= f->private_data;
    if(( s>= 4)&&!( *o& ~0xffc)) // must be aligned to 4 and be on 1 page
    {
        put_user( plx_mmio_read( &xdev->mmio, xdev->reg_base_peer+ *o),(u32 __user*) u);
        *o+= 4;
        return 4;
    }
    return ( *o== PAGE_SIZE)? 0: -EIO;
}


static int plx_open(struct inode* i, struct file* f) {
	struct plx_device* xdev=(struct plx_device*) i->i_private;
	f->private_data= xdev;
	return 0;
}

/**
 * plx_create_debug_dir - Initialize VCA debugfs entries.
 */
void plx_create_debug_dir(struct plx_device *xdev)
{
	if (!plx_dbg)
		return;

	xdev->dbg_dir = debugfs_create_dir(dev_name(&xdev->pdev->dev), plx_dbg);
	if( IS_ERR_OR_NULL( xdev->dbg_dir))
		return;

	debugfs_create_file("msi_irq_info", 0444, xdev->dbg_dir, xdev,
			    &msi_irq_info_ops);

	debugfs_create_file("spad", 0444, xdev->dbg_dir, xdev,
			    &spad_ops);

	debugfs_create_file("intr_req", 0444, xdev->dbg_dir, xdev,
			    &intr_req_ops);

	debugfs_create_file("intr_free", 0444, xdev->dbg_dir, xdev,
			    &intr_free_ops);

	debugfs_create_file("intr_send", 0444, xdev->dbg_dir, xdev,
			    &intr_send_ops);
#if PLX_MEM_DEBUG
	debugfs_create_file("memory_write", 0444, xdev->dbg_dir, xdev,
			    &plx_memory_write_ops);

	debugfs_create_file("memory_read", 0444, xdev->dbg_dir, xdev,
			    &plx_memory_read_ops);

	debugfs_create_file("local_read", 0444, xdev->dbg_dir, xdev,
			    &local_read_ops);

	debugfs_create_file("bar_read", 0444, xdev->dbg_dir, xdev,
			    &bar_read_ops);

	debugfs_create_file("bar_write", 0444, xdev->dbg_dir, xdev,
			    &bar_write_ops);

	debugfs_create_file("gpio", 0644,  xdev->dbg_dir, xdev,
			    &gpio_ops);
#endif
	debugfs_create_file("dump_regs_complete", 0444, xdev->dbg_dir, xdev,
			    &dump_regs_complete_ops);

	debugfs_create_file("dump_regs_single", 0444, xdev->dbg_dir, xdev,
			    &dump_regs_single_ops);

	debugfs_create_file("alm_state", 0444, xdev->dbg_dir, xdev,
			    &alm_ops);
	{
		static struct file_operations const ops= { .read= plx_read_eeprom0, .open= plx_open, .owner= THIS_MODULE};
		struct dentry*const d= debugfs_create_file( "eeprom", 0440, xdev->dbg_dir, xdev, &ops);
		if(! IS_ERR_OR_NULL( d))
			d->d_inode->i_size= 0x4000;
	}
	switch(xdev->card_type) {
	case VCA_FPGA_FAB1:
	case VCA_VCAA_FAB1:
	case VCA_VCGA_FAB1:
		debugfs_create_file("gpio", 0444, xdev->dbg_dir, xdev, &e3s10_gpio_ops);
		break;
	default:;
	}
	{
		static const struct file_operations ops = { .read= plx_read_base, .open = plx_open, .owner= THIS_MODULE};
		struct dentry* d= debugfs_create_file( "base.ntb", 0440, xdev->dbg_dir, xdev, &ops);
		if(!IS_ERR_OR_NULL( d)) d->d_inode->i_size= PAGE_SIZE;
	}
	{
		static const struct file_operations ops = { .read= plx_read_peer, .open = plx_open, .owner= THIS_MODULE};
		struct dentry* d= debugfs_create_file( "peer.ntb", 0440, xdev->dbg_dir, xdev, &ops);
		if(!IS_ERR_OR_NULL( d)) d->d_inode->i_size= PAGE_SIZE;
	}
}

/**
 * plx_delete_debug_dir - Uninitialize VCA debugfs entries.
 */
void plx_delete_debug_dir(struct plx_device *xdev)
{
	if (!xdev->dbg_dir)
		return;

	debugfs_remove_recursive(xdev->dbg_dir);
}

/**
 * plx_init_debugfs - Initialize global debugfs entry.
 */
void __init plx_init_debugfs(void)
{
	plx_dbg = debugfs_create_dir(KBUILD_MODNAME, NULL);
	if( IS_ERR_OR_NULL( plx_dbg))
		pr_err("can't create debugfs dir\n");
}

/**
 * plx_exit_debugfs - Uninitialize global debugfs entry
 */
void plx_exit_debugfs(void)
{
#if PLX_MEM_DEBUG
	gpio_clean();
#endif
	debugfs_remove(plx_dbg);
}

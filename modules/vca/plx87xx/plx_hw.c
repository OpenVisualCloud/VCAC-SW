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

#include <linux/fs.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/firmware.h>
#include <linux/completion.h>
#include <linux/delay.h>


#include "../common/vca_common.h"
#include "plx_device.h"
#include "plx_hw.h"
#include "plx_lbp.h"
#include <linux/module.h>

#define VIRTUAL_DBG_SW_REG 0xA30
#define MARGIN_TIME          8
#define POWER_OFF_HOLD_TIME   (5200 + (MARGIN_TIME))
#define POWER_OFF_PULSE_TIME  (200 + (MARGIN_TIME))
#define RESET_PULSE_TIME	(16 + (MARGIN_TIME))
#define GPIO_DEF_WAIT_TIME	(500 + (MARGIN_TIME))

#define PV_SIGIGNATURE	0x0
#define PV_VERSION		0x4
#define PV_ROOTBUS		0x8
#define PV_ROOTPORT		0xc
#define PV_DMABUS		0x10

static bool DontCheckEepromCrc = false;
module_param(DontCheckEepromCrc, bool, 0644);
MODULE_PARM_DESC(DontCheckEepromCrc, "Skip crc check for EEPROM");

bool kvm_check_guest(void)
{
	unsigned int eax, ebx, ecx, edx;
	char s[12];
	unsigned int *i;

	/* KVM_CPUID_SIGNATURE */
	eax = 0x40000000;
	ebx = ecx = edx = 0;

	asm volatile ("cpuid"
		: "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
		:
		: "cc", "memory");
	i = (unsigned int *)s;
	i[0] = ebx;
	i[1] = ecx;
	i[2] = edx;

	return !strncmp(s, "KVMKVMKVM", strlen("KVMKVMKVM"));
}


struct GpioVV { u32
	unused0:1,  // 1<<0
	Reset0:1,   // 1<<1 PLX_VV_CPU0_RESET_BIT
	Reset1:1,   // 1<<2 PLX_VV_CPU1_RESET_BIT
	Reset2:1,   // 1<<3 PLX_VV_CPU2_RESET_BIT
	unused4:1,  // 1<<4
	unused5:1,  // 1<<5
	unused6:1,  // 1<<6
	unused7:1,  // 1<<7
	unused8:1,  // 1<<8
	unused9:1,  // 1<<9
	unused10:1, // 1<<10
	unused11:1; // 1<<11
};

struct GpioMV { u32
	ResetM2:1,       // 1<<0 PLX_M2_RESET_BIT
	Reset1:1,        // 1<<1 PLX_MV_CPU1_RESET_BIT
	Reset0:1,        // 1<<2 PLX_MV_CPU0_RESET_BIT
	Reset2:1,        // 1<<3 PLX_MV_CPU2_RESET_BIT
	BiosRevovery0:1, // 1<<4 PLX_BIOS_RCV_MODE_CPU0
	BiosRecovery2:1, // 1<<5 PLX_BIOS_RCV_MODE_CPU2
	BiosRevocery1:1, // 1<<6 PLX_BIOS_RCV_MODE_CPU1
	CardReset:1,     // 1<<7 PLX_CARD_RESET_BIT
	PowerButton2:1,  // 1<<8 PLX_CPU2_POWER_BIT
	PowerButton0:1,  // 1<<9 PLX_CPU0_POWER_BIT
	PowerButton1:1,  // 1<<10 PLX_CPU1_POWER_BIT
	unconnected11:1; // 1<<11
};

struct GpioE3S10 { u32 // 1008
	CatErr:1,          // 1<<0
	CpuReset:1,        // 1<<1
	FpgaPowerButton:1, // 1<<2
	unconnected3:1,    // 1<<3
	BiosRecovery:1,    // 1<<4
	BoardId0:1,        // 1<<5
	BoardId1:1,        // 1<<6
	BoardId2:1,        // 1<<7
	unconnected8:1,    // 1<<8
	CpuPowerButton:1,  // 1<<9
	unconnected10:1;   // 1<<10
};

struct GpioVcga { u32
	CatErr0:1,       // 1<<0
	CpuReset0:1,     // 1<<1 PLX_VCGA_CPU0_RESET_BIT
	CpuReset1:1,     // 1<<2 PLX_VCGA_CPU1_RESET_BIT
	CardId:1,        // 1<<3 PLX_CARD_LED_BIT
	BiosRecovery0:1, // 1<<4 PLX_VCGA_BIOS_RCV_MODE_CPU0
	BiosRecovery1:1, // 1<<5 PLX_VCGA_BIOS_RCV_MODE_CPU1
	PowerOk0:1,      // 1<<6
	CatErr1:1,       // 1<<7
	PowerOk1:1,      // 1<<8
	PowerButton0:1,  // 1<<9
	PowerButton1:1,  // 1<<10
	unconnected11:1; // 1<<11
};

struct GpioVcaa { u32
	CatErr:1,        // 1<<0
	CpuReset:1,      // 1<<1
	unconnected2:1,  // 1<<2
	CardId:1,        // 1<<3 PLX_CARD_LED_BIT
	BiosRecovery:1,  // 1<<4
	unconnected5:1,  // 1<<5
	PowerOk:1,       // 1<<6
	unconnected7:1,  // 1<<7
	unconnected8:1,  // 1<<8
	PowerButton:1,   // 1<<9
	unconnected10:1; // 1<<10
};

union Gpio {
	u32 all;
	struct GpioVcga Vcga;   // SUBSYSTEM_ID_VCGA_FAB1
	struct GpioVcaa Vcaa;   // SUBSYSTEM_ID_VCAA_FAB1
	struct GpioE3S10 E3S10; // SUBSYSTEM_ID_FPGA_FAB1
	struct GpioMV MV;       // SUBSYSTEM_ID_MV_FAB1
	struct GpioVV VV;       // SUBSYSTEM_ID_VV_FAB2
};

static const u32 plx_reset_bits[2][3] = {
		{
				PLX_VV_CPU0_RESET_BIT,
				PLX_VV_CPU1_RESET_BIT,
				PLX_VV_CPU2_RESET_BIT
		},
		{
				PLX_MV_CPU0_RESET_BIT,
				PLX_MV_CPU1_RESET_BIT,
				PLX_MV_CPU2_RESET_BIT
		}
};

const u32 plx_reset_bits_vcga[2] = {
		PLX_VCGA_CPU0_RESET_BIT,
		PLX_VCGA_CPU1_RESET_BIT
};

const u32 plx_bios_rcv_bits[3] = {
		PLX_BIOS_RCV_MODE_CPU0,
		PLX_BIOS_RCV_MODE_CPU1,
		PLX_BIOS_RCV_MODE_CPU2
};

const u32 plx_bios_rcv_bits_vcga[2] = {
	PLX_VCGA_BIOS_RCV_MODE_CPU0,
	PLX_VCGA_BIOS_RCV_MODE_CPU1
};

extern struct plx_device * plx_contexts[MAX_VCA_CARDS][MAX_VCA_CARD_CPUS];

/**
 * plx_a_lut_disable() - disable alut
 * @xdev: pointer to plx_device instance
 *
 * */
void plx_a_lut_disable(struct plx_device *xdev)
{
	plx_mmio_write(&xdev->mmio, 0, xdev->reg_base + PLX_A_LUT_CONTROL);
}

/*
 * plx_find_root_port - find root port which plx chip connects to
 *
 * @pdev: The PCIe device
 *
 * RETURNS: struct pci_dev *, NULL on don't find a root port device which will
 *          never happen.
 */
static struct pci_dev *
plx_find_root_port(struct pci_dev *pdev)
{
	for (pdev = pdev->bus->self;
		pdev && (pci_pcie_type(pdev) != PCI_EXP_TYPE_ROOT_PORT);
		pdev = pdev->bus->self)
		;
	return pdev;
}

/*
 * plx_find_root_complex_bus_num - find root complex bus number for a pdev
 *
 * @pdev: The PCIe device
 *
 * RETURNS: The root complex bus number
 */
static u8
plx_find_root_complex_bus_num(struct pci_dev *pdev)
{
	struct pci_bus *bus = pdev->bus;

	while (bus->parent)
		bus = bus->parent;

	return bus->number;
}

/*
 * plx_rid_lut - construct RID LUT value
 *
 * @root_port_bus: bus number of root port device
 * @root_port_dev: device number of root port device
 * @root_complex_bus: bus number of root complex device
 * @root_complex_dev: device number of root complex device
 *
 * RETURNS: RID LUT
 */
static inline u32
plx_rid_lut(u8 root_port_bus, u8 root_port_dev,
	    u8 root_complex_bus, u8 root_complex_dev)
{
	return ((root_port_bus << 24) | ((root_port_dev & 0x0f) << 19) |
		(root_complex_bus << 8) | ((root_complex_dev & 0x0f) << 3) |
		PLX_RID_LUT_ENABLE);
}

/*
 * plx_rid_lut_dma - construct RID LUT value for DMA
 *
 * @dma_bus: bus number of DMA device
 *
 * RETURNS: RID LUT
 */
static inline u32 plx_rid_lut_dma(u8 dma_bus, u8 dma_dev)
{
	return (dma_bus << 8) | ((dma_dev & 0x0f) << 3) | PLX_RID_LUT_ENABLE_1;
}

u32 plx_find_rootinfo(struct pci_dev *pdev)
{
	bool kvm;
	u32 ret;
	u32 rb, rp;
	int len;
	void *mmio, *p;
	struct pci_dev *root_port;
	u8 root_complex_bus_num;

	kvm = kvm_check_guest();
	if (kvm) {
		/* TODO: check signature */
		len = pci_resource_len(pdev, 0) / 2;

		mmio = pci_iomap(pdev, 0, ~0UL);
		p = mmio + len;

		rb = readl(p + PV_ROOTBUS);
		rp = readl(p + PV_ROOTPORT);

		ret = rb << 16 | rp;
		pci_iounmap(pdev, mmio);
		return ret;
	}

	root_port = plx_find_root_port(pdev);
	if (!root_port) {
		dev_err(&pdev->dev, "can't find root port\n");
		return -ENXIO;
	}
	root_complex_bus_num = plx_find_root_complex_bus_num(pdev);

	ret = root_complex_bus_num << 16 | PCI_DEVID(root_port->bus->number, root_port->devfn);
	return ret;
}

u32 plx_find_dma_bus_number(struct pci_dev *pdev)
{
	bool kvm;
	u32 ret = 0;
	u32 db;
	int len;
	void *mmio, *p;

	kvm = kvm_check_guest();
	if (kvm) {
		/* TODO: check signature */
		len = pci_resource_len(pdev, 0) / 2;

		mmio = pci_iomap(pdev, 0, ~0UL);
		p = mmio + len;

		db = readl(p + PV_DMABUS);

		ret = db;
		pci_iounmap(pdev, mmio);
		return ret;
	}

	return ret;
}

/*
 * plx_program_rid_lut_dma - program RID LUT for PLX DMA
 *
 * @xdev: pointer to plx_device instance
 *
 * RETURNS: 0 on success and errno on failure
 */
int plx_program_rid_lut_dma(struct plx_device *xdev)
{
	u8 dma_bus_number;
	u32 rid_lut;
	bool kvm;

	if (xdev->dma_ch) {
		kvm = kvm_check_guest();
		if (kvm) {
			dma_bus_number = plx_find_dma_bus_number(xdev->pdev);
			// dma_slot number can be hardcoded to 0x0 as it will never change to different value due to topology
			rid_lut = plx_rid_lut_dma(dma_bus_number, 0x0);
		}
		else {
			struct pci_dev const*const dma_dev = container_of(xdev->dma_ch->device->dev, struct pci_dev, dev);
			dma_bus_number = dma_dev->bus->number;
			rid_lut = plx_rid_lut_dma(dma_bus_number, PCI_SLOT(dma_dev->devfn));
		}
		
		plx_mmio_write(&xdev->mmio, rid_lut, xdev->reg_base+ PLX_RID_LUT_2_3 );
		dev_info(&xdev->pdev->dev, "DMA port bus: 0x%X, rid_lut: 0x%X\n", dma_bus_number, rid_lut);
		
		return 0;
	}
	dev_warn(&xdev->pdev->dev, "Missing DMA\t%s:%u\n", __func__, __LINE__);
	return ENODEV;
}

enum rid_lut_node_side_constants {
	RID_LUT_ROOTPORT_DEV_VCAA    = 0xc,
	RID_LUT_ROOTPORT_DEV_DEFAULT = 0x1,
	RID_LUT_DMA_BUS_VCGA         = 0x2,
	RID_LUT_DMA_BUS_DEFAULT      = 0x1,
};

/*
 * plx_program_rid_lut_for_node - apply RID LUT for node
 *
 * @xdev: pointer to plx_device instance
 *
 * RETURNS: 0 on success, otherwise errno
 */
int plx_program_rid_lut_for_node(struct plx_device *xdev)
{
	u32 rid_lut_link_side, rid_lut_dma_link_side, link_rid_offset;
	u32 rid_lut_link_side_old, rid_lut_dma_link_side_old;
	u8 root_port_dev, dma_bus;

	if (xdev->link_side == node_side)
		return 0;

	if (xdev->port_id == 0)
		link_rid_offset = PLX_NT0_RID_LUT_LINK_OFFSET; // NT0
	else
		link_rid_offset = PLX_NT1_RID_LUT_LINK_OFFSET; // NT1

	switch (xdev->card_type) {
	case VCA_VV_FAB1:
	case VCA_VV_FAB2:
	case VCA_MV_FAB1:
	case VCA_FPGA_FAB1:
		dma_bus = RID_LUT_DMA_BUS_DEFAULT;
		root_port_dev = RID_LUT_ROOTPORT_DEV_DEFAULT;
		break;
	case VCA_VCAA_FAB1:
		dma_bus = RID_LUT_DMA_BUS_DEFAULT;
		root_port_dev = RID_LUT_ROOTPORT_DEV_VCAA;
		break;
	case VCA_VCGA_FAB1:
		dma_bus = RID_LUT_DMA_BUS_VCGA;
		root_port_dev = RID_LUT_ROOTPORT_DEV_DEFAULT;
		break;
	default:
		dev_err(&xdev->pdev->dev, "%s: unknown card type\n", __func__);
		return ENODEV;
	}

	rid_lut_link_side = plx_rid_lut(0, root_port_dev, 0, 0);
	rid_lut_dma_link_side = plx_rid_lut_dma(dma_bus, VCA_DMA_DEVICE);
	rid_lut_link_side_old = plx_mmio_read(&xdev->mmio, link_rid_offset);
	rid_lut_dma_link_side_old = plx_mmio_read(&xdev->mmio, link_rid_offset + 4);
	if (rid_lut_link_side != rid_lut_link_side_old) {
		dev_info(&xdev->pdev->dev, "%s: changing link_side rid_lut from 0x%X to 0x%X\n",
				__func__, rid_lut_link_side_old, rid_lut_link_side);
		plx_mmio_write(&xdev->mmio, rid_lut_link_side, link_rid_offset);
	}
	if (rid_lut_dma_link_side_old != rid_lut_dma_link_side) {
		dev_info(&xdev->pdev->dev, "%s: changing DMA link_side rid_lut from 0x%X to 0x%X\n",
				__func__, rid_lut_dma_link_side_old, rid_lut_dma_link_side);
		plx_mmio_write(&xdev->mmio, rid_lut_dma_link_side, link_rid_offset + 4);
	}
	return 0;
}

/*
 * plx_program_rid_lut - program RID LUT
 *
 * @xdev: pointer to plx_device instance
 * @pdev: The PCIe device
 *
 * RETURNS: 0 on success and errno on failure
 */
static int
plx_program_rid_lut(struct plx_device *xdev, struct pci_dev *pdev)
{
	u32 rid_lut;
	u32 virtual_rid_offset, link_rid_offset;
	u32 rootinfo;
	u8 root_bus, rp_bus, rp_devfn;

	if (xdev->port_id == 0) {
		// NT0
		virtual_rid_offset = PLX_NT0_RID_LUT_VIRTUAL_OFFSET;
		link_rid_offset = PLX_NT0_RID_LUT_LINK_OFFSET;
	} else {
		// NT1
		virtual_rid_offset = PLX_NT1_RID_LUT_VIRTUAL_OFFSET;
		link_rid_offset = PLX_NT1_RID_LUT_LINK_OFFSET;
	}

	rootinfo = plx_find_rootinfo(pdev);
	if (rootinfo == 0) {
		dev_err(&pdev->dev, "can't find PCIe root information\n");
		return -ENXIO;
	}

	root_bus = rootinfo >> 16;
	rootinfo &= 0xffff;
	rp_bus = rootinfo >> 8;
	rp_devfn = PCI_SLOT(rootinfo & 0xff);
	
	dev_info(&xdev->pdev->dev, "root_bus:%x, rp_bus:%x, rp_devfn:%x\n", root_bus, rp_bus, rp_devfn);

	rid_lut = plx_rid_lut(rp_bus, rp_devfn, root_bus, 0);
	dev_info(&xdev->pdev->dev, "rid_lut: 0x%X\n", rid_lut);

	if (xdev->link_side) {
		plx_mmio_write(&xdev->mmio, rid_lut, link_rid_offset);
		dev_info(&xdev->pdev->dev, "link_side rid_lut: 0x%X\n", rid_lut);
	} else {
		plx_mmio_write(&xdev->mmio, rid_lut, virtual_rid_offset);
	}
	return 0;
}

/* plx_get_a_lut_entry_offset - calculate an offset to a given A-LUT entry
 * in A-LUT array. The offset is to a first subarray only- next subarrays
 * offsets needs to be calculated by own; function covers a case of
 * A-LUT table divided to two arrays
 *
 * @idx- index of an A-LUT entry
 *
 * RETURNS: offset of a given A-LUT entry, starting from the beginning of
 * a first array
 */
static inline unsigned int plx_get_a_lut_entry_offset(unsigned int idx)
{
	unsigned int array_offset = (idx >= PLX_A_LUT_MAX_ARRAY) ? PLX_A_LUT_ARRAY_OFFSET : 0;
	return array_offset +  (idx % PLX_A_LUT_MAX_ARRAY) * sizeof(u32);
}

/*
 * plx_a_lut_clear - clear A-LUT entries
 *
 * @xdev: pointer to plx_device instance
 * @offset: offset of A-LUT array
 *
 * RETURNS: nothing
 */
void
plx_a_lut_clear(struct plx_device* xdev, u32 offset)
{
	int i;
	unsigned int entry_offset;

	plx_alm_reset(&xdev->a_lut_manager, xdev->pdev);

	for(i=0; i<xdev->a_lut_manager.segments_num; i++)
	{
		entry_offset = plx_get_a_lut_entry_offset(i) + offset;

		plx_mmio_write(&xdev->mmio, 0, entry_offset +
			PLX_A_LUT_PERMISSION_SUBARRAY_OFFSET);
		plx_mmio_write(&xdev->mmio, 0, entry_offset +
			PLX_A_LUT_HIGHER_RE_MAP_SUBARRAY_OFFSET);
		plx_mmio_write(&xdev->mmio, 0, entry_offset +
			PLX_A_LUT_LOWER_RE_MAP_SUBARRAY_OFFSET);
	}
}

/*
 * _plx_configure_a_lut - enable A-LUT
 *
 * @xdev: pointer to plx_device instance
 * @pdev: The PCIe device
 *
 * RETURNS: 0 on success and errno on failure
 */
static int
_plx_a_lut_enable(struct plx_device *xdev, struct pci_dev *pdev)
{
	spin_lock(&xdev->alm_lock);
	plx_a_lut_disable(xdev);
	plx_a_lut_clear(xdev, xdev->a_lut_array_base);

	if (xdev->a_lut_peer)
		plx_mmio_write(&xdev->mmio, PLX_A_LUT_ENABLE,
			xdev->reg_base + PLX_A_LUT_CONTROL);

	spin_unlock(&xdev->alm_lock);

	return 0;
}

/*
 * plx_a_lut_peer_enable - enable A-LUT for peer
 *
 * @xdev: pointer to plx_device instance
 *
 * RETURNS: nothing
 */
void
plx_a_lut_peer_enable(struct plx_device *xdev)
{
	dev_dbg(&xdev->pdev->dev, "%s Enable A-LUT for peer reg peeer base %x, \n",
		__func__, xdev->reg_base_peer);

	plx_mmio_write(&xdev->mmio, PLX_A_LUT_ENABLE,
		xdev->reg_base_peer + PLX_A_LUT_CONTROL);
}

static u32 _plx_get_reg_base(u32 link_side, u32 port_id)
{
	return 0x3E000 - port_id * 0x2000 +  link_side * 0x1000;
}

/*
 * plx_check_eeprom - Check EEPROM validity and content CRC
 *
 * @xdev: pointer to plx_device instance
 *
 * RETURNS: 0 on success and errno on failure
 */
int plx_check_eeprom(struct plx_device *xdev)
{
	u32 val;
	u32 crc;

	if (DontCheckEepromCrc) {
		dev_info(&xdev->pdev->dev, "EEPROM CRC check skipped.\n");
		return 0;
	}

	val = plx_mmio_read(&xdev->mmio, PLX_EEP_STATUS_CONTROL);

	switch ((val & PLX_EEP_EEPPRSNT_MASK) >> PLX_EEP_EEPPRSNT_SHIFT) {
		case PLX_EEP_EEPPRSNT_NOT_PRESENT:
			dev_err(&xdev->pdev->dev, "EEPROM not present\n");
			return -EIO;
		case PLX_EEP_EEPRRSNT_SIGNATURE_OK:
			dev_dbg(&xdev->pdev->dev, "EEPROM present, signature valid\n");
			break;
		case PLX_EEP_EEPRRSNT_SIGNATURE_FAIL:
			dev_err(&xdev->pdev->dev, "EEPROM present, signature fail\n");
			return -EIO;
		default:
			dev_err(&xdev->pdev->dev, "Unknown EEPROM status\n");
			return -EIO;
	}

	crc = plx_mmio_read(&xdev->mmio, PLX_EEP_CRC);
	dev_info(&xdev->pdev->dev, "EEPROM CRC: 0x%08x\n", crc);

	if (val & PLX_EEP_EEPCRC_ERR_MASK) {
		dev_err(&xdev->pdev->dev, "EEPROM CRC check fail");
		return -EIO;
	}
	else {
		dev_info(&xdev->pdev->dev, "EEPROM CRC check OK\n");
	}

	return 0;
}

/*
 * plx_hw_init - Initialize any hardware specific information
 *
 * @xdev: pointer to plx_device instance
 * @pdev: The PCIe device
 *
 * RETURNS: 0 on success and errno on failure
 */
int plx_hw_init(struct plx_device *xdev, struct pci_dev *pdev)
{
	u32 val;
	int rc;
	unsigned alut_segments = 0;
	unsigned alut_ports = 0;

	spin_lock_init(&xdev->alm_lock);
	dev_dbg(&pdev->dev, "Device: %04x\n", xdev->pdev->device);
	switch(xdev->pdev->device) {
	case 0x2954:	xdev->port_id = 0; xdev->link_side =0; break;
	case 0x2955:	xdev->port_id = 1; xdev->link_side =0; break;
	case 0x2956:	xdev->port_id = 0; xdev->link_side =0; break;
	case 0x2958:	xdev->port_id = 0; xdev->link_side =1; break;
	case 0x2959:	xdev->port_id = 1; xdev->link_side =1; break;
	case 0x295a:	xdev->port_id = 0; xdev->link_side =1; break;
	default:dev_err(&pdev->dev, "Unsupporter device %x\n", xdev->pdev->device); return -ENODEV;
	}
	xdev->reg_base = _plx_get_reg_base(xdev->link_side, xdev->port_id);
	xdev->reg_base_peer = _plx_get_reg_base(!xdev->link_side, xdev->port_id);

	xdev->a_lut = false;
	xdev->a_lut_peer = false;
#ifdef VCA_ALUT_CARD_SIDE
	if (xdev->link_side) {
		xdev->a_lut = true;
	} else {
		xdev->a_lut_peer = true;
	}
#endif
#ifdef VCA_ALUT_HOST_SIDE
	if (xdev->link_side) {
		xdev->a_lut_peer = true;
	} else {
		xdev->a_lut = true;
	}
#endif

	if (!xdev->link_side) {
		rc = plx_check_eeprom(xdev);
		if (rc)
			return rc;
 	}

	if (xdev->link_side) {
		xdev->intr_reg_base = 0x10;
	} else {
		xdev->peer_intr_reg_base = 0x10;
	}

	/* Calculate A-LUT segments numbers */
	val =  plx_link_mmio_read( xdev, PLX_VS0UPSTREAM);

	if (val & PLX_ALUT_NT0_PORT_ENABLE) {
		++alut_ports;
	}
	if (val & PLX_ALUT_NT1_PORT_ENABLE) {
		++alut_ports;
	}

	if (!alut_ports) {
		dev_info(&xdev->pdev->dev, "Device 0x%x doesn't explicity identify VCA node. Assuming two NTBs. Switch to newer EEPROM\n",
			pdev->device);
		alut_ports = 2;
	}
	alut_segments = PLX_ALUT_SEGMENTS_NUM / alut_ports;

	if (alut_ports == 2) {
		if (!xdev->link_side)
			xdev->a_lut_array_base = (!xdev->port_id)?0x38000:0x3a000;
		else
			xdev->a_lut_array_base = (!xdev->port_id)?0x39000:0x3b000;
	} else {
		if (!xdev->link_side)
			xdev->a_lut_array_base = 0x38000;
		else
			xdev->a_lut_array_base = 0x3a000;
	}
	dev_info(&xdev->pdev->dev, "A-LUT array base port is %x\n",
		xdev->a_lut_array_base);

	rc = plx_program_rid_lut(xdev, pdev);
	if (rc) {
		dev_err(&pdev->dev, "can't program RID LUT: %d\n", rc);
		return rc;
	}

	rc = _plx_a_lut_enable(xdev, pdev);
	if (rc) {
		dev_err(&pdev->dev, "can't configure A LUT: %d\n", rc);
		return rc;
	}

	dev_info(&pdev->dev, "link_side %d reg_base 0x%x reg_base_peer 0x%x "\
		    "port id 0x%x a_lut %d a_lut_peer %d\n", xdev->link_side,
		    xdev->reg_base, xdev->reg_base_peer, xdev->port_id, xdev->a_lut,
		    xdev->a_lut_peer);

	spin_lock(&xdev->alm_lock);

	rc = plx_alm_init(&xdev->a_lut_manager, xdev->pdev,
			alut_segments, xdev->aper.len);

	dev_info(&xdev->pdev->dev,
		"programmed a lut segment size to %llx num segments:%x alut_ports:%x\n",
		xdev->a_lut_manager.segment_size, xdev->a_lut_manager.segments_num,
		alut_ports);

	spin_unlock(&xdev->alm_lock);

	return rc;
}

/*
 * plx_hw_deinit - Deinitialize any hardware
 *
 * @xdev: pointer to plx_device instance
 *
 * RETURNS: none.
 */
void
plx_hw_deinit(struct plx_device *xdev)
{
	spin_lock(&xdev->alm_lock);
	plx_alm_release(&xdev->a_lut_manager, xdev->pdev);
	spin_unlock(&xdev->alm_lock);
}

/**
 * plx_add_lut_entry() - add entry to A LUT array
 * @xdev: pointer to plx_device instance
 * @addr: DMA address to access memory
 *
 * This function allows other side of NTB to access address @addr by adding
 * a lookup entry to A LUT array.
 *
 * RETURNS: DMA addres to be used by the other side of NTB to access
 * @dma_addr.
 *
 * */
int
plx_add_a_lut_entry(struct plx_device *xdev, dma_addr_t addr, size_t size,
			dma_addr_t *addr_out)
{
	u32 lower_re_map_offset;
	u32 higher_re_map_offset;
	u32 permission_offset = 0;
	u64 translation_mask = xdev->a_lut_manager.segment_size - 1;
	dma_addr_t addr_masked = addr & ~translation_mask;

	int i;
	int err;
	unsigned int entry_offset;
	u32 segments_num;
	u32 segment_id;
	u32 last_permission_reg = 0;

	spin_lock(&xdev->alm_lock);

	err = plx_alm_add_entry(&xdev->a_lut_manager, xdev->pdev, addr,
		size, &segment_id, &segments_num);
	if (err && err != -EEXIST) {
		goto failed;
	}

	if (err == 0) {
		for (i = segment_id; i < segment_id + segments_num; i++) {
			entry_offset = xdev->a_lut_array_base + plx_get_a_lut_entry_offset(i);

			lower_re_map_offset = entry_offset +
					  PLX_A_LUT_LOWER_RE_MAP_SUBARRAY_OFFSET;
			higher_re_map_offset = entry_offset +
					  PLX_A_LUT_HIGHER_RE_MAP_SUBARRAY_OFFSET;
			permission_offset = entry_offset +
					  PLX_A_LUT_PERMISSION_SUBARRAY_OFFSET;

			plx_mmio_write(&xdev->mmio, (u32)(addr_masked >> 32),
				higher_re_map_offset);
			dev_dbg(&xdev->pdev->dev, "%s writing %x : %x\n",
				__func__, (u32)(addr_masked >> 32), higher_re_map_offset);
			plx_mmio_write(&xdev->mmio, (u32)(addr_masked), lower_re_map_offset);
			dev_dbg(&xdev->pdev->dev, "%s writing %x : %x\n",
				__func__,(u32)(addr_masked), lower_re_map_offset);
			wmb();
			plx_mmio_write(&xdev->mmio, PLX_A_LUT_PERMISSION_READ_ENABLE |
					PLX_A_LUT_PERMISSION_WRITE_ENABLE,
					permission_offset);
			dev_dbg(&xdev->pdev->dev, "%s writing %x : %x\n",
					__func__,PLX_A_LUT_PERMISSION_READ_ENABLE |
					PLX_A_LUT_PERMISSION_WRITE_ENABLE,
					permission_offset);
			last_permission_reg = permission_offset;
			addr_masked += xdev->a_lut_manager.segment_size;
		}

		mb();
		/*
		 * This is barrier to check that last write to register end,
		 * Check that hardware configuration finished.
		 **/
		if (last_permission_reg)
			plx_mmio_read(&xdev->mmio, last_permission_reg);
	}
	spin_unlock(&xdev->alm_lock);

	*addr_out = segment_id * (u64)xdev->a_lut_manager.segment_size +
		(addr & translation_mask);

	dev_dbg(&xdev->pdev->dev,
			"%s map entry no %i original %llx translated %llx\n",
			__func__,
			segment_id,
			(u64)addr,
			(u64)*addr_out);

	return 0;

failed:
	spin_unlock(&xdev->alm_lock);
	return -ENOMEM;
}

/**
 * plx_del_lut_entry() - delete entry from A LUT array
 * @xdev: pointer to plx_device instance
 * @addr: address in A-LUT segment
 *
 * This function allows other side of NTB to access address @addr by adding
 * a lookup entry to A LUT array.
 *
 * RETURNS: none.
 *
 * */
void plx_del_a_lut_entry(struct plx_device *xdev, dma_addr_t addr)
{
	u32 segment_id = addr / (u64)xdev->a_lut_manager.segment_size;
	u32 segments_num = 0;
	u32 start_segment = 0;

	if (addr >= xdev->a_lut_manager.segments_num *
		xdev->a_lut_manager.segment_size) {
			dev_err(&xdev->pdev->dev,
					"%s addres not in BAR range: %llx\n", __func__, (u64)addr);
			return;
	}

	spin_lock(&xdev->alm_lock);

	plx_alm_del_entry(&xdev->a_lut_manager, xdev->pdev, segment_id,
			&start_segment, &segments_num);

	if (segments_num) {
		u32 permission_offset;
		int i;

		dev_dbg(&xdev->pdev->dev, "%s delete entry no %i translated %llx\n",
				__func__, segment_id, (u64)addr);

		for (i = start_segment; i < start_segment + segments_num; i++) {

			permission_offset =  plx_get_a_lut_entry_offset(i) +
				      PLX_A_LUT_PERMISSION_SUBARRAY_OFFSET;
			plx_mmio_write(&xdev->mmio, 0, xdev->a_lut_array_base + permission_offset);
		}
	}

	spin_unlock(&xdev->alm_lock);
}

/**
 * plx_write_spad() - write to the scratchpad register
 * @xdev: pointer to plx_device instance
 * @idx: index to the scratchpad register, 0 based
 * @val: the data value to put into the register
 *
 * This function allows writing of a 32bit value to the indexed scratchpad
 * register.
 *
 * RETURNS: none.
 */
void plx_write_spad(struct plx_device *xdev, unsigned int idx, u32 val)
{
	dev_dbg(&xdev->pdev->dev, "Writing 0x%x to scratch pad index %d\n",
		val, idx);
	plx_mmio_write(&xdev->mmio, val, xdev->reg_base + PLX_SPAD0 + idx * 4);
}

/**
 * plx_read_spad() - read from the scratchpad register
 * @xdev: pointer to plx_device instance
 * @idx: index to scratchpad register, 0 based
 *
 * This function allows reading of the 32bit scratchpad register.
 *
 * RETURNS: An appropriate -ERRNO error value on error, or zero for success.
 */
u32 plx_read_spad(struct plx_device *xdev, unsigned int idx)
{
	u32 val = plx_mmio_read(&xdev->mmio,
				xdev->reg_base + PLX_SPAD0 + idx * 4);

	dev_dbg(&xdev->pdev->dev,
		"Reading 0x%x from scratch pad index %d\n", val, idx);
	return val;
}

/**
 * plx_enable_interrupts - Enable interrupts.
 * @xdev: pointer to plx_device instance
 */
void plx_enable_interrupts(struct plx_device *xdev)
{
	u32 offset = xdev->reg_base + xdev->intr_reg_base
			+ PLX_DBIMC;
	plx_mmio_write(&xdev->mmio, 0xFFFF, offset);
}

/**
 * plx_disable_interrupts - Disable interrupts.
 * @xdev: pointer to plx_device instance
 */
void plx_disable_interrupts(struct plx_device *xdev)
{
	u32 offset = xdev->reg_base + xdev->intr_reg_base
			+ PLX_DBIMS;
	plx_mmio_write(&xdev->mmio, 0xFFFF, offset);
}

/**
 * __plx_send_intr - Send interrupt to VCA.
 * @xdev: pointer to plx_device instance
 * @doorbell: doorbell number.
 */
void plx_send_intr(struct plx_device *xdev, int doorbell)
{
	u32 offset = xdev->reg_base + xdev->peer_intr_reg_base
			+ PLX_DBIS;

	plx_mmio_write(&xdev->mmio, DB_TO_MASK(doorbell), offset);
}

/**
 * plx_ack_interrupt - Device specific interrupt handling.
 * @xdev: pointer to plx_device instance
 *
 * Returns: bitmask of doorbell events triggered.
 */
u32 plx_ack_interrupt(struct plx_device *xdev)
{
	u32 offset = xdev->reg_base + xdev->intr_reg_base
			+ PLX_DBIC;
	u32 reg = plx_mmio_read(&xdev->mmio, offset);

	plx_mmio_write(&xdev->mmio, reg, offset);
	return reg;
}

/**
 * plx_hw_intr_init() - Initialize h/w specific interrupt
 * information.
 * @xdev: pointer to plx_device instance
 */
void plx_intr_init(struct plx_device *xdev)
{
	xdev->intr_info = (struct plx_intr_info *)_plx_intr_init;
}

/**
 * plx_dma_filter - DMA filter function
 * @chan: The DMA channel to compare with
 * @param: Data passed from DMA engine
 *
 * Returns: true if DMA device matches the PCIe device and false otherwise.
 */
bool plx_dma_filter(struct dma_chan *chan, void *param)
{
	struct device *dev = param;
	struct pci_dev *pdev = container_of(dev, struct pci_dev, dev);
	struct pci_dev *dma_dev =
		container_of(chan->device->dev, struct pci_dev, dev);
	u32 val;
	int rc;
	bool link_side;

	rc = pci_read_config_dword(pdev, PLX_PORT_ID, &val);
	if (rc) {
		dev_err(&pdev->dev, "can't read config dword: %d\n", rc);
		return false;
	}
	link_side = !!(val & (1 << 31));

	if (link_side)
	{
		/*
		 * On the link side any DMA device is accepted
		 */
		dev_info(dev, "%s returning true\n", __func__);
		return true;
	}
	else
	{
		/* On host side only DMA engine assigned to the currently used
		 * PCIe switch shall be used. Due to HW topology, it is at the
		 * same level, as an upsteram port, two buses above NT port
		 */
		dev_dbg(dev, "%s Host side DMA filter looks for DMA at bus 0x%x\n",
			__func__,
			pdev->bus->parent->parent->number);
		if (pdev->bus->parent->parent->number == dma_dev->bus->number)
		{
			dev_info(dev, "%s Host side DMA filter accepts DMA at bus 0x%x\n",
				__func__,
				dma_dev->bus->number);
			return true;
		}
		else
		{
			dev_dbg(dev, "%s Host side DMA filter rejects DMA at bus 0x%x\n",
				__func__,
				dma_dev->bus->number);
			return false;
		}
	}
}


/**
 * plx_ioremap() - remaps memory from physical address to virtual address
 * @xdev: pointer to plx_device instance
 * @len: length of memory
 *
 * RETURNS: pointer to mapped memory.
 */
void __iomem * plx_ioremap(struct plx_device *xdev, dma_addr_t pa, size_t len)
{
	dma_addr_t pa_out = pa;
	dev_dbg(&xdev->pdev->dev,  "%s physical address 0x%llx, len 0x%x\n",
		__func__, (u64) pa, (u32) len);

	if (xdev->a_lut) {
		if (plx_add_a_lut_entry(xdev, pa, len, &pa_out)) {
			dev_err(&xdev->pdev->dev,
				"cannot map pa in ALUT\n");
			return NULL;
		}
		dev_dbg(&xdev->pdev->dev,
			"%s Link side, ALUT translation done; remapping to 0x%llx\n",
			__func__,
			(u64) xdev->aper.va + pa_out);
	} else {
		dev_dbg(&xdev->pdev->dev,
			"%s Virtual side, no ALUT translation needed; direct remapping to 0x%llx\n",
			__func__,
			(u64) xdev->aper.va + pa);
	}

	return xdev->aper.va + pa_out;
}
EXPORT_SYMBOL_GPL(plx_ioremap);

/**
 * plx_iounmap() - unmap from virtual memory
 * @xdev: pointer to plx_device instance
 * @va: virtual address to unmap
 *
 * RETURNS: none.
 */
void plx_iounmap(struct plx_device *xdev, void __iomem *va)
{
	dma_addr_t pa = (u64)(va - xdev->aper.va);
	dev_dbg(&xdev->pdev->dev,  "%s virtual address 0x%llx and physical address 0x%llx\n", __func__, (u64) va, (u64) pa);

	if (xdev->a_lut) {
		plx_del_a_lut_entry(xdev, pa);
	}

}
EXPORT_SYMBOL_GPL(plx_iounmap);

/**
 * plx_link_width() -returns the width of link(0 - default value, link down, but in case of power button value is undefined)
 * @xdev: pointer to plx_device instance
 * RETURNS: true if link is up, false otherwise
 */
u32 plx_link_width(struct plx_device *xdev)
{
	u32 data;
	data = plx_link_mmio_read(xdev, PLX_LINK_STATUS_AND_CONTROL_REGISTER);

	if ((data & PLX_LINK_GEN_BITMASK) >> PLX_LINK_GEN_OFFSET != PLX_LINK_GEN3_VALUE)
		return 0;
	return (data & PLX_LINK_WIDTH_BITMASK) >> PLX_LINK_WIDTH_OFFSET;
}

/**
 * plx_link_status() -returns status of link, using read of SPAD register
 * @xdev: pointer to plx_device instance
 * RETURNS: true if link is up, false otherwise
 */
u32 plx_link_status(struct plx_device *xdev)
{
	u32 status;
	if (!plx_link_width(xdev))
		return 0;
	status = plx_lbp_get_state(xdev);
	return status != VCA_LINK_DOWN;
}

/**
 * plx_get_state() - return state of the card
 * @xdev: pointer to plx_device instance
 * RETURNS: true if link is up, false otherwise
 */
u32 plx_get_state(struct plx_device *xdev)
{
	dev_err(&xdev->pdev->dev, "%s not implemented\n", __func__);
	return 0;
}

/**
 * plx_get_cpu_num() - return number of cpu
 * @xdev: pointer to plx_device instance
 * RETURNS: number of CPUs available on card
 */
u32 plx_get_cpu_num(struct plx_device *xdev)
{
	u32 nums = 0;
	if (xdev->card_type & VCA_VV ) {
		nums = PLX_VV_CPU_NUMS;
	} else if (xdev->card_type & VCA_MV ) {
		nums = PLX_MV_CPU_NUMS;
	} else if (xdev->card_type & VCA_FPGA ) {
		nums = PLX_FPGA_CPU_NUMS;
    } else if (xdev->card_type & VCA_VCAA) {
		nums = PLX_VCAA_CPU_NUMS;
	} else if (xdev->card_type & VCA_VCGA) {
		nums = PLX_VCGA_CPU_NUMS;
	} else {
		dev_err(&xdev->pdev->dev, "Unknown Card %d\n", xdev->card_id);
	}
	return nums;
}

u32 plx_get_meminfo(struct plx_device *xdev)
{
	return xdev->lbp.i7_ddr_size_mb;
}

/**
 * signal_bit - Set input bit to 0 for defined amount of time
 * @xdev: pointer to plx_device instance
 * @signal_completion: for thread to complete when driver unloads
 * @bit: bit to set
 * @offset: register offset
 * @ms: amount of time in miliseconds to have bit set to 0
 * @wait_start: completion to trigger moment when state start change
 * @signal_release_ts: set jiffies when release signal
 */
static void signal_bit(struct plx_device *xdev,
		struct completion *signal_completion,
		u32 bit, u32 offset, u32 ms,
		struct completion *wait_start, u64 *signal_release_ts)
{
	bool signal_fail;
	u32 data;
	mutex_lock(&xdev->mmio_lock);
	data = plx_mmio_read(&xdev->mmio, offset);
	data &= ~bit;
	plx_mmio_write(&xdev->mmio, data, offset);
	mutex_unlock(&xdev->mmio_lock);

	if (wait_start)
		complete_all(wait_start);

	if (signal_completion)
		wait_for_completion_interruptible_timeout(signal_completion,
			    msecs_to_jiffies(ms));
	else
		msleep(ms);

	mutex_lock(&xdev->mmio_lock);
	if (signal_release_ts)
		*signal_release_ts = get_jiffies_64();
	data = plx_mmio_read(&xdev->mmio, offset);
	signal_fail = (data & bit);
	data |= bit;
	plx_mmio_write(&xdev->mmio, data, offset);
	mutex_unlock(&xdev->mmio_lock);

	if (signal_fail)
		dev_warn( &xdev->pdev->dev, "%s Unexpected reset signal, mask %08x"
			" offset %03x time_ms %u\n", __func__, bit, offset, ms);
}
/**
 * _set_bit - set input bit
 * @xdev: pointer to plx_device instance
 * @bit: bit to set
 * @offset: register offset
 */
static void _set_bit(struct plx_device *xdev, u32 bit, u32 offset)
{
	u32 data;
	mutex_lock(&xdev->mmio_lock);
	data = plx_mmio_read(&xdev->mmio, offset);
	data |= bit;
	plx_mmio_write(&xdev->mmio, data, offset);
	mutex_unlock(&xdev->mmio_lock);
}

/**
 * _clear - clear input bit
 * @xdev: pointer to plx_device instance
 * @bit: bit to reset
 * @offset: register offset
 */
static void _clear_bit(struct plx_device *xdev, u32 bit, u32 offset)
{
	u32 data;
	mutex_lock(&xdev->mmio_lock);
	data = plx_mmio_read(&xdev->mmio, offset);
	data &= ~bit;
	plx_mmio_write(&xdev->mmio, data, offset);
	mutex_unlock(&xdev->mmio_lock);
}

/**
 * plx_init_vca_g2_gpios - Initialize GPIO outputs for gen2 device
 * @xdev: pointer to plx_device instance
 *
 * VCA GEN2 device doesn't have GPIO default states
 * programmed in EEPROM, thus driver needs to
 * program them at start (only once, for all nodes)
 */
void plx_init_vca_g2_gpios(struct plx_device *xdev)
{
	union Gpio data;
	mutex_lock(&xdev->mmio_lock);
	data.all= plx_mmio_read( &xdev->mmio, PLX_GPIO_OUTPUT);
	switch( xdev->card_type) {
	case VCA_MV_FAB1:
	// GPIO initialization done only once for all 3 nodes
		if( !data.MV.CardReset) {
			data.all= PLX_MV_DEF_GPIO_VAL;
			plx_mmio_write( &xdev->mmio, data.all, PLX_GPIO_OUTPUT);
			msleep( GPIO_DEF_WAIT_TIME);
		}
		break;
	case VCA_VCAA_FAB1:
		data.all = 0x7ef; // recovery pin is different logic
		plx_mmio_write(&xdev->mmio, data.all, PLX_GPIO_OUTPUT);
		break;
	case VCA_VCGA_FAB1:
		if( data.Vcga.CpuReset0 || data.Vcga.CpuReset1)
			break;
	case VCA_UNKNOWN: case VCA_VV: case VCA_PRODUCTION: // These values should be removed from vca_card_type
	case VCA_VV_FAB1: case VCA_VV_FAB2: case VCA_FPGA_FAB1:
		data.all= 0x7ff;
		plx_mmio_write( &xdev->mmio, data.all, PLX_GPIO_OUTPUT);
		break;
	}
	mutex_unlock(&xdev->mmio_lock);
}

/**
 * plx_card_reset - Reset the VCA device.
 * @xdev: pointer to plx_device instance
 * @compl: for signal_bit to complete on driver unload
 * @cpu_id: id of cpu to reset
 */
void plx_card_reset(struct plx_device *xdev, struct completion *compl, int cpu_id)
{
	u32 bit;
	u64 time;
	struct plx_device *node;

	if (cpu_id < 0 || cpu_id >= plx_get_cpu_num(xdev)) {
		dev_err(&xdev->pdev->dev, "Unknown CPU ID: card %d cpu %d\n",
				xdev->card_id, cpu_id);
		return;
	}

	node = plx_contexts[xdev->card_id][cpu_id];
	if (!node) {
		dev_warn(&xdev->pdev->dev, "No device context for card %d cpu %d\n", xdev->card_id, cpu_id);
		return;
	}

	mutex_lock(&node->reset_lock);
	time = get_jiffies_64();
	if(time_after_eq64(time, node->reset_ts) && time_before64(time, node->reset_ts + msecs_to_jiffies(RESET_GRACE_PERIOD_MS))) {
		dev_warn(&xdev->pdev->dev, "Reset trigger ignored: card %d cpu %d; only %d msec elaspsed since last reset pulse\n"
			 , xdev->card_id, cpu_id,
			 jiffies_to_msecs((s64) time - (s64) node->reset_ts)
			 );
		mutex_unlock(&node->reset_lock);
		return;
	}
	node->reset_ts = get_jiffies_64();
	mutex_unlock(&node->reset_lock);

	if (xdev->card_type & VCA_VV ) {
		bit = plx_reset_bits[0][cpu_id];
	} else if (xdev->card_type & VCA_MV ) {
		bit = plx_reset_bits[1][cpu_id];
	} else if (xdev->card_type & VCA_FPGA) {
		bit = (1<<1) | (1<<2) ; //reset CPU and FPGA
	} else if (xdev->card_type & VCA_VCAA) {
		bit = (1 << 1);
	} else if (xdev->card_type & VCA_VCGA && cpu_id < sizeof(plx_reset_bits_vcga)/sizeof(plx_reset_bits_vcga[0]) ) {
		bit = plx_reset_bits_vcga[cpu_id];
	} else {
		dev_err(&xdev->pdev->dev, "Reset unsupported: card %d cpu %d\n",
				xdev->card_id, cpu_id);
		return;
	}

	dev_dbg(&xdev->pdev->dev, "Reset start: card %d cpu %d\n", xdev->card_id,
			cpu_id);
	plx_lbp_reset_start(xdev->card_id, cpu_id);
	signal_bit(xdev, compl, bit, PLX_GPIO_OUTPUT, RESET_PULSE_TIME, NULL, NULL);

	mutex_lock(&node->reset_lock);
	node->reset_ts = get_jiffies_64();
	mutex_unlock(&node->reset_lock);

	msleep(100);
	plx_lbp_reset_stop(xdev->card_id, cpu_id);
	dev_dbg(&xdev->pdev->dev, "Reset finish: card %d cpu %d\n", xdev->card_id,
			cpu_id);
}


static int card_check_power_button_state(struct plx_device *xdev, int cpu_id) {
	union Gpio gpio={ plx_mmio_read( &xdev->mmio, PLX_GPIO_OUTPUT)};
	switch( xdev->card_type) {
	case VCA_VCGA_FAB1:
		switch( cpu_id) {
		case 0: return gpio.Vcga.PowerButton0;
		case 1: return gpio.Vcga.PowerButton1;
		default: return -EINVAL;
		}
	case VCA_VCAA_FAB1: return gpio.Vcaa.PowerButton;
	case VCA_FPGA_FAB1: return gpio.E3S10.CpuPowerButton;
	case VCA_MV_FAB1:
		switch( cpu_id) {
		case 0: return gpio.MV.PowerButton0;
		case 1: return gpio.MV.PowerButton1;
		case 2: return gpio.MV.PowerButton2;
		default: return -EINVAL;
		}
	case VCA_VV_FAB1: case VCA_VV_FAB2: return 1; // silent error for back compatibility
	default: return -ENODATA;
	}
}

/**
 * plx_card_check_power_button_state - Read power button on VCA device.
 * @xdev: pointer to plx_device instance
 * @cpu_id: id of cpu to read status
 * RETURNS: 0 if power button is released, positive number if power button
 *     is pressed down, or time during padding between minimal time to change
 *     button state, negative number if error.
 */
int plx_card_check_power_button_state(struct plx_device *xdev, int cpu_id)
{
	int ret= card_check_power_button_state( xdev, cpu_id);
	switch( ret) {
	case 0: {
		u64 time= get_jiffies_64();
		if( time_after_eq64( time, xdev->power_ts[cpu_id]) &&
			time_before64( time, xdev->power_ts[cpu_id]
				+ msecs_to_jiffies(POWER_GRACE_PERIOD_MS)))
			return 2;
		}
		return 1;
	case 1: return 0;
	default: return ret;
	}
}

/*
 * plx_card_press_power_button - Manages power button on VCA device.
 * @xdev: pointer to plx_device instance
 * @compl: for signal_bit to complete on driver unload
 * @cpu_id: id of cpu to reset
 * @hold: true to set power button hold, false to set power button toggle
 * @wait_start: completion to trigger moment when button state start change
 */
int plx_card_press_power_button(struct plx_device *xdev,
		struct completion *compl, int cpu_id, bool hold,
		struct completion *wait_start)
{
	union Gpio bit={ 0};
	switch( xdev->card_type) {
	case VCA_VCGA_FAB1:
		switch( cpu_id) {
		case 0: bit.Vcga.PowerButton0= 1;break;
		case 1: bit.Vcga.PowerButton1= 1;break;
		default: return -EINVAL;
		} break;
	case VCA_VCAA_FAB1: bit.Vcaa.PowerButton= 1; break;
	case VCA_FPGA_FAB1: bit.E3S10.CpuPowerButton= 1; break;
	case VCA_MV_FAB1:
		switch( cpu_id) {
		case 0: bit.MV.PowerButton0= 1; break;
		case 1: bit.MV.PowerButton1= 1; break;
		case 2: bit.MV.PowerButton2= 1; break;
		default: return -EINVAL;
		} break;
	default: return -ENOKEY;
	}
	dev_dbg( &xdev->pdev->dev, "Power OFF %s begin: card %d cpu %d\n",
		hold? "hold": "toggle", xdev->card_id, cpu_id);
	signal_bit( xdev, compl, bit.all, PLX_GPIO_OUTPUT,
		hold?  POWER_OFF_HOLD_TIME: POWER_OFF_PULSE_TIME,
		wait_start, &xdev->power_ts[ cpu_id]);
	dev_dbg( &xdev->pdev->dev, "Power OFF %s end: card %d cpu %d\n",
		hold? "hold": "toggle", xdev->card_id, cpu_id);
	return 0;
}

/*
 * plx_id_led_switch - turn on/off LED on card
 * @xdev - plx device instance
 * @turn_on - bool indicator if to turn led on or off
 */
void plx_id_led_switch(struct plx_device *xdev, bool turn_on)
{
	if(turn_on){
		dev_dbg(&xdev->pdev->dev, "LED turning on: card %d\n", xdev->card_id);
		_clear_bit(xdev, PLX_CARD_LED_BIT, PLX_GPIO_OUTPUT);
	}
	else{
		dev_dbg(&xdev->pdev->dev, "LED turning off: card %d\n", xdev->card_id);
		_set_bit(xdev, PLX_CARD_LED_BIT, PLX_GPIO_OUTPUT);
	}
}

/*
 * plx_turn_rcv_mode - turn cpu bios recovery mode on/off
 * @xdev - plx device instance
 * @cpu_id - cpu id
 */
void plx_turn_rcv_mode(struct plx_device *xdev, u32 cpu_id, bool turn_on)
{
	if ((xdev->card_type & VCA_MV) || (xdev->card_type & VCA_FPGA)) {
		if (turn_on)
			_clear_bit(xdev, plx_bios_rcv_bits[cpu_id], PLX_GPIO_OUTPUT);
		else
			_set_bit(xdev, plx_bios_rcv_bits[cpu_id], PLX_GPIO_OUTPUT);
	} else if (xdev->card_type & VCA_VCAA) {
		if (turn_on)
			_set_bit(xdev, plx_bios_rcv_bits[cpu_id], PLX_GPIO_OUTPUT);
		else
			 _clear_bit(xdev, plx_bios_rcv_bits[cpu_id], PLX_GPIO_OUTPUT);
	} else if (xdev->card_type & VCA_VCGA) {
		if (turn_on)
			_clear_bit(xdev, plx_bios_rcv_bits_vcga[cpu_id], PLX_GPIO_OUTPUT);
		else
			_set_bit(xdev, plx_bios_rcv_bits_vcga[cpu_id], PLX_GPIO_OUTPUT);
	} else {
		dev_err(&xdev->pdev->dev, "Operation unsupported on card %d\n", xdev->card_id);
	}
}

/*
 * plx_enable_bios_recovery - prepares cpu to boot gold bios
 * @xdev - plx device instance
 * @cpu_id - cpu id
 */
void plx_enable_bios_recovery_mode(struct plx_device *xdev, u32 cpu_id)
{
	if ((xdev->card_type & VCA_MV) || (xdev->card_type & VCA_FPGA)
		|| (xdev->card_type & VCA_VCAA) || (xdev->card_type & VCA_VCGA)) {
		plx_turn_rcv_mode(xdev, cpu_id,true);
	}
	 else if (!(xdev->card_type & VCA_VV)) {
		dev_err(&xdev->pdev->dev, "Gold BIOS update unsupported: card %d\n", xdev->card_id);
	}
}

/*
 * plx_disable_bios_recovery_mode- sets cpu to boot user bios
 * @xdev - plx device instance
 * @cpu_id - cpu id
 */
void plx_disable_bios_recovery_mode(struct plx_device *xdev, u32 cpu_id)
{
	if ((xdev->card_type & VCA_MV) || (xdev->card_type & VCA_FPGA)
		|| (xdev->card_type & VCA_VCAA) || (xdev->card_type & VCA_VCGA)) {
		plx_turn_rcv_mode(xdev, cpu_id, false);
	}
	else if (!(xdev->card_type & VCA_VV)) {
		dev_err(&xdev->pdev->dev, "Gold BIOS update unsupported: card %d\n", xdev->card_id);
	}
}

/**
 * plx_identify_cpu_id - get the id of cpu on VCA card.
 * @xdev: pointer to plx_device instance
 * RETURNS: -1 on error, cpu id otherwise
 */
int plx_identify_cpu_id(struct pci_dev *pdev)
{
	if (pdev->vendor == PCI_VENDOR_ID_INTEL
#ifdef RDK_SUPPORT
		|| pdev->vendor == PLX_PCI_VENDOR_ID_PLX
#endif
	) {
		switch (pdev->device) {
		case INTEL_VCA_PCI_NODE0_ID:
#ifdef RDK_SUPPORT
		case PLX_PCI_DEVICE_87B0:
#endif
			return 0;
		case INTEL_VCA_PCI_NODE1_ID:
#ifdef RDK_SUPPORT
		case PLX_PCI_DEVICE_87B1:
#endif
			return 1;
		case INTEL_VCA_PCI_NODE2_ID:
			return 2;
		};
	}

	return -1;
}

void plx_set_SMB_id(struct plx_device *xdev, u8 id)
{
	if (xdev->card_type & VCA_VV) {
		u32 data;
		u32 smb_id_bits = ID_TO_PLX_SEL_BITS(id);

		dev_dbg(&xdev->pdev->dev, "Setting SMB id: %x on Card: %d START\n", id, xdev->card_id);

		mutex_lock(&xdev->mmio_lock);
		data = plx_mmio_read(&xdev->mmio, PLX_GPIO_OUTPUT);
		data &= ~(PLX_SEL0_BIT | PLX_SEL1_BIT | PLX_SEL2_BIT);
		data += smb_id_bits;
		plx_mmio_write(&xdev->mmio, data, PLX_GPIO_OUTPUT);
		mutex_unlock(&xdev->mmio_lock);

		dev_dbg(&xdev->pdev->dev, "Setting SMB id: %x on Card: %d END\n", id, xdev->card_id);
	} else if ((xdev->card_type & VCA_MV) || (xdev->card_type & VCA_FPGA)
		|| (xdev->card_type & VCA_VCAA) || (xdev->card_type & VCA_VCGA)) {
		dev_err(&xdev->pdev->dev, "Setting SMB NOT IMPLEMENTED FOR MV id: %x on Card: %d END\n", id, xdev->card_id);
	} else {
		dev_err(&xdev->pdev->dev, "Setting SMB unsupported for MV id: %x on Card: %d END\n", id, xdev->card_id);
	}
}

static enum plx_eep_retval eeprom_wait_for_cmd_complete(struct plx_device *xdev)
{
	struct plx_eep_status_register status;
	unsigned tries_left = PLX_EEP_WAIT_TRIES;

	do {
		status.value = plx_mmio_read(&xdev->mmio, PLX_EEP_STATUS_CONTROL);
		if (status.eep_cmd_status == 0)
			return PLX_EEP_STATUS_OK;

		usleep_range(PLX_EEP_WAIT_US, 2 * PLX_EEP_WAIT_US);
		tries_left--;
	} while (tries_left);

	return PLX_EEP_TIMEOUT;
}

static enum plx_eep_retval eeprom_send_cmd(struct plx_device *xdev, u32 cmd, u16 offset)
{
	enum plx_eep_retval ret;

	plx_mmio_write(&xdev->mmio, cmd, offset);

	ret = eeprom_wait_for_cmd_complete(xdev);
	if (ret != PLX_EEP_STATUS_OK) {
		dev_err(&xdev->pdev->dev,
			"%s: error when waiting for cmd to complete!\n",
			__func__);
		return ret;
	}
	return PLX_EEP_STATUS_OK;
}

static enum plx_eep_retval eeprom_wait_for_access_ready(struct plx_device *xdev)
{
	enum plx_eep_retval ret;
	struct plx_eep_status_register status;
	unsigned tries_left = PLX_EEP_WAIT_TRIES;

	status.value = plx_mmio_read(&xdev->mmio, PLX_EEP_STATUS_CONTROL);

	status.eep_cmd = PLX_EEP_CMD_READ_STATUS;
	status.eep_ready = 0;
	status.eep_write_status = 0;

	do {
		ret = eeprom_send_cmd(xdev, status.value, PLX_EEP_STATUS_CONTROL);
		if (ret != PLX_EEP_STATUS_OK) {
			dev_err(&xdev->pdev->dev,
				"%s: failed sending eeprom register cmd!\n",
				__func__);
			return ret;
		}

		status.value = plx_mmio_read(&xdev->mmio, PLX_EEP_STATUS_CONTROL);

		// check EEPROM read & write status
		if (status.eep_ready == 0 && status.eep_write_status == 0)
			return PLX_EEP_STATUS_OK;

		usleep_range(PLX_EEP_WAIT_US, 2 * PLX_EEP_WAIT_US);
		tries_left--;
	} while (tries_left);

	return PLX_EEP_TIMEOUT;
}

enum plx_eep_retval eeprom_read32(struct plx_device *xdev, u32 offset, u32 *value_32)
{
	enum plx_eep_retval ret;
	struct plx_eep_status_register status;
	*value_32 = 0;

	ret = eeprom_wait_for_access_ready(xdev);
	if (ret != PLX_EEP_STATUS_OK) {
		dev_err(&xdev->pdev->dev,
			"%s: error when waiting for eeprom access ready...\n",
			__func__);
		return ret;
	}

	status.value = plx_mmio_read(&xdev->mmio, PLX_EEP_STATUS_CONTROL);

	offset = (offset / sizeof(u32));

	status.eep_blk_addr = offset;
	status.eep_cmd = PLX_EEP_CMD_READ;
	status.eep_blk_addr_upper_bit = 0;

	ret = eeprom_send_cmd(xdev, status.value, PLX_EEP_STATUS_CONTROL);
	if (ret != PLX_EEP_STATUS_OK) {
		dev_err(&xdev->pdev->dev,
			"%s: failed sending eeprom register cmd!\n",
			__func__);
		return ret;
	}

	*value_32 = plx_mmio_read(&xdev->mmio, PLX_EEP_BUFFER);

	return PLX_EEP_STATUS_OK;
}

static enum plx_eep_retval eeprom_read16(struct plx_device *xdev, u32 offset, u16 *value_16)
{
	enum plx_eep_retval ret;
	u32 value_32;

	*value_16 = 0;

	ret = eeprom_read32(xdev, offset & ~0x3, &value_32);
	if (ret != PLX_EEP_STATUS_OK) {
		dev_err(&xdev->pdev->dev,
			"%s: failed to read value_32 on offset %08x...\n",
			__func__, offset);
		return ret;
	}

	if (offset & 0x3)
		*value_16 = (u16)(value_32 >> 16);
	else
		*value_16 = (u16)value_32;

	return PLX_EEP_STATUS_OK;
}

static enum plx_eep_retval eeprom_write32(struct plx_device *xdev, u16 offset, u32 value_32)
{
	enum plx_eep_retval ret;
	struct plx_eep_status_register status;

	ret = eeprom_wait_for_access_ready(xdev);
	if (ret != PLX_EEP_STATUS_OK) {
		dev_err(&xdev->pdev->dev,
			"%s: error when waiting for eeprom access ready...\n",
			__func__);
		return ret;
	}

	status.value = plx_mmio_read(&xdev->mmio, PLX_EEP_STATUS_CONTROL);

	offset = (offset / sizeof(u32));

	status.eep_blk_addr = 0;
	status.eep_cmd = PLX_EEP_CMD_WRITE_ENABLE;
	status.eep_blk_addr_upper_bit = 0;

	ret = eeprom_send_cmd(xdev, status.value, PLX_EEP_STATUS_CONTROL);
	if (ret != PLX_EEP_STATUS_OK) {
		dev_err(&xdev->pdev->dev,
			"%s: failed sending eeprom register cmd!\n",
			__func__);
		return ret;
	}

	plx_mmio_write(&xdev->mmio, value_32, PLX_EEP_BUFFER);

	status.eep_blk_addr = offset;
	status.eep_cmd = PLX_EEP_CMD_WRITE;
	status.eep_blk_addr_upper_bit = 0;

	ret = eeprom_send_cmd(xdev, status.value, PLX_EEP_STATUS_CONTROL);
	if (ret != PLX_EEP_STATUS_OK) {
		dev_err(&xdev->pdev->dev,
			"%s: failed sending eeprom register cmd!\n",
			__func__);
		return ret;
	}

	return PLX_EEP_STATUS_OK;
}

static enum plx_eep_retval eeprom_write16(struct plx_device *xdev, u16 offset, u16 value_16)
{
	enum plx_eep_retval ret;
	u32 value_32;

	ret = eeprom_read32(xdev, offset & ~0x3, &value_32);
	if (ret != PLX_EEP_STATUS_OK) {
		dev_err(&xdev->pdev->dev,
			"%s: failed to read value32 on offset %08x...\n",
			__func__, offset);
		return ret;
	}

	if (offset & 0x3)
		value_32 = ((u32)value_16 << 16) | (value_32 & 0xFFFF);
	else
		value_32 = ((u32)value_16) | (value_32 & 0xFFFF0000);

	ret = eeprom_write32(xdev, offset & ~0x3, value_32);
	if (ret != PLX_EEP_STATUS_OK) {
		dev_err(&xdev->pdev->dev,
			"%s: failed to write value_32 on offset %08x...\n",
			__func__, offset);
		return ret;
	}

	return PLX_EEP_STATUS_OK;
}

static enum plx_eep_retval eeprom_check_crc(struct plx_device *xdev, char *eeprom_data,
				      size_t eeprom_size)
{
	enum plx_eep_retval ret;
	u16 i;
	u32 next_crc_value;
	u32 crc_value;
	u32 xor_value;
	u32 crc_offset;
	u32 crc_calculated = ~0U;
	u32 crc_end_offset = eeprom_size - PLX_EEP_CRC_LENGTH_BYTE;

	for (crc_offset = PLX_EEP_START_CRC_OFFSET; crc_offset < crc_end_offset; crc_offset += sizeof(u32))
	{
		if (crc_end_offset - crc_offset == 2)
			next_crc_value = *(u16*)(eeprom_data + crc_offset);
		else
			next_crc_value = *(u32*)(eeprom_data + crc_offset);

		for (i = 0; i < 32; ++i)
		{
			xor_value = ((crc_calculated ^ (next_crc_value << i)) & (1 << 31));

			if (xor_value)
				xor_value = PLX_EEP_CONST_CRC_XOR_VALUE;
			else
				xor_value = 0;

			crc_calculated = (crc_calculated << 1) ^ xor_value;
		}
	}

	crc_value = *(u32*)(eeprom_data + crc_end_offset);

	if (crc_calculated != crc_value)
		ret = PLX_EEP_INTERNAL_ERROR;
	else
		ret = PLX_EEP_STATUS_OK;

	return ret;
}

static enum plx_eep_retval eeprom_validate(struct plx_device *xdev, char *eeprom_data,
				    size_t eeprom_size)
{
	struct eeprom_header {
		unsigned char validation_signature;
		unsigned char flags;
		unsigned short configuration_size;
	};
	struct eeprom_header *header = (struct eeprom_header *)eeprom_data;
	enum plx_eep_retval ret = PLX_EEP_STATUS_OK;

	if (eeprom_size <= PLX_EEP_HEADER_LENGTH_BYTE + PLX_EEP_CRC_LENGTH_BYTE) {
		dev_err(&xdev->pdev->dev, "Eeprom file is too short!\n");
		return PLX_EEP_INTERNAL_ERROR;
	}

	dev_dbg(&xdev->pdev->dev, "Eeprom config header: signature:%02x flags:%02x size:%04x\n",
		(u32)header->validation_signature,
		(u32)header->flags,
		(u32)header->configuration_size);

	if (header->configuration_size % PLX_EEP_CONFIGURATION_ALIGNMENT_BYTE) {
		dev_err(&xdev->pdev->dev, "Invalid eeprom configuration size!\n");
		return PLX_EEP_INTERNAL_ERROR;
	}

	if ((eeprom_size - PLX_EEP_HEADER_LENGTH_BYTE - PLX_EEP_CRC_LENGTH_BYTE) != header->configuration_size) {
		dev_err(&xdev->pdev->dev, "Size mismatch!\n");
		return PLX_EEP_INTERNAL_ERROR;
	}

	if (header->validation_signature != PLX_EEP_VALIDATION_HEADER) {
		dev_err(&xdev->pdev->dev, "Invalid validation signature!\n");
		return PLX_EEP_INTERNAL_ERROR;
	}

	ret = eeprom_check_crc(xdev, eeprom_data, eeprom_size);
	if (ret != PLX_EEP_STATUS_OK) {
		dev_err(&xdev->pdev->dev, "Calculate CRC is different that provided in eeprom file!\n");
		return ret;
	}

	return ret;
}

/*
* plx_update_eeprom - write user provided configuration to EEPROM
*
* @xdev - pointer to plx device
* @eeprom_data  - eeprom content in following layout:
*	4B - header:
*		1B - validation_signature
*		1B - flags
*		2B - configuration_size
*	(2B, 4B) x configuration_size - (register address, register value) pairs
*	4B - CRC
* @eeprom_size - configuration_size passed by user
*
* RETURNS: PLX_EEP_STATUS_OK on success,
*	   PLX_EEP_INTERNAL_ERROR or PLX_EEP_TIMEOUT on failure
*/
enum plx_eep_retval plx_update_eeprom(struct plx_device *xdev, char *eeprom_data,
				      size_t eeprom_size)
{
	u16 offset;
	u16 verify_value_16;
	u32 verify_value_32;
	u32 value_32;
	enum plx_eep_retval ret;
	u32 eep_3rd_addr_byte_reg;

	ret = eeprom_validate(xdev, eeprom_data, eeprom_size);
	if (ret != PLX_EEP_STATUS_OK)
		return ret;

	mutex_lock(&xdev->mmio_lock);

	eep_3rd_addr_byte_reg = plx_mmio_read(&xdev->mmio, PLX_EEP_ADDRESS_BYTE);

	if (eep_3rd_addr_byte_reg & 0x3)
		plx_mmio_write(&xdev->mmio,
			eep_3rd_addr_byte_reg & (PLX_EEP_3RD_ADDRES_BYTE_RSVD_MASK | PLX_EEP_EXPANSION_ROM_BASE_ADDR_MASK),
			PLX_EEP_ADDRESS_BYTE);

	for (offset = 0; offset < eeprom_size; offset += sizeof(u32))
	{
		// if eeprom data is not dword aligned
		if (eeprom_size - offset == 2) {
			value_32 = *(u16*)(eeprom_data + offset);

			ret = eeprom_write16(xdev, offset, value_32);
			if (ret != PLX_EEP_STATUS_OK) {
				dev_err(&xdev->pdev->dev,
					"%s: failed to write value_32 %08x on offset %08x!\n",
					__func__, value_32, offset);
				goto unlock;
			}

			ret = eeprom_read16(xdev, offset, &verify_value_16);
			if (ret != PLX_EEP_STATUS_OK) {
				dev_err(&xdev->pdev->dev,
					"%s: failed to read value_16 from offset %08x !\n",
					__func__, offset);
				goto unlock;
			}

			if ((u16)value_32 != verify_value_16) {
				dev_err(&xdev->pdev->dev, "value_16 into eeprom (%08x) is "
					"different with value_16 in file (%08x) !\n",
					verify_value_16, value_32);
				ret = PLX_EEP_INTERNAL_ERROR;
				goto unlock;
			}
			break;
		}

		value_32 = *(u32*)(eeprom_data + offset);

		ret = eeprom_write32(xdev, offset, value_32);
		if (ret != PLX_EEP_STATUS_OK) {
			dev_err(&xdev->pdev->dev,
				"%s: failed to write value_32 %08x on offset %08x!\n",
				__func__, value_32, offset);
			goto unlock;
		}

		ret = eeprom_read32(xdev, offset, &verify_value_32);
		if (ret != PLX_EEP_STATUS_OK) {
			dev_err(&xdev->pdev->dev,
				"%s: failed to read value_32 %08x on offset %08x!\n",
				__func__, verify_value_32, offset);
			goto unlock;
		}

		if (value_32 != verify_value_32) {
			dev_err(&xdev->pdev->dev, "value_32 into eeprom (%08x) is "
				"different with value_32 in file (%08x) !\n", verify_value_32, value_32);
			ret = PLX_EEP_INTERNAL_ERROR;
			goto unlock;
		}
	}
unlock:
	mutex_unlock(&xdev->mmio_lock);
	return ret;
}

void plx_clear_dma_mapped_mem(struct plx_device * xdev, __u64 * dma_addr, __u32 * size, u64 * virt_addr)
{
	int pages_num = DIV_ROUND_UP(*size, PAGE_SIZE);

	if (pages_num) {
		dma_unmap_single(&xdev->pdev->dev, *dma_addr,
			*size, DMA_BIDIRECTIONAL);
		free_pages(*virt_addr, get_order(pages_num));
	}
}

ssize_t plx_set_config_file(struct plx_device *xdev, const char *buf,
	size_t count, __u64 * dma_addr, __u32 * size, u64 * virt_addr)
{
	char *config_file;
	void * addr;
	ssize_t old_pages_num;
	ssize_t pages_num = DIV_ROUND_UP(count + 1, PAGE_SIZE);

	dev_dbg(&xdev->pdev->dev, "writing: %s :%d", buf, (int)count);

	/* if already exist, then delete */
	if (*size) {
		old_pages_num = DIV_ROUND_UP(*size, PAGE_SIZE);

		dma_unmap_single(&xdev->pdev->dev, *dma_addr, *size,
			DMA_BIDIRECTIONAL);
		free_pages(*virt_addr, get_order(old_pages_num));

		*dma_addr = 0;
		*virt_addr = 0;
		*size = 0;
	}

	addr = (void *)__get_dma_pages(GFP_KERNEL | __GFP_ZERO,
				       get_order(pages_num));

	if (!addr) {
		count = -ENOMEM;
		goto finish;
	}

	config_file = (char*)addr;

	memcpy_toio(config_file, buf, count);

	if (config_file[count - 1] == '\n')
		config_file[count - 1] = '\0';
	else
		config_file[count] = '\0';

	*virt_addr = (unsigned long long)addr;
	*dma_addr = dma_map_single(&xdev->pdev->dev, addr, count + 1,
		DMA_BIDIRECTIONAL);
	*size = count + 1;

	CHECK_DMA_ZONE(&xdev->pdev->dev, *dma_addr);

	if (dma_mapping_error(&xdev->pdev->dev, *dma_addr)) {
		dev_err(&xdev->pdev->dev,
			"dma_map_single failed in %s!\n",
			__func__);
	}

	smp_wmb();
finish:
	return count;
}

/**
 * plx_read_dma_mapped_mem - Mapped remote memory and copy string.
 * @xdev: pointer to plx_device instance
 * @dma_addr: remote address to copy
 * @dma_size: remote address size to copy
 * @out_buf: destination buffer to copy
 * @out_buf_size: destination buffer size
 *
 * RETURNS: size of copy string without end zero char.
 *
 */
ssize_t plx_read_dma_mapped_mem(struct plx_device *xdev,
	__u64 * dma_addr, __u32 * dma_size, char *out_buf, ssize_t out_buf_size)
{
	ssize_t dma_size_read = readq(dma_size);
	ssize_t size = min(dma_size_read, out_buf_size - 1);
	ssize_t count = 0;
	char *mem;


	if (out_buf_size == 0) {
		dev_err(&xdev->pdev->dev,
			"%s empty output buffer!\n",
			__func__);
		return 0;
	}

	mem = plx_ioremap(xdev, *dma_addr, size);
	if (!mem)
		return 0;

	memcpy_fromio(out_buf, mem, size);
	out_buf[size] = 0;
	count = strnlen(out_buf, size);

	plx_iounmap(xdev, mem);

	return count;
}

/**
 * plx_set_dp_addr - Write boot params address to spad
 * @xdev: pointer to plx_device instance
 * @dp_addr: remote address to copy
 */
void plx_set_dp_addr(struct plx_device *xdev, u64 dp_addr)
{
	dev_dbg(&xdev->pdev->dev, "%s Write dp addr 0x%llx \n", __func__, dp_addr);
	plx_write_spad(xdev, PLX_DPLO_SPAD, dp_addr);
	plx_write_spad(xdev, PLX_DPHI_SPAD, dp_addr >> 32);
}

/**
 * plx_set_dp_addr - Read remote address to boot params from spad
 * @xdev: pointer to plx_device instance
 *
 * RETURNS: Remote address to boot params rdp.
 *
 */
u64 plx_get_dp_addr(struct plx_device *xdev)
{
	u64 lo, hi, dp_addr;
	lo = plx_read_spad(xdev, PLX_DPLO_SPAD);
	hi = plx_read_spad(xdev, PLX_DPHI_SPAD);
	dp_addr = lo | (hi << 32);
	dev_err(&xdev->pdev->dev, "%s Read dp_addr 0x%llx \n", __func__, dp_addr);
	return dp_addr;
}

u32 plx_read_straps(struct plx_device *xdev)
{
	return plx_mmio_read(&xdev->mmio, MV_STRAPS_GPIO) & MV_STRAPS_BIT_MASK;
}


int plx_read_caterr(struct plx_device *xdev) {
	union Gpio gpio={ plx_mmio_read(&xdev->mmio, PLX_GPIO_INPUT) };
	switch( xdev->card_type){
	case VCA_FPGA_FAB1:
		return gpio.E3S10.CatErr;
	case VCA_VCAA_FAB1:
		return gpio.Vcaa.CatErr;
	case VCA_VCGA_FAB1:
		switch( xdev->port_id) { // equivalent of cpu_id
		case 0:return gpio.Vcga.CatErr0;
		case 1:return gpio.Vcga.CatErr1;
		}
	default: ;}
	return -ENODATA;
}


int plx_read_power_ok(struct plx_device *xdev) {
	union Gpio gpio={ plx_mmio_read(&xdev->mmio, PLX_GPIO_INPUT) };
	switch( xdev->card_type){
	case VCA_VCAA_FAB1:
		return gpio.Vcaa.PowerOk;
	case VCA_VCGA_FAB1:
		switch( xdev->port_id) { // equivalent of cpu_id
		case 0: return gpio.Vcga.PowerOk0;
		case 1: return gpio.Vcga.PowerOk1;
		}
	default: ;}
	return -ENODATA;
}

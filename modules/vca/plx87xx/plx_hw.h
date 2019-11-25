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
#ifndef _PLX_HW_H_
#define _PLX_HW_H_

#define PLX_PCI_VENDOR_ID_PLX 0x10B5

#define PLX_PCI_DEVICE_87A0 0x87A0
#define PLX_PCI_DEVICE_87A1 0x87A1
#define PLX_PCI_DEVICE_87B0 0x87B0
#define PLX_PCI_DEVICE_87B1 0x87B1

#define INTEL_VCA_PCI_NODE0_ID	0x2954
#define INTEL_VCA_PCI_NODE1_ID	0x2955
#define INTEL_VCA_PCI_NODE2_ID	0x2956
#define INTEL_VCA_PCI_NODE3_ID	0x2957

#define INTEL_VCA_PCI_BRIDGE_ID 	0x2950
#define INTEL_VCA_CARD_UPSTREAM_ID	0x2953
#define INTEL_VCA_CPU0_UPSTREAM_ID	0x2958
#define INTEL_VCA_CPU1_UPSTREAM_ID	0x2959
#define INTEL_VCA_CPU2_UPSTREAM_ID	0x295A

#define PLX_VS0UPSTREAM                    0x360
#define PLX_ALUT_SEGMENTS_NUM     256
#define PLX_ALUT_NT0_PORT_ENABLE (1<<30)
#define PLX_ALUT_NT1_PORT_ENABLE (1<<31)

#define PLX_MMIO_BAR 0
#define PLX_APER_BAR 2

#define VCA_ALUT_CARD_SIDE
#define VCA_ALUT_HOST_SIDE

#define PLX_DOORBELL_IDX_START 0
#define PLX_NUM_DOORBELL 16

#ifndef READ_ONCE
#define READ_ONCE(x) ACCESS_ONCE(x)
#endif
#ifndef WRITE_ONCE
#define WRITE_ONCE(x, val) ( ACCESS_ONCE(x) = (val) )
#endif

enum plx_register {
	PLX_LINK_STATUS_AND_CONTROL_REGISTER= 0x78,
	PLX_EEP_STATUS_CONTROL=               0x260,
	PLX_EEP_BUFFER=                       0x264,
	PLX_EEP_ADDRESS_BYTE=                 0x26c,
	PLX_EEP_CRC=                          0x270,
	PLX_GPIO_DIRECTION1=  0x600, // for bit 0-9
	PLX_GPIO_DIRECTION2=  0x604, // for bit 10
	PLX_GPIO_INPUT=       0x61C,
	PLX_GPIO_OUTPUT=      0x624,
	PLX_SPAD0=            0xC6C,
	PLX_PORT_ID=          0xC8C,
	PLX_BAR2_AT=          0xC3C,
	PLX_BAR3_AT=          0xC40,
	PLX_BAR4_AT=          0xC44,
	PLX_BAR5_AT=          0xC48,
	PLX_DBIS=             0xC4C,
	PLX_DBIC=             0xC50,
	PLX_DBIMS=            0xC54,
	PLX_DBIMC=            0xC58,
	PLX_RID_LUT_2_3=      0xDB8,// for DMA
};

#define PLX_DBIS 0xC4C
#define PLX_DBIC 0xC50
#define PLX_DBIMS 0xC54
#define PLX_DBIMC 0xC58

#define PLX_NT0_RID_LUT_VIRTUAL_OFFSET      0x3edb4
#define PLX_NT0_RID_LUT_LINK_OFFSET         0x3fdb4
#define PLX_NT1_RID_LUT_VIRTUAL_OFFSET      0x3cdb4
#define PLX_NT1_RID_LUT_LINK_OFFSET         0x3ddb4
#define PLX_RID_LUT_ENABLE 0x00010001
#define PLX_RID_LUT_ENABLE_1 0x1

#define VCA_DMA_DEVICE      0
#define VCA_DMA_HOST_FUN_1  1
#define VCA_DMA_HOST_FUN_2  2
#define VCA_DMA_CARD_FUN_1  3
#define VCA_DMA_CARD_FUN_2  4

#define PLX_VV_CPU_NUMS 3
#define PLX_MV_CPU_NUMS 3
#define PLX_FPGA_CPU_NUMS 1
#define PLX_VCAA_CPU_NUMS 1
#define PLX_VCGA_CPU_NUMS 2

/* bits for various operations on PLXs*/
#define PLX_VV_CPU0_RESET_BIT (1 << 1)
#define PLX_VV_CPU1_RESET_BIT (1 << 2)
#define PLX_VV_CPU2_RESET_BIT (1 << 3)
#define PLX_MV_CPU0_RESET_BIT (1 << 2)
#define PLX_MV_CPU1_RESET_BIT (1 << 1)
#define PLX_MV_CPU2_RESET_BIT (1 << 3)

#define PLX_VCGA_CPU0_RESET_BIT (1 << 1)
#define PLX_VCGA_CPU1_RESET_BIT (1 << 2)

#define PLX_CARD_LED_BIT (1 << 3)

#define PLX_SEL0_BIT (1 << 4)
#define PLX_SEL1_BIT (1 << 5)
#define PLX_SEL2_BIT (1 << 6)
#define ID_TO_PLX_SEL_BITS(x) (x << 4) & \
	(PLX_SEL0_BIT | PLX_SEL1_BIT | PLX_SEL2_BIT)

#define PLX_CPU0_POWER_BIT (1 << 9)
#define PLX_CPU1_POWER_BIT (1 << 10)
#define PLX_CPU2_POWER_BIT (1 << 8)

#define PLX_BIOS_RCV_MODE_CPU0 (1 << 4)
#define PLX_BIOS_RCV_MODE_CPU1 (1 << 6)
#define PLX_BIOS_RCV_MODE_CPU2 (1 << 5)

#define PLX_VCGA_BIOS_RCV_MODE_CPU0 (1 << 4)
#define PLX_VCGA_BIOS_RCV_MODE_CPU1 (1 << 5)

#define PLX_CARD_RESET_BIT (1 << 7)
#define PLX_M2_RESET_BIT (1 << 0)

#define PLX_MV_DEF_GPIO_VAL ( PLX_MV_CPU0_RESET_BIT | PLX_MV_CPU1_RESET_BIT | \
			      PLX_MV_CPU2_RESET_BIT | PLX_CPU0_POWER_BIT | \
			      PLX_CPU1_POWER_BIT | PLX_CPU2_POWER_BIT | \
			      PLX_BIOS_RCV_MODE_CPU0 | PLX_BIOS_RCV_MODE_CPU1 |\
			      PLX_BIOS_RCV_MODE_CPU2 | PLX_M2_RESET_BIT | \
			      PLX_CARD_RESET_BIT )


#define PLX_LINK_WIDTH_BITMASK 0x1F00000
#define PLX_LINK_WIDTH_OFFSET 20
#define PLX_LINK_GEN_BITMASK 0xF0000
#define PLX_LINK_GEN_OFFSET 16
#define PLX_LINK_GEN3_VALUE 3

#define PLX_MAXDB 16
#define DB_TO_MASK(n) ((u32)(0x1) << (n))
#define PLX_DPLO_SPAD 0
#define PLX_DPHI_SPAD 1
#define PLX_HSDB_SPAD 2
#define PLX_RESERVED_FOR_LPB_0_SPAD 2
#define PLX_RESERVED_FOR_LPB_1_SPAD 3

#define PLX_HSDB_CMD	0xc0ffee
#define PLX_HSDB_MASK	0xffffff00
#define PLX_HSDB_SHIFT	8

#define PLX_A_LUT_CONTROL 0xc94
#define PLX_A_LUT_ENABLE  (0x1UL << 28 | 0x1UL << 31)

static const u16 _plx_intr_init[] = {
	PLX_DOORBELL_IDX_START,
	PLX_NUM_DOORBELL,
};

#define PLX_A_LUT_LOWER_RE_MAP_SUBARRAY_OFFSET  0x0
#define PLX_A_LUT_HIGHER_RE_MAP_SUBARRAY_OFFSET 0x400
#define PLX_A_LUT_PERMISSION_SUBARRAY_OFFSET    0x800
#define PLX_A_LUT_PERMISSION_WRITE_ENABLE	0x1
#define PLX_A_LUT_PERMISSION_READ_ENABLE	0x2
#define PLX_A_LUT_ARRAY_OFFSET			0x1000
#define PLX_A_LUT_MAX_ARRAY			128

struct plx_eep_status_register {
	union {
		struct {
			u32 eep_blk_addr		: 13; //  0-12
			u32 eep_cmd			: 3;  // 13-15
			u32 eep_prsnt			: 2;  // 16-17
			u32 eep_cmd_status		: 1;  // 18
			u32 eep_crc_err			: 1;  // 19
			u32 eep_blk_addr_upper_bit	: 1;  // 20
			u32 eep_addr_width_override	: 1;  // 21
			u32 eep_addr_width		: 2;  // 22-23
			u32 eep_ready			: 1;  // 24
			u32 eep_write_enabled		: 1;  // 25
			u32 eep_write_blk_protect	: 2;  // 26-27
			u32 eep_write_status		: 3;  // 28-30
			u32 eep_write_protect_enabled	: 1;  // 31
		};
		u32 value;
	};
};


// PLX EEPROM related definitions
#define PLX_EEP_CMD_READ		3
#define PLX_EEP_CMD_READ_STATUS		5
#define PLX_EEP_CMD_WRITE_ENABLE	6
#define PLX_EEP_CMD_WRITE		2

#define PLX_EEP_3RD_ADDRES_BYTE_RSVD_MASK	0xFF00
#define PLX_EEP_EXPANSION_ROM_BASE_ADDR_MASK	0xFFFF0000

#define PLX_EEP_START_CRC_OFFSET		2
#define PLX_EEP_HEADER_LENGTH_BYTE		4
#define PLX_EEP_CRC_LENGTH_BYTE			4
#define PLX_EEP_CONFIGURATION_ALIGNMENT_BYTE	6
#define PLX_EEP_WAIT_US				50
#define PLX_EEP_WAIT_TRIES			1000
#define PLX_EEP_VALIDATION_HEADER		0x5A
#define PLX_EEP_CONST_CRC_XOR_VALUE		0xDB710641

// EEPROM present status in PLX_EEP_STATUS_CONTROL register
#define PLX_EEP_EEPPRSNT_SHIFT		16
#define PLX_EEP_EEPPRSNT_MASK		0x30000
#define PLX_EEP_EEPPRSNT_NOT_PRESENT	0
#define PLX_EEP_EEPRRSNT_SIGNATURE_OK	1
#define PLX_EEP_EEPRRSNT_SIGNATURE_FAIL	3

// EEPROM CRC status in PLX_EEP_STATUS_CONTROL register
#define PLX_EEP_EEPCRC_ERR_SHIFT	19
#define PLX_EEP_EEPCRC_ERR_MASK		0x80000

#define	MV_STRAPS_GPIO          0x61c
#define MV_STRAPS_BIT_MASK      0x0000000F

int plx_program_rid_lut_dma(struct plx_device *xdev);
int plx_hw_init(struct plx_device *xdev, struct pci_dev *pdev);
void plx_hw_deinit(struct plx_device *xdev);
void plx_intr_init(struct plx_device *xdev);
void plx_disable(struct plx_device *xdev);
void plx_write_spad(struct plx_device *xdev, unsigned int idx, u32 val);
u32 plx_read_spad(struct plx_device *xdev, unsigned int idx);
void plx_enable_interrupts(struct plx_device *xdev);
void plx_disable_interrupts(struct plx_device *xdev);
void plx_send_intr(struct plx_device *xdev, int doorbell);
u32 plx_ack_interrupt(struct plx_device *xdev);
bool plx_dma_filter(struct dma_chan *chan, void *param);
int plx_add_a_lut_entry(struct plx_device *xdev, dma_addr_t addr_in,
	size_t size, dma_addr_t* addr_out);
void plx_del_a_lut_entry(struct plx_device *xdev, dma_addr_t addr);
void plx_a_lut_clear(struct plx_device* xdev, u32 offset);
void plx_a_lut_peer_enable(struct plx_device *xdev);

void __iomem * plx_ioremap(struct plx_device *xdev, dma_addr_t pa, size_t len);
void plx_iounmap(struct plx_device *xdev, void __iomem *va);

u32 plx_link_width(struct plx_device *xdev);
u32 plx_link_status(struct plx_device *xdev);

void plx_card_reset(struct plx_device *xdev,
 struct completion *compl, int cpu_id);
void plx_init_vca_g2_gpios(struct plx_device *xdev);
int plx_card_check_power_button_state(struct plx_device *xdev, int cpu_id);
int plx_card_press_power_button(struct plx_device *xdev,
	struct completion *compl, int cpu_id, bool on, struct completion *wait_start);
u32 plx_get_cpu_num(struct plx_device *xdev);
u32 plx_get_meminfo(struct plx_device *xdev);
int plx_identify_cpu_id(struct pci_dev *pdev);
void plx_set_SMB_id(struct plx_device *xdev, u8 id);
enum plx_eep_retval plx_update_eeprom(struct plx_device *xdev,
 char *eeprom_data, size_t eeprom_size);
void plx_set_dp_addr(struct plx_device *xdev, u64 dp_addr);
u64 plx_get_dp_addr(struct plx_device *xdev);
u32 plx_get_eeprom_crc(struct plx_device *xdev);
u32 plx_read_straps(struct plx_device *xdev);
void plx_clear_dma_mapped_mem(struct plx_device * xdev,
	__u64 * dma_addr, __u32 * size, u64 * virt_addr);
ssize_t plx_set_config_file(struct plx_device *xdev, const char *buf,
	size_t count, __u64 * dma_addr, __u32 * size, u64 * virt_addr);
ssize_t plx_read_dma_mapped_mem(struct plx_device *xdev,
	__u64 * dma_addr, __u32 * dma_size, char * out_buf, ssize_t out_buf_size);
void plx_enable_bios_recovery_mode(struct plx_device *xdev, u32 cpu_id);
void plx_disable_bios_recovery_mode(struct plx_device *xdev, u32 cpu_id);
void plx_id_led_switch(struct plx_device *xdev, bool turn_on);
bool kvm_check_guest(void);
int plx_read_caterr(struct plx_device *xdev);
int plx_program_rid_lut_for_node(struct plx_device *xdev);
int plx_read_power_ok(struct plx_device *xdev);

/**
 * plx_mmio_read - read from an MMIO register.
 * @mw: MMIO register base virtual address.
 * @offset: register offset.
 *
 * RETURNS: register value.
 */
static inline u32 plx_mmio_read(struct vca_mw *mw, enum plx_register offset)
{
    return ioread32(mw->va + offset);
}

/**
 * plx_mmio_write - write to an MMIO register.
 * @mw: MMIO register base virtual address.
 * @val: the data value to put into the register
 * @offset: register offset.
 *
 * RETURNS: none.
 */
static inline void
plx_mmio_write(struct vca_mw *mw, u32 val, enum plx_register offset)
{
    iowrite32(val, mw->va + offset);
}
#endif

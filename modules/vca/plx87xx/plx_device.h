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
#ifndef _PLX_DEVICE_H_
#define _PLX_DEVICE_H_

#include <linux/cdev.h>
#include <linux/idr.h>
#include <linux/notifier.h>
#include <linux/version.h>
#include <linux/dmaengine.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#ifdef VCA_IN_KERNEL_BUILD
#include <linux/vop_bus.h>
#include <linux/vca_csm_bus.h>
#else
#include "../bus/vop_bus.h"
#include "../bus/vca_csm_bus.h"
#include "../bus/vca_mgr_bus.h"
#include "../bus/vca_mgr_extd_bus.h"
#include "../bus/vca_csa_bus.h"
#endif
#include "plx_intr.h"
#include "plx_alm.h"
#include "plx_lbp.h"
#include "../common/vca_dev_common.h"
/**
 * struct plx_device -  VCA device information for each card.
 *
 * @mmio: MMIO bar information.
 * @aper: Aperture bar information.
 * @id: The unique device id for this VCA device.
 * @pdev: The PCIe device
 * @smpt: VCA SMPT information.
 * @intr_info: H/W specific interrupt information.
 * @irq_info: The OS specific irq information
 * @dbg_dir: debugfs directory of this VCA device.
 * @bootaddr: VCA boot address.
 * @dp: virtio device page
 * @rdp: remote virtio device page
 * @dp_dma_addr: virtio device page DMA address.
 * @link_side: PLX Link Side
 * @a_lut: Offset Side use A-LUT
 * @a_lut_peer: Peer use A-LUT
 * @a_lut_array_base: Offset of A-LUT array
 * @reg_base: Register base offset
 * @reg_base_peer: Remote register base offset
 * @intr_reg_base: Interrupt register base offset
 * @peer_intr_reg_base: Remote Interrupt register base offset
 * @dma_ch - DMA channel
 * @vpdev: Virtio over PCIe device on the VOP virtual bus.
 * @scdev: SCIF device on the SCIF virtual bus.
 * @vca_csm_dev: VCA_CSM device on the VCA_CSM bus
 * @vca_mgr_dev: VCA_MGR device on VCA_MGR bus
 * @vca_mgr_extd_dev: VCA_MGR_EXTD device on VCA_MGR_EXTD bus
 * @vca_csa_dev: VCA_CSA device on VCA_CSA bus
 * @spinlock: Spinlock for locking critical sections with I/O to A-LUT
 * @a_lut_manager: For managing entries in A-LUT
 * @card_id: Id of a card that this device is on
 * @card_type: Type of VCA card in this device
 * $mmio_lock: mutex for protecting mmio's
 * @mmio_link_offset: offset to mmio link registers
 * @lbp: LBP data
 * @lbp_lock: for protecting lbp protocol
 * @lbp_resetting: to check if cpu is being reset
 * @blockio.be_dev: blockio backend control device
 * @blockio.fe_dev: blockio frontend device
 * @blockio.dp_va: blockio device page virtual addess
 * @blockio.dp_da: blockio device page dma address
 * @blockio.ftb_db: blockio frontend-to-backend doorbell
 * @mac_addr: node ethernet adapter MAC
 */
enum plx_side {
    host_side, // virtual side
    node_side, // link side
};

struct plx_device {
	struct vca_mw mmio;
	struct vca_mw aper;
	int id;
	struct pci_dev *pdev;
	struct plx_smpt_info *smpt;
	struct plx_intr_info *intr_info;
	struct plx_irq_info irq_info;
	struct dentry *dbg_dir;
	u32 bootaddr;
	struct vca_bootparam *dp;
	struct vca_bootparam __iomem *rdp;
	dma_addr_t dp_dma_addr;
	dma_addr_t dp_dma_addr_a_lut;
	short port_id;
	enum plx_side link_side;
	u32 reg_base;
	u32 reg_base_peer;
	u64 bios_information_date;
	u64 bios_information_version;
	u64 bios_cfg_sgx;
	u64 bios_cfg_gpu_aperture;
	u64 bios_cfg_tdp;
	u64 bios_cfg_hyper_threading;
	u64 bios_cfg_gpu;

	bool a_lut;
	bool a_lut_peer;
	/* mmio offset of A-LUT array for transactions *to* current side.
	 *
	 * Virtual side must add an entry to A-LUT array at MMIO
	 * a_lut_array_base in order to allow link side port to access
	 * memory location.
	 *
	 * Link side on port must add an antry to A-LUT array at MMIO
	 * a_lut_array_base in order to alow virtual side to access memory
	 * location.
	 * */
	u32 a_lut_array_base;

	u32 intr_reg_base;
	u32 peer_intr_reg_base;
	struct dma_chan *dma_ch;
	struct vop_device *vpdev;
	struct vca_csm_device *vca_csm_dev;
	struct vca_mgr_device *vca_mgr_dev;
	struct vca_mgr_extd_device *vca_mgr_extd_dev;
	struct vca_csa_device *vca_csa_dev;
	spinlock_t alm_lock;
	struct plx_alm a_lut_manager;

	bool hs_done;
	struct vca_irq *hs_irq;

	u8 card_id;
	enum vca_card_type card_type;
	struct mutex mmio_lock;
	u32 mmio_link_offset;

	struct plx_lbp lbp;
	struct mutex * lbp_lock;
	bool lbp_resetting;
	u64 power_ts[MAX_VCA_CARD_CPUS];
	u64 reset_ts;
	struct mutex reset_lock;
	bool first_time_boot_mgr;

	struct {
		union {
			struct miscdevice *be_dev;
			struct vcablk_dev *fe_dev;
		};
		void* dp_va;
		dma_addr_t dp_da;
		int ftb_db;
	} blockio;

	u8 mac_addr[6];
};

static inline u32 plx_link_mmio_read(struct plx_device *xdev, u32 offset)
{
	struct vca_mw *mw = &xdev->mmio;
	return ioread32(mw->va + xdev->mmio_link_offset + offset);
}

static inline void
plx_link_mmio_write(struct plx_device *xdev, u32 val, u32 offset)
{
	struct vca_mw *mw = &xdev->mmio;

	iowrite32(val, mw->va + xdev->mmio_link_offset + offset);
}

void plx_bootparam_init(struct plx_device *xdev);
void plx_create_debug_dir(struct plx_device *dev);
void plx_delete_debug_dir(struct plx_device *dev);
void __init plx_init_debugfs(void);
void plx_exit_debugfs(void);
int plx_scif_setup(struct plx_device *xdev);
extern struct dma_map_ops _plx_dma_ops;
extern struct vop_hw_ops vop_hw_ops;
extern struct vca_csm_hw_ops vca_csm_plx_hw_ops;
extern struct vca_mgr_hw_ops vca_mgr_plx_hw_ops;
extern struct vca_mgr_extd_hw_ops vca_mgr_extd_plx_hw_ops;
extern struct vca_csa_hw_ops vca_csa_plx_hw_ops;
extern struct vca_csa_hw_ops vca_csa_plx_ddhw_ops;
extern struct plx_blockio_hw_ops blockio_hw_ops;

/* Grace period after triggering reset GPIO */
#define RESET_GRACE_PERIOD_MS	2000

/* Grace period after triggering power GPIO */
#define POWER_GRACE_PERIOD_MS	1500

#endif

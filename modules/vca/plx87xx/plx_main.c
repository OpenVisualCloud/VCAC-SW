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
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/poll.h>
#include <linux/version.h>
#include <linux/suspend.h>
#include <linux/kthread.h>
#include <linux/delay.h>

#ifdef VCA_IN_KERNEL_BUILD
#include <linux/vca_dev_common.h>
#else
#include "../common/vca_dev_common.h"
#endif
#include "../common/vca_common.h"
#include "plx_device.h"
#include "plx_hw.h"
#include "plx_alm.h"
#include "plx_hw_ops_blockio.h"
#include "../blockio/hal_pci/vcablk_hal_pci.h"
#include "plx_procfs.h"

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37)
static const char plx_driver_name[] = "plx87xx_vca";
#else
static char plx_driver_name[] = "plx87xx_vca";
#endif

#define BLOKIO_DEVPAGE_SIZE (2 * PAGE_SIZE)

#define LINK_WAIT_TIMEOUT 60

static const struct pci_device_id plx_pci_tbl[] = {
	{PCI_DEVICE(PLX_PCI_VENDOR_ID_PLX, PLX_PCI_DEVICE_87A0)},
	{PCI_DEVICE(PLX_PCI_VENDOR_ID_PLX, PLX_PCI_DEVICE_87A1)},
	{PCI_DEVICE(PLX_PCI_VENDOR_ID_PLX, PLX_PCI_DEVICE_87B0)},
	{PCI_DEVICE(PLX_PCI_VENDOR_ID_PLX, PLX_PCI_DEVICE_87B1)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, INTEL_VCA_CARD_UPSTREAM_ID)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, INTEL_VCA_CPU0_UPSTREAM_ID)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, INTEL_VCA_CPU1_UPSTREAM_ID)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, INTEL_VCA_CPU2_UPSTREAM_ID)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, INTEL_VCA_PCI_NODE0_ID)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, INTEL_VCA_PCI_NODE1_ID)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, INTEL_VCA_PCI_NODE2_ID)},
	/* required last entry */
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, plx_pci_tbl);

/**
 * enum subsystem_id - subsystem ID for VCA cards.
 */
enum subsystem_id {
	SUBSYSTEM_ID_VV_FAB1     = 0x1001,
	SUBSYSTEM_ID_VV_FAB2     = 0x1002,
	SUBSYSTEM_ID_MV_FAB1     = 0x1004,
	SUBSYSTEM_ID_VCAA_FAB1	 = 0x1006,
	SUBSYSTEM_ID_FPGA_FAB1   = 0x1008,
	SUBSYSTEM_ID_VCGA_FAB1	 = 0x1009,
};

/* ID allocator for VCA devices */
static struct ida g_plx_ida;

/* Thread required to perform deffered handshake,
 when card is booted before a host */
static struct task_struct *plx_deffered_hs_thread = NULL;

struct plx_device * plx_contexts[MAX_VCA_CARDS][MAX_VCA_CARD_CPUS]
= { { 0 } };
EXPORT_SYMBOL_GPL(plx_contexts);

struct plx_device *g_vca_xdev[MAX_VCA_CARDS][MAX_VCA_CARD_CPUS] = { { NULL, },};

struct plx_device* vca_get_xdev_pointer(int card, int cpu)
{
        return g_vca_xdev[card][cpu];
}
EXPORT_SYMBOL_GPL(vca_get_xdev_pointer);

static void register_plx_context(struct plx_device *context)
{
	int card_id = context->card_id;
	int cpu_id = plx_identify_cpu_id(context->pdev);

	if (card_id >= 0 && card_id < MAX_VCA_CARDS &&
	    cpu_id >= 0 && cpu_id < MAX_VCA_CARD_CPUS)
		plx_contexts[card_id][cpu_id] = context;
}

static void unregister_plx_context(struct plx_device *context)
{
	struct plx_device * _context;
	int card_id = context->card_id;
	int cpu_id = plx_identify_cpu_id(context->pdev);

	if (card_id >= 0 && card_id < MAX_VCA_CARDS &&
	    cpu_id >= 0 && cpu_id < MAX_VCA_CARD_CPUS) {
		_context = plx_contexts[card_id][cpu_id];
		if(_context == context)
			plx_contexts[card_id][cpu_id] = NULL;
	}
}

/* Initialize the device page */
static int plx_dp_init(struct plx_device *xdev)
{
	// dma_alloc_coherent() zeroes out memory on  allocation:
	xdev->dp = dma_alloc_coherent(&xdev->pdev->dev, VCA_DP_SIZE,
                                  &xdev->dp_dma_addr, GFP_KERNEL);
	if (!xdev->dp) {
		dev_err(&xdev->pdev->dev, "%s %d err %d\n",
			__func__, __LINE__, -ENOMEM);
		return -ENOMEM;
	}

	return 0;
}

/* Uninitialize the device page */
static void plx_dp_uninit(struct plx_device *xdev)
{
	dev_info(&xdev->pdev->dev, "%s entered\n", __func__);
	dma_free_coherent(&xdev->pdev->dev, VCA_DP_SIZE, xdev->dp,
	                        xdev->dp_dma_addr);
	plx_set_dp_addr(xdev, 0);

}

/* Notify remote side about initialized DP*/
static void plx_dp_notify(struct plx_device *xdev)
{
	u32 notify;
	dev_info(&xdev->pdev->dev, "%s writing dp_dma_addr:%llx\n",
		__func__, xdev->dp_dma_addr);
	plx_set_dp_addr(xdev, xdev->dp_dma_addr);

	if(!(xdev->card_type & VCA_PRODUCTION)) {
		notify = plx_read_spad(xdev, PLX_HSDB_SPAD);
		if((notify & PLX_HSDB_MASK) >> PLX_HSDB_SHIFT == PLX_HSDB_CMD) {
			dev_info(&xdev->pdev->dev,
				"Send doorbell %d\n", notify & ~PLX_HSDB_MASK);
			plx_send_intr(xdev, notify & ~PLX_HSDB_MASK);
		}
		xdev->hs_done = true;
	}
}

static int plx_rdp_init(struct plx_device *xdev)
{
	struct vca_bootparam __iomem *bootparam;
	u64 dp_dma_addr;
	u32 magic = 0;
	u32 version = 0;
	int err = 0;

	dp_dma_addr = plx_get_dp_addr(xdev);

	dev_info(&xdev->pdev->dev,  "%s reading dp_dma_addr:0x%llx\n",
		__func__, dp_dma_addr);

	if ((dp_dma_addr == 0x0L) || (dp_dma_addr == 0xffffffffffffffffL)) {
		err = -ENODEV;
		goto skip_rdp;
	}

	xdev->rdp = plx_ioremap(xdev, dp_dma_addr, VCA_DP_SIZE);
	if (!xdev->rdp) {
		dev_err(&xdev->pdev->dev,
			"cannot map dp_dma_addr in ALUT\n");
		err = -ENOMEM;
		goto skip_rdp;
	}

	dp_dma_addr = ((void*)xdev->rdp) - xdev->aper.va;

	dev_info(&xdev->pdev->dev,  "dp_dma_addr mapped as 0x%llx offset in BAR\n",
		dp_dma_addr);

	dev_info(&xdev->pdev->dev, "bar size is %llx, rdp address is %llx\n",
		(u64)xdev->aper.len, dp_dma_addr);

	bootparam = xdev->rdp;
	magic = ioread32(&bootparam->magic);

	if (VCA_MAGIC != magic) {
		dev_err(&xdev->pdev->dev,
			"bootparam magic mismatch 0x%x\n", magic);
		err = -EINVAL;
		goto skip_rdp;
	} else {
		dev_info(&xdev->pdev->dev,  "%s magic is OK: %x", __func__, magic);
	}

	version = ioread32(&bootparam->version_host);
	iowrite32(VCA_PROTOCOL_VERSION, &bootparam->version_card);

	if (VCA_PROTOCOL_VERSION != version) {
		dev_err(&xdev->pdev->dev,  "%s Protocol version mismatch. "
				"Host version is 0x%x, but expected 0x%x\n",
				__func__, version, VCA_PROTOCOL_VERSION);
		err = -EINVAL;
	}else {
		dev_err(&xdev->pdev->dev,  "%s Correct protocol version 0x%x\n",
			__func__, VCA_PROTOCOL_VERSION);
	}

skip_rdp:
	if (err) {
		xdev->hs_done = false;
		plx_write_spad(xdev, PLX_LBP_SPAD_i7_READY, PLX_LBP_i7_GENERAL_ERROR);
		if (xdev->rdp) {
			plx_iounmap(xdev, xdev->rdp);
			xdev->rdp = NULL;
			err = -EIO;
		}
	} else {
		xdev->hs_done = true;
		plx_write_spad(xdev, PLX_LBP_SPAD_i7_READY, PLX_LBP_i7_DRV_PROBE_DONE);
	}
	return err;
}

/**
 * plx_device_init - Allocates and initializes the VCA device structure
 *
 * @xdev: pointer to plx_device instance
 * @pdev: The pci device structure
 *
 * returns none.
 */
static void
plx_device_init(struct plx_device *xdev, struct pci_dev *pdev)
{
	int i;
	xdev->pdev = pdev;
	xdev->irq_info.next_avail_src = 0;
	xdev->dma_ch = NULL;
	mutex_init(&xdev->mmio_lock);
	mutex_init(&xdev->reset_lock);
	mutex_lock(&xdev->reset_lock);
	xdev->reset_ts = INITIAL_JIFFIES - msecs_to_jiffies(RESET_GRACE_PERIOD_MS);
	for (i = 0; i < MAX_VCA_CARD_CPUS; ++i)
		xdev->power_ts[i] = INITIAL_JIFFIES - msecs_to_jiffies(POWER_GRACE_PERIOD_MS);
	mutex_unlock(&xdev->reset_lock);

}

#if PLX_USE_CPU_DMA
// Prevent connection of dma from the same cpu, because then the dma transmission is ineffective (data are not written)
static bool workaround_dma_filter(struct dma_chan *chan, struct device *dev)
{
	struct pci_dev*const pdev = container_of( dev, struct pci_dev, dev);
	struct pci_dev*const dma_dev = container_of( chan->device->dev, struct pci_dev, dev);
	struct pci_bus const*root_plx= pdev->bus;
	struct pci_bus const*root_dma= dma_dev->bus;
	while( root_plx->parent) root_plx= root_plx->parent;
	while( root_dma->parent) root_dma= root_dma->parent;
	if( root_plx->number!= root_dma->number)
		return true;
	dev_dbg( dev, "Prevent use DMA from bus %x\n", root_dma->number); // WORKAROUND for iommu probably
	return false;
	}
#endif

/**
 * plx_request_dma_chan - Request DMA channel
 * @xdev: pointer to plx_device instance
 *
 * returns 0 if a DMA channel was acquired
 */
bool plx_request_dma_chan(struct plx_device *xdev)
{
	dma_cap_mask_t mask;
	dma_cap_zero(mask);
	dma_cap_set(DMA_MEMCPY, mask);

	if (kvm_check_guest()) {
		xdev->dma_ch = dma_request_channel(mask, NULL,
						   &xdev->pdev->dev);
	} else {
		xdev->dma_ch = dma_request_channel(mask, plx_dma_filter,
						   &xdev->pdev->dev);
	}
#if PLX_USE_CPU_DMA
	if( !xdev->dma_ch) // if not PLX DMA request CPU DMA
		xdev->dma_ch = dma_request_channel( mask,(dma_filter_fn) workaround_dma_filter, &xdev->pdev->dev);
#endif
	if( xdev->dma_ch) {
		int rc= sysfs_create_link( &xdev->pdev->dev.kobj, &xdev->dma_ch->device->dev->kobj, VCA_DMA_LINK_NAME);
		if( rc) // VCA_DMA_PATH is used to show dma_hung status only.
			dev_warn( &xdev->pdev->dev, "Error %i create link " VCA_DMA_LINK_NAME "\n", rc);
		plx_program_rid_lut_dma( xdev); // skiped check error, because it's when xdev->dma_ch is null only.
		dev_dbg( &xdev->pdev->dev, "DMA channels %p\n", xdev->dma_ch);
		return 0; // successful
	}
	dev_dbg( &xdev->pdev->dev, "Missing DMA at %s\n", __func__);
	return -ENODEV;
}

/**
 * plx_free_dma_chan - release DMA channel
 * @xdev: pointer to plx_device instance
 *
 * returns none
 */
static void plx_free_dma_chan(struct plx_device *xdev)
{
	if (xdev->dma_ch) {
		sysfs_remove_link( &xdev->pdev->dev.kobj, VCA_DMA_LINK_NAME);
		dma_release_channel(xdev->dma_ch);
		xdev->dma_ch = NULL;
	}
}

/* helper function to check if pointer is NULL or error. In such case pointer is set to NULL,
 * error is logged and error code is returned. If pointer looks good nothing happens and function
 * returns with 0 */
static inline int _validate_ptr(struct plx_device* xdev, void** ptr, const char* name)
{
	int rc = 0;
	if (IS_ERR_OR_NULL(*ptr)) {
		if (!(*ptr)) {
			rc = -ENODEV;
			dev_err(&xdev->pdev->dev, "failed to register %s device\n", name);
		} else {
			rc = PTR_ERR(*ptr);
			dev_err(&xdev->pdev->dev, "failed to register %s device, rc=%d\n", name, rc);
		}
		*ptr = NULL;
	}
	return rc;
}
static ssize_t _plx_id_led_switch(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) {
	struct plx_device* const xdev = dev_get_drvdata(dev);
	dev_dbg(dev, "instruction written to id-led device attribute: %s", buf);
	if (!strcmp(buf, "id-led_on"))
		plx_id_led_switch(xdev, true);
	else if (!strcmp(buf, "id-led_off"))
		plx_id_led_switch(xdev, false);
	else{
		dev_warn(dev, "instruction not recognized: %s", buf);
		return -EINVAL;
	}
	return count;
}


static ssize_t plx_show_caterr_cpu(struct device *dev, struct device_attribute *attr, char *buf) {
	struct plx_device* const xdev= dev_get_drvdata( dev);
	int err= plx_read_caterr( xdev);
	switch( err) {
	case 0: memcpy( buf, "caterr", 6); return 6;
	case 1: return 0;
	default: return err;
	}
}

static ssize_t plx_show_power_ok(struct device *dev, struct device_attribute *attr, char *buf) {
	struct plx_device* const xdev= dev_get_drvdata( dev);
	int const ok= plx_read_power_ok( xdev);
	switch( ok) {
	case 0: memcpy( buf, "invalid", 7); return 7;
	case 1: memcpy( buf, "ok", 2); return 2;
	default: return ok;
	}
}

static void plx_unregister_device(struct plx_device *xdev);

/*
 * plx_register_device - register device on virtual bus- VCA_CSM/VOP/VCA_MGR
 * @xdev: pointer to plx_device instance
 *
 * returns 0 on success, < 0 on failure.
 */
static int plx_register_device(struct plx_device *xdev)
{
	int rc;
	// static struct device_attribute caterr;

	if (!xdev->link_side) {
		/* VCA Card OS management only required on host side */
		xdev->vca_csm_dev = vca_csm_register_device(&xdev->pdev->dev,
						      VCA_CSM_DEV_VCA,
						      &vca_csm_plx_hw_ops);
		if ((rc = _validate_ptr(xdev, (void**)&xdev->vca_csm_dev, "vca_csm")))
			goto err;

		/* VCA manager required only on host side and on first plx_device */
		if (plx_identify_cpu_id(xdev->pdev) == 0) {
			xdev->vca_mgr_dev = vca_mgr_register_device(&xdev->pdev->dev,
				VCA_MGR_DEV_VCA,
				&vca_mgr_plx_hw_ops);
			if ((rc =  _validate_ptr(xdev, (void**)&xdev->vca_mgr_dev, "vca_mgr")))
				goto err;
		} else if(plx_identify_cpu_id(xdev->pdev) == 2) {
			xdev->vca_mgr_extd_dev = vca_mgr_extd_register_device(&xdev->pdev->dev,
				VCA_MGR_EXTD_DEV_VCA,
				&vca_mgr_extd_plx_hw_ops);
			if ((rc = _validate_ptr(xdev, (void**)&xdev->vca_mgr_extd_dev, "vca_mgr_ext")))
				goto err;

		}

		xdev->blockio.be_dev = vcablkebe_register(&xdev->pdev->dev,
				&blockio_hw_ops,
				xdev->dma_ch,
				xdev->card_id,
				plx_identify_cpu_id(xdev->pdev));

		if ((rc =  _validate_ptr(xdev, (void**)&xdev->blockio.be_dev, "blockio backend")))
			goto err;

		switch(xdev->card_type) {
			case VCA_VCGA_FAB1:
			case VCA_VCAA_FAB1: {
				static struct device_attribute const power_ok = { { "power_ok", S_IRUGO }, plx_show_power_ok, NULL };
				static struct device_attribute const id_led = { { "id-led", S_IWUGO }, NULL, _plx_id_led_switch };
				if( device_create_file( &xdev->pdev->dev, &power_ok))
					dev_warn( &xdev->pdev->dev, "device_create_file power_ok\n");
				if (device_create_file(&xdev->pdev->dev, &id_led))
					dev_warn(&xdev->pdev->dev, "device_create_file id-led\n");
				} // fallthrough
			case VCA_FPGA_FAB1: {
				static struct device_attribute const caterr_cpu = { { "caterr", S_IRUGO }, plx_show_caterr_cpu, NULL };
				if( device_create_file( &xdev->pdev->dev, &caterr_cpu))
					dev_warn( &xdev->pdev->dev, "device_create_file caterr\n");
				} // fallthrough
			default: ;
		}
	} else {
		xdev->vpdev = vop_register_device(&xdev->pdev->dev,
						  VOP_DEV_TRNSP, &_plx_dma_ops,
						  &vop_hw_ops, 0, &xdev->aper,
						  xdev->dma_ch);
		if ((rc = _validate_ptr(xdev, (void**)&xdev->vpdev, "vop")))
			goto err;

		xdev->vca_csa_dev = vca_csa_register_device(&xdev->pdev->dev,
			VCA_CSA_DEV_VCA,
			&vca_csa_plx_hw_ops);
		if ((rc = _validate_ptr(xdev, (void**)&xdev->vca_csa_dev, "csa")))
			goto err;

		xdev->blockio.fe_dev = vcablk_register(&xdev->pdev->dev, &blockio_hw_ops,
						xdev->blockio.dp_va, BLOKIO_DEVPAGE_SIZE);
		if ((rc = _validate_ptr(xdev, (void**)&xdev->blockio.fe_dev, "blockio frontend")))
			goto err;
	}

	return 0;
err:
	/* plx_unregister_device will unregister only these devices which were
	successfully registerd */
	plx_unregister_device(xdev);

	return rc;
}

/**
 * plx_unregister_device - unregister device from virtual buses- VCA_CSM/VOP/VCA_MGR
 * @xdev: pointer to plx_device instance
 */
static void plx_unregister_device(struct plx_device *xdev)
{
	if (!xdev->link_side) {
		if (xdev->vca_csm_dev)
			vca_csm_unregister_device(xdev->vca_csm_dev);
		if (xdev->vca_mgr_dev)
			vca_mgr_unregister_device(xdev->vca_mgr_dev);
		if (xdev->blockio.be_dev) {
			vcablkebe_unregister(xdev->blockio.be_dev);
			xdev->blockio.be_dev = NULL;
		}

	} else {
		if (xdev->vca_csa_dev)
			vca_csa_unregister_device(xdev->vca_csa_dev);
		if (xdev->hs_done) {
			if(xdev->vpdev) {
				vop_unregister_device(xdev->vpdev);
				xdev->vpdev = 0;
			}
		}
		if (xdev->blockio.fe_dev) {
			vcablk_unregister(xdev->blockio.fe_dev);
			xdev->blockio.fe_dev = NULL;
		}
	}
}

/**
 * plx_deffered_hs_thread_fn - function invoked as deffered handhake handler
 *	from dedicated thread
 * @data: pointer to plx_device instance
 *
 * returns 0 on success, < 0 on failure.
 */
static int plx_deffered_hs_thread_fn(void * data) {
	return plx_register_device((struct plx_device *) data);
}

/**
 * plx_deffered_rdp_init - function invoked as deffered handhake handler from
 *	generic irq handler thread
 * @irq: number of triggered irq
 * @dev: pointer to plx_device instance
 *
 * returns 0 on success, < 0 on failure.
 */
static irqreturn_t plx_deffered_rdp_init(int irq, void *dev)
{
	struct plx_device *xdev = dev;
	int rc;
	if(xdev->hs_done) {
		dev_err(&xdev->pdev->dev,
			"Unexpected handshake IRQ; possible reset on host side\n");
		return -EIO;
	}

	plx_a_lut_clear(xdev, xdev->a_lut_array_base);

	rc = plx_rdp_init(xdev);

	if(rc){
		dev_err(&xdev->pdev->dev, "plx_rdp_init failed rc %d\n", rc);
		return rc;
	}

	if(!xdev->hs_done) {
		dev_err(&xdev->pdev->dev, "Deffered handshake failed\n");
		return 0;
	}

	wake_up_process(plx_deffered_hs_thread);

	return rc;
}

/**
 * plx_register_deffered_hs - register deffered handhake handler and irq
 * @xdev: pointer to plx_device instance
 *
 * returns 0 on success, < 0 on failure.
 */
static int plx_register_deffered_hs(struct plx_device *xdev)
{
	int db;
	int rc;
	dev_err(&xdev->pdev->dev,
		"Deffer registration until Handshake with the other side done\n");

	plx_deffered_hs_thread = kthread_create(plx_deffered_hs_thread_fn,
		(void *) xdev, "PLX deffered HS thread");
	if(IS_ERR(plx_deffered_hs_thread)) {
		dev_err(&xdev->pdev->dev,
			"Couldn't register deffered handshake thread\n");
		rc = PTR_ERR(plx_deffered_hs_thread);
		goto exit;
	}

	db = plx_next_db(xdev);
	xdev->hs_irq = plx_request_threaded_irq(xdev, NULL, plx_deffered_rdp_init,
		"handshake", xdev, db);
	if(IS_ERR(xdev->hs_irq)) {
		rc = PTR_ERR(xdev->hs_irq);
		dev_err(&xdev->pdev->dev,
			"Couldn't register IRQ for deffered handshake, rc %d\n", rc);
		kthread_stop(plx_deffered_hs_thread);
		goto exit;
	}

	plx_write_spad(xdev, PLX_HSDB_SPAD,
		(PLX_HSDB_CMD << PLX_HSDB_SHIFT) | (db & ~PLX_HSDB_MASK));
	dev_info(&xdev->pdev->dev, "Registered deffered Handshake on IRQ %d\n", db);
	rc = 0;

exit:
	return rc;
}

/**
 * plx_card_get_type - get type of VCA card.
  * @pdev: pointer to pci_dev instance
 * RETURNS: type of VCA card
 */
static enum vca_card_type plx_card_get_type(struct pci_dev *pdev)
{
	switch (pdev->subsystem_device) {
	case SUBSYSTEM_ID_VV_FAB1:
		return VCA_VV_FAB1;
	case SUBSYSTEM_ID_VV_FAB2:
		return VCA_VV_FAB2;
	case SUBSYSTEM_ID_MV_FAB1:
		return VCA_MV_FAB1;
	case SUBSYSTEM_ID_VCAA_FAB1:
		return VCA_VCAA_FAB1;
	case SUBSYSTEM_ID_VCGA_FAB1:
		return VCA_VCGA_FAB1;
	case SUBSYSTEM_ID_FPGA_FAB1:
			return VCA_FPGA_FAB1;
	}
	dev_err(&pdev->dev, "Unknown card type! subsystemID 0x%x\n",
			pdev->subsystem_device);
	return VCA_UNKNOWN;
}

/**
 * plx_identify_self - Find on what PCI-e card this device is and its type
 * @pdev: PCI device structure
 * return 0 on success, < 0 on failure.
 */
static int plx_identify_self(struct plx_device *xdev)
{
	/* Identified Card ID not on the PCI topology,
	 * but check PCI list device and detect numbers
	 * of previous occurrences NODE0 related with actual node.
	 * Devices on the PCI list have to be in that same ordering on host and VM.
	 * */
	struct pci_dev * pdev = xdev->pdev;
	struct pci_dev * found_dev = NULL;
	int card_id = -1;

	while ((found_dev = pci_get_device(
#ifdef RDK_SUPPORT
			PCI_ANY_ID,
#else
			PCI_VENDOR_ID_INTEL,
#endif
			PCI_ANY_ID, found_dev)) != NULL) {

		/* Ignore not related devices. */
		if (plx_identify_cpu_id(found_dev) < 0) {
			continue;
		}

		dev_dbg(&pdev->dev, "Found vendor %u device %u subsystem_vendor %u "
				"subsystem_device %u Card ID: %i %s\n", found_dev->vendor,
				found_dev->device, found_dev->subsystem_vendor,
				found_dev->subsystem_device, plx_identify_cpu_id(found_dev),
				(xdev->pdev == found_dev)?" This device":"");

		/* to identify CPU0, new Card */
		if (plx_identify_cpu_id(found_dev) == 0)
			card_id++;

		/* Check that this device is related with instance of drivers. */
		if (xdev->pdev == found_dev)
			break;
	}

	if (found_dev == NULL || card_id < 0)
		return -ENOENT;

	xdev->card_id = card_id;

	dev_info(&pdev->dev, "Identify CARD_ID: %i, CPU_ID %i\n", xdev->card_id,
			plx_identify_cpu_id(xdev->pdev));

	return 0;
}


/**
 * plx_device_create - Creates plx device
 * @xdev: Pointer to created device
 * @pdev: PCI device structure
 * returns 0 on success, < 0 on failure.
 */
static int plx_device_create(struct plx_device **xdevp, struct pci_dev *pdev)
{
	int rc = 0;
	struct plx_device *xdev;
	xdev = kzalloc(sizeof(*xdev), GFP_KERNEL);
	if (!xdev) {
		rc = -ENOMEM;
		dev_err(&pdev->dev, "xdev kmalloc failed rc %d\n", rc);
		goto finish;
	}

	xdev->id = ida_simple_get(&g_plx_ida, 0, VCA_MAX_NUM_CPUS, GFP_KERNEL);
	if (xdev->id < 0) {
		rc = xdev->id;
		dev_err(&pdev->dev, "ida_simple_get failed rc %d\n", rc);
		goto ida_fail;
	}

	*xdevp = xdev;
	goto finish;

ida_fail:
	kfree(xdev);
finish:
	return rc;
}

/**
 * plx_device_destroy - Destroys plx device
 * @xdev: Pointer to plx device to destroy
 */
static void plx_device_destroy(struct plx_device *xdev)
{
	ida_simple_remove(&g_plx_ida, xdev->id);
	kfree(xdev);
}

/**
 * plx_pci_device_init - Init pci device for plx device
 * @pdev: PCI device structure
 * returns 0 on success, < 0 on failure.
 */
static int plx_pci_device_init(struct pci_dev *pdev)
{
	int rc = pci_enable_device(pdev);
	if (rc) {
		dev_err(&pdev->dev, "failed to enable pci device.\n");
		goto finish;
	}

	pci_set_master(pdev);

	rc = pci_request_regions(pdev, plx_driver_name);
	if (rc) {
		dev_err(&pdev->dev, "failed to get pci regions.\n");
		goto disable_device;
	}

	rc = pci_set_dma_mask(pdev, DMA_BIT_MASK(64));
	if (rc) {
		dev_err(&pdev->dev, "Cannot set DMA mask\n");
		goto release_regions;
	}
	goto finish;

release_regions:
	pci_release_regions(pdev);
disable_device:
	pci_disable_device(pdev);
finish:
	return rc;
}

/**
 * plx_pci_device_deinit - Deinitialize pci device used by plx device
 * @pdev: PCI device structure
 */
static void plx_pci_device_deinit(struct pci_dev *pdev)
{
	pci_release_regions(pdev);
	pci_disable_device(pdev);
}

/**
 * plx_map_resources - Mapping resources to device
 * @xdev: plx device structure
 * @pdev: PCI device structure
 * returns 0 on success, < 0 on failure.
 */
static int plx_map_resources(struct plx_device *xdev, struct pci_dev *pdev)
{
	int rc = 0;

	xdev->mmio.pa = pci_resource_start(pdev, PLX_MMIO_BAR);
	xdev->mmio.len = pci_resource_len(pdev, PLX_MMIO_BAR);
	xdev->mmio.va = pci_ioremap_bar(pdev, PLX_MMIO_BAR);

	if (!xdev->mmio.va) {
		dev_err(&pdev->dev, "Cannot remap %llx MMIO BAR\n", xdev->mmio.len);
		rc = -EIO;
		goto finish;
	}

	dev_info(&pdev->dev, "mmio pa 0x%llx len 0x%llx va %p\n",
		 xdev->mmio.pa, xdev->mmio.len, xdev->mmio.va);

	xdev->aper.pa = pci_resource_start(pdev, PLX_APER_BAR);
	xdev->aper.len = pci_resource_len(pdev, PLX_APER_BAR);
	xdev->aper.va = ioremap_wc(xdev->aper.pa, xdev->aper.len);

	if (!xdev->aper.va) {
		dev_err(&pdev->dev, "Cannot remap %llx Aperture BAR\n", xdev->aper.len);
		rc = -EIO;
		goto unmap_mmio;
	}

	dev_info(&pdev->dev, "aper pa 0x%llx len 0x%llx va %p\n",
		 xdev->aper.pa, xdev->aper.len, xdev->aper.va);
	goto finish;

unmap_mmio:
	iounmap(xdev->mmio.va);
finish:
	return rc;
}

/**
 * plx_unmap_resources - Unmaps resources used by device
 * @xdev: plx device structure
 * @pdev: PCI device structure
 */
static void plx_unmap_resources(struct plx_device *xdev)
{
	iounmap(xdev->aper.va);
	iounmap(xdev->mmio.va);
}

/**
 * plx_try_deinit_lbp - Deinit lbp based on configuration
 * @xdev: plx device structure
 */
static void plx_try_deinit_lbp(struct plx_device *xdev)
{
	if (!xdev->link_side) {
		if (xdev->card_type & VCA_PRODUCTION) {
			plx_lbp_deinit(xdev);
			unregister_plx_context(xdev);
		}
	}
}

/**
 * plx_alloc_blockio_dev_page - allocate memory used for blockio device injection
 * @xdev: plx device structure
 */
static int plx_alloc_blockio_dev_page(struct plx_device *xdev)
{
	dma_addr_t dma_addr;
	void* va;

	// dma_alloc_coherent() zeroes out memory on  allocation:
	va = dma_alloc_coherent(&xdev->pdev->dev, BLOKIO_DEVPAGE_SIZE, &dma_addr,
				GFP_KERNEL |  GFP_DMA);

	if (!va) {
		dev_err(&xdev->pdev->dev, "failed to allocate blockio dev page\n");
		return -ENOMEM;
	}

	xdev->blockio.dp_va = va;
	xdev->blockio.dp_da = dma_addr;

	return 0;
}

/**
 * plx_dealloc_blockio_dev_page - deallocate memory used for blockio device injection
 * @xdev: plx device structure
 */
static void plx_dealloc_blockio_dev_page(struct plx_device *xdev)
{
	if (xdev->blockio.dp_va) {
		dma_free_coherent(&xdev->pdev->dev,
			BLOKIO_DEVPAGE_SIZE,
			xdev->blockio.dp_va,
			xdev->blockio.dp_da);
		 xdev->blockio.dp_va = NULL;
		 xdev->blockio.dp_da = 0;
	}
}

/**
 * plx_check_node_link_state - check if PCIe link to node is in expected state
 * (established Gen 3);
 * @xdev: plx device structure
 * return: true if link established, otherwise return false
 */
static bool plx_check_node_link_state(struct plx_device *xdev)
{
	uint reg_val = plx_mmio_read(&xdev->mmio, xdev->reg_base + PLX_LINK_STATUS_AND_CONTROL_REGISTER);
	dev_dbg(&xdev->pdev->dev, "Status reg=0x%0x\n", reg_val);

	if (!(reg_val & PLX_LINK_WIDTH_BITMASK)) {
		dev_dbg(&xdev->pdev->dev, "Link down\n");
		return false;
	}

	if ((reg_val & PLX_LINK_GEN_BITMASK) !=
	    (PLX_LINK_GEN3_VALUE << PLX_LINK_GEN_OFFSET) ) {
		dev_dbg(&xdev->pdev->dev, "Link degragaded\n");
		return false;
	}

	dev_dbg(&xdev->pdev->dev, "Link up\n");
	return true;
}

/**
 * plx_wait_good_link - wait up to 60 seconds for expected node PCIe link
 * state (established Gen 3)
 * @xdev: plx device structure
 * return: true if the expected state reached
 */
static bool plx_wait_good_link(struct plx_device *xdev)
{
	int retry_count = 0;
	while (retry_count < LINK_WAIT_TIMEOUT) {
		if (plx_check_node_link_state(xdev)) {
			dev_info(&xdev->pdev->dev, "Node link established\n");
			return true;
		}
		else
			dev_dbg(&xdev->pdev->dev, "%s: No link yet\n", __func__);
		if (++retry_count < LINK_WAIT_TIMEOUT)
			msleep(1000);
	}
	return false;
}

/**
 * plx_first_boot_mgr - thread function managing node link establishement after
 * power on
 * @plxdev: plx device structure
 * return: 0 on success, otherwise -errno
 *
 * Press long and short power button to equalize power state of node.
 * If no link after 60 seconds, press long and short power button again,
 * try this 3 times. Function shouldn't be invoked for VCA gen 1.
 */
static int plx_first_boot_mgr(void *plxdev)
{
	struct plx_device *xdev = plxdev;
	int err = 0;
	int tries_pwr = 3;

	if (plx_identify_cpu_id(xdev->pdev) < 0) {
		dev_err(&xdev->pdev->dev, "%s: unknown device\n", __func__);
		err = -ENODEV;
		goto exit;
	}


	while (tries_pwr) {
		--tries_pwr;

		plx_card_press_power_button(plx_contexts[xdev->card_id][0], NULL,
						plx_identify_cpu_id(xdev->pdev), true, NULL);

		msleep(500);

		plx_card_press_power_button(plx_contexts[xdev->card_id][0], NULL,
						plx_identify_cpu_id(xdev->pdev), false, NULL);

		if (plx_wait_good_link(xdev)) {
			err = 0;
			goto exit;
		} else {
			dev_info(&xdev->pdev->dev, "No good node link for %d seconds, "
					"left tries of power button cycles %i\n",
					LINK_WAIT_TIMEOUT, tries_pwr);
		}
	}

	dev_err(&xdev->pdev->dev, "Node link cannot be established!!!\n");
	err = -ETIME;

exit:
	xdev->first_time_boot_mgr = false;
	do_exit(err);
	return err;
}

/**
 * plx_probe - Device Initialization Routine
 *
 * @pdev: PCI device structure
 * @ent: entry in plx_pci_tbl
 *
 * returns 0 on success, < 0 on failure.
 */
static int plx_probe(struct pci_dev *pdev,
		     const struct pci_device_id *ent)
{
	int rc;
	struct plx_device *xdev;
	enum vca_card_type card_type = VCA_UNKNOWN;

	dev_info(&pdev->dev,
		"buildinfo: build no " BUILD_NUMBER ", built on " BUILD_ONDATE ".\n");

#if PLX_MEM_DEBUG
	dev_err(&pdev->dev, "DRIVERS RUN WITH DEBUG MEMORY MODE!!!\n");
#endif

	card_type = plx_card_get_type(pdev);
	if (card_type == VCA_UNKNOWN) {
		rc = -ENXIO;
		dev_err(&pdev->dev, "Unknown card type! subsystemID 0x%x\n",
				pdev->subsystem_device);
		goto plx_create_device_fail;
	} else if (card_type & VCA_VV) {
		dev_info(&pdev->dev, "Detected device 'ValleyVista' subsystemID 0x%x\n",
				pdev->subsystem_device);
	} else if (card_type & VCA_MV) {
		dev_info(&pdev->dev, "Detected device 'MonteVista' subsystemID 0x%x\n",
				pdev->subsystem_device);
	} else if (card_type & VCA_FPGA) {
		dev_info(&pdev->dev, "Detected device 'FPGA' subsystemID 0x%x\n",
				pdev->subsystem_device);
	} else if (card_type & VCA_VCAA) {
		dev_info(&pdev->dev, "Detected device 'VCAC-A' subsystemID 0x%x\n",
				pdev->subsystem_device);
	} else if (card_type & VCA_VCGA) {
		dev_info(&pdev->dev, "Detected device 'VCAC-R' subsystemID 0x%x\n",
			pdev->subsystem_device);
	}

	rc = plx_device_create(&xdev, pdev);
	if (rc) {
		dev_err(&pdev->dev, "plx_device_create failed %d\n", rc);
		goto plx_create_device_fail;
	}

	plx_device_init(xdev, pdev);

	xdev->card_type = card_type;

	rc = plx_pci_device_init(pdev);
	if (rc) {
		dev_err(&pdev->dev, "plx_pci_device_init failed %d\n", rc);
		goto destroy_plx_device;
	}

	rc = plx_map_resources(xdev, pdev);
	if (rc) {
		dev_err(&pdev->dev, "plx_map_resources failed %d\n", rc);
		goto deinit_pci_device;
	}

	rc = plx_hw_init(xdev, pdev);
	if (rc) {
		dev_err(&pdev->dev, "plx_hw_init failed %d\n", rc);
		goto unmap_resources;
	}

	if (!xdev->link_side) {
		rc = plx_identify_self(xdev);
		if (rc) {
			dev_err(&pdev->dev,
				"Could not identify card for pci_device with "
				"vendor %x, device_id %x !\n",
				(unsigned int)PCI_VENDOR_ID_INTEL,
				(unsigned int)INTEL_VCA_PCI_NODE0_ID);
			goto unmap_resources;
		}

		xdev->mmio_link_offset = plx_identify_cpu_id(xdev->pdev) == 1
			? 0x3d000 : 0x3f000;
	}

	plx_intr_init(xdev);
	rc = plx_setup_interrupts(xdev, pdev);
	if (rc) {
		dev_err(&pdev->dev, "plx_setup_interrupts failed %d\n", rc);
		goto unmap_resources;
	}
	pci_set_drvdata(pdev, xdev);


	if (!xdev->link_side) {
		xdev->blockio.ftb_db = plx_next_db(xdev);
	}

	if (!xdev->link_side) {
		rc = plx_dp_init(xdev);
		if (rc) {
			dev_err(&pdev->dev, "plx_dp_init failed rc %d\n", rc);
			goto free_interrupts;
		}
		plx_bootparam_init(xdev);
		plx_dp_notify(xdev);
	} else {
		rc = plx_rdp_init(xdev);
		if (rc) {
			dev_err(&pdev->dev, "plx_rdp_init failed rc %d\n", rc);
			goto free_interrupts;
		}
	}
	plx_create_debug_dir(xdev);

	dev_info(&pdev->dev, "link side %d\n", xdev->link_side);
	switch(xdev->link_side) {
	case node_side:
		if (xdev->rdp->flags.DontUseDma)
			break;
		// fallthrough
	case host_side:
		if (plx_request_dma_chan(xdev)) {
			xdev->dma_ch = NULL;
			dev_warn(&pdev->dev, "plx_request_dma_chan failed\n");
		}
	}
	if (xdev->card_type & VCA_PRODUCTION) {
		if (!xdev->link_side) {
			register_plx_context(xdev);
			rc = plx_lbp_init(xdev);
			if (rc) {
				dev_err(&pdev->dev, "%s error initialiing LBP rc %d\n",
					__func__, rc);
				goto dma_remove;
			}
		}
	}
	else {
		if (!xdev->hs_done) {
			rc = plx_register_deffered_hs(xdev);
			dev_err(&pdev->dev, "plx_register_deffered_hs failed rc %d\n", rc);
			if (rc)
				goto dma_remove;
			else
				goto exit;
		}
	}

	if ((!xdev->link_side) && (plx_contexts[xdev->card_id][0])) {
		if (xdev->card_type & VCA_VV) {
			xdev->first_time_boot_mgr = false;
			plx_card_reset(plx_contexts[xdev->card_id][0], NULL, plx_identify_cpu_id(xdev->pdev));
		}
		else if ((xdev->card_type & VCA_MV) || (xdev->card_type & VCA_FPGA)
			|| (xdev->card_type & VCA_VCAA) || (xdev->card_type & VCA_VCGA)) {
			plx_init_vca_g2_gpios(plx_contexts[xdev->card_id][0]);
			xdev->first_time_boot_mgr = true;
			kthread_run(plx_first_boot_mgr, (void *) xdev, "VCA first boot mgr");
		}
	}

	if (xdev->link_side) {
		rc = plx_alloc_blockio_dev_page(xdev);
		if (rc) {
			dev_err(&pdev->dev, "plx_alloc_blockio_dev_page failed rc %d\n",rc);
			goto lbp_deinit;
		}
	}

	rc = plx_register_device(xdev);
	if (rc) {
		dev_err(&pdev->dev, "plx_register_device failed rc %d\n", rc);
		goto blockio_dev_page_dealloc;
	}
	rc = 0; /* Ignore warnings, when not cleanup driver. */

	if (!xdev->link_side) { // host side
		int cpu_id = plx_identify_cpu_id(xdev->pdev);
		if (-1 == cpu_id) {
			dev_err(&pdev->dev, "Wrong cpu_id %d\n", cpu_id);
			goto blockio_dev_page_dealloc;
		}
		g_vca_xdev[xdev->card_id][cpu_id] = xdev;
		dev_dbg( &pdev->dev, "SGX: g_vca_xdev[%d][%d] is %p\n",xdev->card_id,plx_identify_cpu_id(xdev->pdev),xdev);
	} else { //card side
		g_vca_xdev[0][0] = xdev; //For card side always store xdev in position [0][0]
		dev_dbg( &pdev->dev, "SGX: g_vca_xdev[0][0] is %p",xdev);
	}
	goto exit;

blockio_dev_page_dealloc:
	if (xdev->link_side)
		plx_dealloc_blockio_dev_page(xdev);
lbp_deinit:
	plx_try_deinit_lbp(xdev);
dma_remove:
	plx_free_dma_chan(xdev);
	plx_delete_debug_dir(xdev);
	if (!xdev->link_side) {
		plx_dp_uninit(xdev);
	}

free_interrupts:
	plx_free_interrupts(xdev, pdev);
unmap_resources:
	if ((xdev->link_side) && (rc != -EPROBE_DEFER))
		plx_write_spad(xdev, PLX_LBP_SPAD_i7_READY, PLX_LBP_i7_DRV_PROBE_ERROR);
	plx_unmap_resources(xdev);
deinit_pci_device:
	plx_pci_device_deinit(pdev);
destroy_plx_device:
	plx_device_destroy(xdev);
plx_create_device_fail:
	dev_err(&pdev->dev, "Probe failed rc %d\n", rc);
exit:
	return rc;
}

/**
 * plx_remove - Device Removal Routine
 * plx_remove is called by the PCI subsystem to alert the driver
 * that it should release a PCI device.
 *
 * @pdev: PCI device structure
 */
static void plx_remove(struct pci_dev *pdev)
{
	struct plx_device *xdev = pci_get_drvdata(pdev);

	if (!xdev)
		return;

	plx_try_deinit_lbp(xdev);

	plx_unregister_device(xdev);

	if (xdev->link_side)
		plx_dealloc_blockio_dev_page(xdev);

	plx_hw_deinit(xdev);

	plx_mmio_write(&xdev->mmio, 0, xdev->reg_base + PLX_A_LUT_CONTROL);
	plx_free_dma_chan(xdev);
	plx_delete_debug_dir(xdev);
	if (!xdev->link_side)
		plx_dp_uninit(xdev);
	plx_free_interrupts(xdev, pdev);
	plx_unmap_resources(xdev);
	plx_pci_device_deinit(pdev);
	plx_device_destroy(xdev);
}

static void plx_shutdown(struct pci_dev *pdev)
{
	plx_remove(pdev);
}

static struct pci_driver plx_driver = {
	.name = plx_driver_name,
	.id_table = plx_pci_tbl,
	.probe = plx_probe,
	.remove = plx_remove,
	.shutdown = plx_shutdown
};

static int __init plx_init(void)
{
	int ret = 0;

	plx_init_debugfs();
	ret = plx_init_procfs();
	if (ret) {
		pr_err("plx_init_procfs failed\n");
		goto cleanup_debugfs;
	}
	ida_init(&g_plx_ida);

	ret = pci_register_driver(&plx_driver);
	if (ret) {
		pr_err("pci_register_driver failed ret %d\n", ret);
		goto cleanup_procfs;
	}
	return ret;
cleanup_procfs:
	plx_exit_procfs();
cleanup_debugfs:
	ida_destroy(&g_plx_ida);
	plx_exit_debugfs();
	return ret;
}

static void __exit plx_exit(void)
{
	pci_unregister_driver(&plx_driver);
	ida_destroy(&g_plx_ida);
	plx_exit_debugfs();
	plx_exit_procfs();
}

module_init(plx_init);
module_exit(plx_exit);

MODULE_AUTHOR("Intel Corporation");
MODULE_DESCRIPTION("Intel(R) PLX87XX VCA PCIe driver");
MODULE_LICENSE("GPL v2");

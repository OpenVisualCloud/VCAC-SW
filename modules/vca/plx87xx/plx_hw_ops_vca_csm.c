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

#include "plx_device.h"
#include "plx_hw.h"
#include <linux/delay.h>
#include "../blockio/hal_pci/vcablk_hal_pci.h"

#define VCA_AGENT_STD_TIMEOUT	10
#define VCA_AGENT_RENEW_TIMEOUT	20

static inline struct plx_device *vca_csmdev_to_xdev(
	struct vca_csm_device *cdev)
{
	return dev_get_drvdata(cdev->dev.parent);
}

/**
 * plx_start - Start the VCA.
 * @xdev: pointer to plx_device instance
 * @buf: buffer containing boot string including firmware/ramdisk path.
 *
 * This function prepares an VCA for boot and initiates boot.
 * RETURNS: An appropriate -ERRNO error value on error, or zero for success.
 */
static int plx_start(struct vca_csm_device *cdev, int id)
{
	struct plx_device *xdev = vca_csmdev_to_xdev(cdev);
	int rc = 0;

	if(xdev->vpdev)
		return -EEXIST;

	xdev->vpdev = vop_register_device(&xdev->pdev->dev,
					  VOP_DEV_TRNSP, &_plx_dma_ops,
					  &vop_hw_ops, id + 1, &xdev->aper,
					  xdev->dma_ch);
	if (IS_ERR(xdev->vpdev)) {
		rc = PTR_ERR(xdev->vpdev);
		xdev->vpdev = 0;
		goto ret;
	}

#ifdef SCIF_ENABLED
	rc = plx_scif_setup(xdev);
	if (rc)
		goto scif_remove;
	}
#endif

	plx_enable_interrupts(xdev);
	plx_set_dp_addr(xdev, xdev->dp_dma_addr);
	goto ret;

#ifdef SCIF_ENABLED
scif_remove:
	if (xdev->scdev) {
		scif_unregister_device(xdev->scdev);
		xdev->scdev = 0;
	}
#endif

ret:
	return rc;
}

/**
 * plx_stop - Prepare the VCA for reset and trigger reset.
 * @xdev: pointer to plx_device instance
 * @force: force a VCA to reset even if it is already offline.
 *
 * RETURNS: None.
 */
static void plx_stop(struct vca_csm_device *cdev, bool force)
{
	struct plx_device *xdev = vca_csmdev_to_xdev(cdev);
	struct vca_bootparam * boot_params = xdev->dp;
#ifdef SCIF_ENABLED
	if (xdev->scdev) {
		scif_unregister_device(xdev->scdev);
		xdev->scdev = 0;
	}
#endif

	if(xdev->vpdev) {
		vop_unregister_device(xdev->vpdev);
		xdev->vpdev = 0;
		plx_clear_dma_mapped_mem(xdev,
			&boot_params->net_config_dma_addr,
			&boot_params->net_config_size,
			&cdev->net_config_va);
		plx_clear_dma_mapped_mem(xdev,
			&boot_params->net_config_windows_dma_addr,
			&boot_params->net_config_windows_size,
			&cdev->net_config_windows_va);
		plx_clear_dma_mapped_mem(xdev,
			&boot_params->sys_config_dma_addr,
			&boot_params->sys_config_size,
			&cdev->sys_config_va);
	}

	if(xdev->blockio.be_dev)
	{
		int rc;
		vcablkbe_unregister_f2b_callback(xdev->blockio.be_dev);
		if (!xdev->link_side) {
			/* Refresh doorbell */
			xdev->blockio.ftb_db = plx_next_db(xdev);
		}
		rc = vcablkbe_register_f2b_callback(xdev->blockio.be_dev);
		if (rc)
		{
			pr_err("%s: failed blockio initialization, rc=%d\n", __func__, rc);
		}
	}
	plx_bootparam_init(xdev);
}

static ssize_t _plx_set_net_config(struct vca_csm_device *cdev,
 const char *buf, size_t count)
{
	struct plx_device *xdev = vca_csmdev_to_xdev(cdev);
	struct vca_bootparam * boot_params = xdev->dp;

	return plx_set_config_file(xdev, buf, count,
		&boot_params->net_config_dma_addr,
		&boot_params->net_config_size,
		&cdev->net_config_va);
}

static void * _plx_get_net_config(struct vca_csm_device *cdev, size_t *out_count)
{
	struct plx_device *xdev = vca_csmdev_to_xdev(cdev);
	struct vca_bootparam * boot_params = xdev->dp;

	if (boot_params->net_config_size) {
		*out_count = boot_params->net_config_size;
		return (void*)cdev->net_config_va;
	}

	*out_count = 0;
	return NULL;
}

static ssize_t _plx_set_net_config_windows(struct vca_csm_device *cdev,
	const char *buf, size_t count)
{
	struct plx_device *xdev = vca_csmdev_to_xdev(cdev);
	struct vca_bootparam * boot_params = xdev->dp;

	return plx_set_config_file(xdev, buf, count,
		&boot_params->net_config_windows_dma_addr,
		&boot_params->net_config_windows_size,
		&cdev->net_config_windows_va);
}

static void * _plx_get_net_config_windows(struct vca_csm_device *cdev, size_t *out_count)
{
	struct plx_device *xdev = vca_csmdev_to_xdev(cdev);
	struct vca_bootparam * boot_params = xdev->dp;

	if (boot_params->net_config_windows_size) {
		*out_count = boot_params->net_config_windows_size;
		return (void*)cdev->net_config_windows_va;
	}

	*out_count = 0;
	return NULL;
}

static enum vca_os_type _plx_get_os_type(struct vca_csm_device *cdev)
{
	struct plx_device *xdev = vca_csmdev_to_xdev(cdev);
	struct vca_bootparam * boot_params = xdev->dp;

	return (enum vca_os_type)boot_params->os_type;
}

static ssize_t _plx_set_sys_config(struct vca_csm_device *cdev,
 const char *buf, size_t count)
{
	struct plx_device *xdev = vca_csmdev_to_xdev(cdev);
	struct vca_bootparam * boot_params = xdev->dp;

	return plx_set_config_file(xdev, buf, count,
		&boot_params->sys_config_dma_addr,
		&boot_params->sys_config_size,
		&cdev->sys_config_va);
}

static void * _plx_get_sys_config(struct vca_csm_device *cdev, size_t *out_count)
{
	struct plx_device *xdev = vca_csmdev_to_xdev(cdev);
	struct vca_bootparam * boot_params = xdev->dp;

	if (boot_params->sys_config_size) {
		*out_count = boot_params->sys_config_size;
		return (void*)cdev->sys_config_va;
	}

	*out_count = 0;
	return NULL;
}

static void _plx_get_card_and_cpu_id(struct vca_csm_device *cdev,
 u8 *out_card_id, u8 *out_cpu_id)
{
	struct plx_device *xdev = vca_csmdev_to_xdev(cdev);
	*out_card_id = xdev->card_id;
	*out_cpu_id = plx_identify_cpu_id(xdev->pdev);
}

static u32 _plx_link_width(struct vca_csm_device *cdev)
{
	struct plx_device *xdev = vca_csmdev_to_xdev(cdev);
	return plx_link_width(xdev);
}

static u32 _plx_link_status(struct vca_csm_device *cdev)
{
	struct plx_device *xdev = vca_csmdev_to_xdev(cdev);
	return plx_link_status(xdev);
}

static enum vca_lbp_retval _plx_lbp_handshake(struct vca_csm_device *cdev)
{
	struct plx_device *xdev = vca_csmdev_to_xdev(cdev);
	return plx_lbp_handshake(xdev);
}

/* read card adapter mac address via LBP and set it in device
  boot params */
static enum vca_lbp_retval set_mac_addr(struct plx_device *xdev)
{
	struct vca_bootparam __iomem *bootparam;
	enum vca_lbp_retval ret;

	ret = plx_lbp_get_mac_addr(xdev, xdev->mac_addr);
	if (ret != LBP_STATE_OK) {
		return ret;
	}

	bootparam = xdev->dp;
	memcpy(bootparam->mac_addr, xdev->mac_addr, sizeof(bootparam->mac_addr));

	return ret;
}


static enum vca_lbp_retval _plx_lbp_boot_ramdisk(struct vca_csm_device *cdev,
 void __user * img, size_t img_size)
{
	struct plx_device *xdev = vca_csmdev_to_xdev(cdev);
	enum vca_lbp_retval ret;

	ret = set_mac_addr(xdev);
	if (ret != LBP_STATE_OK) {
		return ret;
	}

	return plx_lbp_boot_ramdisk(xdev, img, img_size);
}

static enum vca_lbp_retval _plx_lbp_boot_blkdisk(struct vca_csm_device *cdev)
{
	struct plx_device *xdev = vca_csmdev_to_xdev(cdev);
	enum vca_lbp_retval ret;

	ret = set_mac_addr(xdev);
	if (ret != LBP_STATE_OK) {
		return ret;
	}

	return plx_lbp_boot_blkdisk(xdev);
}

static enum vca_lbp_retval _plx_lbp_boot_via_pxe(struct vca_csm_device *cdev)
{
	struct plx_device *xdev = vca_csmdev_to_xdev(cdev);
	enum vca_lbp_retval ret;

	ret = set_mac_addr(xdev);
	if (ret != LBP_STATE_OK) {
		return ret;
	}

	return plx_lbp_boot_via_pxe(xdev);
}

static enum vca_lbp_retval _plx_lbp_boot_from_usb(struct vca_csm_device *cdev)
{
	struct plx_device *xdev = vca_csmdev_to_xdev(cdev);
	enum vca_lbp_retval ret;

	ret = set_mac_addr(xdev);
	if (ret != LBP_STATE_OK) {
		return ret;
	}

	return plx_lbp_boot_from_usb(xdev);
}

static enum vca_lbp_retval _plx_lbp_set_param(struct vca_csm_device *cdev,
 enum vca_lbp_param param, unsigned int value)
{
	struct plx_device *xdev = vca_csmdev_to_xdev(cdev);
	return plx_lbp_set_param(xdev, param, value);
}

static enum vca_lbp_retval _plx_lbp_flash_bios(struct vca_csm_device *cdev,
 void *bios_file, size_t bios_file_size)
{
	struct plx_device *xdev = vca_csmdev_to_xdev(cdev);
	return plx_lbp_flash_bios(xdev, bios_file, bios_file_size);
}

static enum vca_lbp_retval _plx_lbp_clear_smb_event_log(struct vca_csm_device *cdev,
 void *file, size_t file_size)
{
	struct plx_device *xdev = vca_csmdev_to_xdev(cdev);
	return plx_lbp_clear_smb_event_log(xdev, file, file_size);
}

static enum vca_lbp_retval _plx_lbp_update_mac_addr(struct vca_csm_device *cdev,
 void *file, size_t file_size)
{
	struct plx_device *xdev = vca_csmdev_to_xdev(cdev);
	return plx_lbp_update_mac_addr(xdev, file, file_size);
}

static enum vca_lbp_retval _plx_lbp_set_serial_number(struct vca_csm_device *cdev,
 void *file, size_t file_size)
{
	struct plx_device *xdev = vca_csmdev_to_xdev(cdev);
	return plx_lbp_set_serial_number(xdev, file, file_size);
}

static enum vca_lbp_retval _plx_lbp_flash_firmware(struct vca_csm_device *cdev)
{
	struct plx_device *xdev = vca_csmdev_to_xdev(cdev);
	return plx_lbp_flash_firmware(xdev);
}

static enum vca_lbp_states _plx_lbp_get_state(struct vca_csm_device *cdev)
{
	struct plx_device *xdev = vca_csmdev_to_xdev(cdev);
	return plx_lbp_get_state(xdev);
}

static enum vca_lbp_rcvy_states _plx_lbp_get_rcvy_state(struct vca_csm_device *cdev)
{
	struct plx_device *xdev = vca_csmdev_to_xdev(cdev);
	enum vca_lbp_rcvy_states ret;
	mutex_lock(xdev->lbp_lock);
	ret = plx_lbp_get_rcvy_state(xdev);
	mutex_unlock(xdev->lbp_lock);
	return ret;
}

static enum vca_card_type _plx_get_card_type(struct vca_csm_device *cdev)
{
	struct plx_device *xdev = vca_csmdev_to_xdev(cdev);
	return xdev->card_type;
}

static u32 _plx_get_cpu_num(struct vca_csm_device *cdev)
{
	struct plx_device *xdev = vca_csmdev_to_xdev(cdev);
	return plx_get_cpu_num(xdev);
}

static u32 _plx_get_memsize(struct vca_csm_device *cdev)
{
	struct plx_device *xdev = vca_csmdev_to_xdev(cdev);
	return plx_get_memsize(xdev);
}

static enum vca_lbp_retval _plx_lbp_get_mac_addr(struct vca_csm_device *cdev,
 char out_mac_addr[6])
{
	struct plx_device *xdev = vca_csmdev_to_xdev(cdev);
	return plx_lbp_get_mac_addr(xdev, out_mac_addr);
}

static enum vca_lbp_retval _plx_lbp_set_time(struct vca_csm_device *cdev)
{
	struct plx_device *xdev = vca_csmdev_to_xdev(cdev);
	return plx_lbp_set_time(xdev);
}

static enum PLX_LBP_PARAM vca_lbp_bios_param_to_PLX_LBP_PARAM(
	enum vca_lbp_bios_param param)
{
	switch(param) {
	case VCA_LBP_BIOS_PARAM_CPU_MAX_FREQ_NON_TURBO:
		return PLX_LBP_PARAM_CPU_MAX_FREQ_NON_TURBO;
	case VCA_LBP_BIOS_PARAM_VERSION:
		return PLX_LBP_PARAM_BIOS_VER;
	case VCA_LBP_BIOS_PARAM_BUILD_DATE:
		return PLX_LBP_PARAM_BIOS_BUILD_DATE;
	case VCA_LBP_PARAM_SGX:
		return PLX_LBP_PARAM_SGX;
	case VCA_LBP_PARAM_EPOCH_0:
		return PLX_LBP_PARAM_EPOCH_0;
	case VCA_LBP_PARAM_EPOCH_1:
		return PLX_LBP_PARAM_EPOCH_1;
	case VCA_LBP_PARAM_SGX_TO_FACTORY:
		return PLX_LBP_PARAM_SGX_TO_FACTORY;
	case VCA_LBP_PARAM_SGX_OWNER_EPOCH_TYPE:
		return PLX_LBP_PARAM_SGX_OWNER_EPOCH_TYPE;
	case VCA_LBP_PARAM_SGX_MEM:
		return PLX_LBP_PARAM_SGX_MEM;
	case VCA_LBP_PARAM_GPU_APERTURE:
		return PLX_LBP_PARAM_GPU_APERTURE;
	case VCA_LBP_PARAM_TDP:
		return PLX_LBP_PARAM_TDP;
	case VCA_LBP_PARAM_HT:
		return PLX_LBP_PARAM_HT;
	case VCA_LBP_PARAM_GPU:
		return PLX_LBP_PARAM_GPU;
	default:
		return PLX_LBP_PARAM_INVALID;
	}
}

static enum vca_lbp_retval _plx_lbp_set_bios_param(struct vca_csm_device *cdev,
 enum vca_lbp_bios_param param, u64 value)
{
	struct plx_device *xdev = vca_csmdev_to_xdev(cdev);
	return plx_lbp_set_bios_param(xdev,
		vca_lbp_bios_param_to_PLX_LBP_PARAM(param), value);
}

static enum vca_lbp_retval _plx_lbp_get_bios_param(struct vca_csm_device *cdev,
 enum vca_lbp_bios_param param, u64 *value)
{
	struct plx_device *xdev = vca_csmdev_to_xdev(cdev);
	return plx_lbp_get_bios_param(xdev,
		vca_lbp_bios_param_to_PLX_LBP_PARAM(param), value);
}

static __u32 __plx_get_eeprom_crc(struct vca_csm_device *cdev)
{
	struct plx_device *xdev = vca_csmdev_to_xdev(cdev);
	return plx_mmio_read(&xdev->mmio, PLX_EEP_CRC);
}

static void __plx_set_link_down_flag(struct vca_csm_device *cdev)
{
	struct plx_device *xdev = vca_csmdev_to_xdev(cdev);
	plx_write_spad(xdev, PLX_LBP_SPAD_i7_READY, PLX_LBP_i7_DOWN);
}

static void __plx_set_power_off_flag(struct vca_csm_device *cdev)
{
	struct plx_device *xdev = vca_csmdev_to_xdev(cdev);
	plx_write_spad(xdev, PLX_LBP_SPAD_i7_READY, PLX_LBP_i7_POWER_DOWN);
}

static bool vca_validate_agent_cmd(char value)
{
	if(value > 0 && value < VCA_AGENT_CMD_SIZE)
		return true;
	return false;
}

static enum vca_lbp_retval _plx_csm_agent_command(
	struct vca_csm_device *cdev, char* buf, size_t *size)
{
	struct plx_device *xdev = vca_csmdev_to_xdev(cdev);
	struct vca_bootparam * boot_params = xdev->dp;
	int retry, rc;
	int timeout_s;
	char vca_agent_cmd = buf[0];

	if (!vca_validate_agent_cmd(vca_agent_cmd))
		return LBP_UNKNOWN_PARAMETER;

	if (boot_params->h2c_csa_mem_db == 0 ||
	    boot_params->h2c_csa_mem_db == -1) {
		 dev_err(&cdev->dev, "csa agent not ready\n");
			return LBP_INTERNAL_ERROR;
	}

	mutex_lock(&cdev-> vca_csm_host_mutex);

	// in case when card sent data before host wait for it (desynchronization)
	if (boot_params->csa_finished) {
		dev_warn(&cdev->dev, "csa agent finished flag is set\n");
		boot_params->csa_finished = 0;
	}

	boot_params->csa_command = vca_agent_cmd;

	/* tell card that there is new command available */
	plx_send_intr(xdev, boot_params->h2c_csa_mem_db);

	switch (vca_agent_cmd){
	case VCA_AGENT_RENEW:
		timeout_s = VCA_AGENT_RENEW_TIMEOUT;
		break;
	default:
		timeout_s = VCA_AGENT_STD_TIMEOUT;
		break;
	}

	retry = timeout_s * 10;	// sleeping for 100 ms in each step
	while (!READ_ONCE(boot_params->csa_finished) && retry--) {
		msleep(100);
	}

	if (boot_params->csa_finished) {
		*size = plx_read_dma_mapped_mem(xdev,
			&boot_params->card_csa_mem_dma_addr,
			&boot_params->card_csa_mem_size, buf, *size);
		boot_params->csa_finished = 0;
		rc = LBP_STATE_OK;
	}
	else {
		rc = LBP_IRQ_TIMEOUT;
	}

	mutex_unlock(&cdev->vca_csm_host_mutex);
	return rc;
}


struct vca_csm_hw_ops vca_csm_plx_hw_ops = {
	.start = plx_start,
	.stop = plx_stop,

	.set_net_config = _plx_set_net_config,
	.get_net_config = _plx_get_net_config,
	.set_net_config_windows = _plx_set_net_config_windows,
	.get_net_config_windows = _plx_get_net_config_windows,
	.set_sys_config = _plx_set_sys_config,
	.get_sys_config = _plx_get_sys_config,
	.get_os_type    = _plx_get_os_type,

	.get_card_and_cpu_id = _plx_get_card_and_cpu_id,
	.link_width = _plx_link_width,
	.link_status = _plx_link_status,
	.get_card_type = _plx_get_card_type,
	.get_cpu_num = _plx_get_cpu_num,
	.get_memsize = _plx_get_memsize,
	.lbp_boot_ramdisk = _plx_lbp_boot_ramdisk,
	.lbp_boot_blkdisk = _plx_lbp_boot_blkdisk,
	.lbp_boot_via_pxe = _plx_lbp_boot_via_pxe,
	.lbp_boot_from_usb = _plx_lbp_boot_from_usb,
	.lbp_handshake = _plx_lbp_handshake,
	.lbp_set_param = _plx_lbp_set_param,
	.lbp_flash_bios = _plx_lbp_flash_bios,
	.lbp_clear_smb_event_log = _plx_lbp_clear_smb_event_log,
	.lbp_update_mac_addr = _plx_lbp_update_mac_addr,
	.lbp_set_serial_number = _plx_lbp_set_serial_number,
	.lbp_flash_firmware = _plx_lbp_flash_firmware,
	.lbp_get_state = _plx_lbp_get_state,
	.lbp_get_rcvy_state = _plx_lbp_get_rcvy_state,
	.lbp_get_mac_addr = _plx_lbp_get_mac_addr,
	.lbp_set_time = _plx_lbp_set_time,
	.lbp_set_bios_param = _plx_lbp_set_bios_param,
	.lbp_get_bios_param = _plx_lbp_get_bios_param,

	.vca_csm_agent_command = _plx_csm_agent_command,

	.set_link_down_flag = __plx_set_link_down_flag,
	.set_power_off_flag = __plx_set_power_off_flag,
	.get_eeprom_crc = __plx_get_eeprom_crc,
};

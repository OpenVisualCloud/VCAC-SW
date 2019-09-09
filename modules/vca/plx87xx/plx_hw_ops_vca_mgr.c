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

extern struct plx_device * plx_contexts[MAX_VCA_CARDS][MAX_VCA_CARD_CPUS];

static struct plx_device * get_plx_context(struct plx_device *xdev, int cpu_id)
{
	if ((xdev->card_id >= MAX_VCA_CARDS) || (cpu_id < 0) || (cpu_id >= MAX_VCA_CARD_CPUS)) {
		dev_err(&xdev->pdev->dev, "Unknown CPU ID: card %d cpu %d\n",
			xdev->card_id, cpu_id);
		return NULL;
	}

	return plx_contexts[xdev->card_id][cpu_id];
}
static inline struct plx_device *vca_mgr_dev_to_xdev(
	struct vca_mgr_device *vca_mgr_dev)
{
	return dev_get_drvdata(vca_mgr_dev->dev.parent);
}

static void _plx_card_reset(struct vca_mgr_device * vca_mgr_dev, int cpu_id)
{
	struct plx_device *xdev = vca_mgr_dev_to_xdev(vca_mgr_dev);
	struct plx_device *node = get_plx_context(xdev, cpu_id);
	if (!node) {
		dev_warn(&xdev->pdev->dev, "No device context for card %d cpu %d\n", xdev->card_id, cpu_id);
		return;
	}
	if (node->first_time_boot_mgr) {
		dev_warn(&node->pdev->dev, "Cannot perform reset as a first time boot manager works in background\n");
		return;
	}

	plx_card_reset(xdev, &vca_mgr_dev->cpu_threads_compl, cpu_id);
}

static int _plx_card_check_power_button(struct vca_mgr_device * vca_mgr_dev,
 int cpu_id)
{
	struct plx_device *xdev = vca_mgr_dev_to_xdev(vca_mgr_dev);
	return plx_card_check_power_button_state(xdev, cpu_id);
}

static void _plx_enable_bios_recovery_mode(struct vca_mgr_device * vca_dev, int cpu_id)
{
	struct plx_device *xdev = vca_mgr_dev_to_xdev(vca_dev);
	plx_enable_bios_recovery_mode(xdev, cpu_id);
}

static void _plx_disable_bios_recovery_mode(struct vca_mgr_device * vca_dev, int cpu_id)
{
	struct plx_device *xdev = vca_mgr_dev_to_xdev(vca_dev);
	plx_disable_bios_recovery_mode(xdev, cpu_id);
}

static void _plx_card_press_power_button(struct vca_mgr_device * vca_mgr_dev,
	int cpu_id, bool on, struct completion *wait_start)
{
	struct plx_device *xdev = vca_mgr_dev_to_xdev(vca_mgr_dev);
	struct plx_device *node = get_plx_context(xdev, cpu_id);
	if (!node) {
		dev_warn(&xdev->pdev->dev, "No device context for card %d cpu %d\n", xdev->card_id, cpu_id);
		complete_all(wait_start);
		return;
	}
	if (node->first_time_boot_mgr) {
		dev_warn(&node->pdev->dev, "Cannot perform power button press as a first time boot manager works in background\n");
		complete_all(wait_start);
		return;
	}

	if( plx_card_press_power_button(xdev, &vca_mgr_dev->cpu_threads_compl, cpu_id, on, wait_start)< 0)
		complete_all( wait_start);
}

static int _plx_card_get_id(struct vca_mgr_device *vca_mgr_dev)
{
	struct plx_device *xdev = vca_mgr_dev_to_xdev(vca_mgr_dev);
	return xdev->card_id;
}

static enum vca_card_type _plx_card_get_type(
	struct vca_mgr_device *vca_mgr_dev)
{
	struct plx_device *xdev = vca_mgr_dev_to_xdev(vca_mgr_dev);
	return xdev->card_type;
}

static u32 _plx_get_cpu_num(struct vca_mgr_device * vca_mgr_dev)
{
	struct plx_device *xdev = vca_mgr_dev_to_xdev(vca_mgr_dev);
	return plx_get_cpu_num(xdev);
}

static void _plx_set_SMB_id(struct vca_mgr_device *vca_mgr_dev, u8 id)
{
	struct plx_device *xdev = vca_mgr_dev_to_xdev(vca_mgr_dev);
	plx_set_SMB_id(xdev, id);
}

static enum plx_eep_retval _plx_update_eeprom(struct vca_mgr_device * vca_dev,
		char *eeprom_data, size_t eeprom_size)
{
	struct plx_device *xdev = vca_mgr_dev_to_xdev(vca_dev);
	dev_dbg(&xdev->pdev->dev, "change eeprom configuration for plx8733 started\n");
	return plx_update_eeprom(xdev, eeprom_data, eeprom_size);
}

struct vca_mgr_hw_ops vca_mgr_plx_hw_ops = {
	.reset = _plx_card_reset,
	.check_power_button = _plx_card_check_power_button,
	.press_power_button = _plx_card_press_power_button,
	.get_card_id = _plx_card_get_id,
	.get_card_type = _plx_card_get_type,
	.get_cpu_num = _plx_get_cpu_num,
	.set_SMB_id = _plx_set_SMB_id,
	.update_eeprom = _plx_update_eeprom,
	.enable_gold_bios = _plx_enable_bios_recovery_mode,
	.disable_gold_bios = _plx_disable_bios_recovery_mode,
};

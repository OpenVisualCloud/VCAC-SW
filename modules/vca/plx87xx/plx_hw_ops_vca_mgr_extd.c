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

static inline struct plx_device *vca_mgr_extd_dev_to_xdev(
	struct vca_mgr_extd_device *vca_mgr_extd_dev)
{
	return dev_get_drvdata(vca_mgr_extd_dev->dev.parent);
}

static enum plx_eep_retval plx_update_secondary_eeprom(struct vca_mgr_extd_device *vca_dev,
		char *eeprom_data, size_t eeprom_size)
{
	struct plx_device *xdev = vca_mgr_extd_dev_to_xdev(vca_dev);
	dev_dbg(&xdev->pdev->dev, "change eeprom configuration for plx8713 started\n");
	return plx_update_eeprom(xdev, eeprom_data, eeprom_size);
}

static int plx_card_get_id(struct vca_mgr_extd_device *vca_mgr_extd_dev)
{
	struct plx_device *xdev =
		vca_mgr_extd_dev_to_xdev(vca_mgr_extd_dev);
	return xdev->card_id;
}

static __u32 _plx_read_straps(struct vca_mgr_extd_device *vca_dev)
{
	struct plx_device *xdev = vca_mgr_extd_dev_to_xdev(vca_dev);
	return plx_read_straps(xdev);
}

struct vca_mgr_extd_hw_ops vca_mgr_extd_plx_hw_ops = {
	.update_eeprom = plx_update_secondary_eeprom,
	.get_card_id = plx_card_get_id,
	.get_plx_straps = _plx_read_straps,
};

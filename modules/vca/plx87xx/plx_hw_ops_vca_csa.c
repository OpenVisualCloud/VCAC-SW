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

static inline struct plx_device *csadev_to_xdev(
	struct vca_csa_device *cdev)
{
	return dev_get_drvdata(cdev->dev.parent);
}

static struct vca_irq *
_plx_request_irq_csa(struct vca_csa_device *cdev,
		  irqreturn_t (*func)(int irq, void *data),
		  const char *name, void *data, int intr_src)
{
	struct plx_device *xdev = csadev_to_xdev(cdev);

	dev_dbg(&xdev->pdev->dev, "%s intr_src 0x%x, name %s\n",
		__func__, intr_src, name);

	return plx_request_threaded_irq(xdev, func, NULL, name, data, intr_src);
}

static void _plx_free_irq_csa(struct vca_csa_device *vpdev,
			   struct vca_irq *cookie, void *data)
{
	struct plx_device *xdev = csadev_to_xdev(vpdev);

	return plx_free_irq(xdev, cookie, data);
}

static int _plx_next_db(struct vca_csa_device *cdev)
{
	struct plx_device *xdev = csadev_to_xdev(cdev);
	return  plx_next_db(xdev);
}

static ssize_t _plx_set_card_csa_mem(struct vca_csa_device *rdev,
 const char *buf, size_t count)
{
	struct plx_device *xdev = csadev_to_xdev(rdev);
	struct vca_bootparam * boot_params = xdev->rdp;
	ssize_t cnt = plx_set_config_file(xdev, buf, count,
		&boot_params->card_csa_mem_dma_addr,
		&boot_params->card_csa_mem_size,
		&rdev->card_csa_mem_va);
	iowrite8(1, &boot_params->csa_finished);
	return cnt;
}

static int _plx_get_vca_agent_command(struct vca_csa_device *cdev)
{
	struct plx_device *xdev = csadev_to_xdev(cdev);
	struct vca_bootparam * boot_params = xdev->rdp;

	return ioread8(&boot_params->csa_command);
}

static ssize_t _plx_get_net_config_card(struct vca_csa_device *cdev,
				     char * out_buf)
{
	struct vca_bootparam * boot_params;
	struct plx_device *xdev = csadev_to_xdev(cdev);
	*out_buf = 0;

	if (!xdev->hs_done) {
		dev_info(&xdev->pdev->dev, "Can't read net_config "
			"because !hs_done\n");
		return 0;
	}

	boot_params = xdev->rdp;
	return plx_read_dma_mapped_mem(xdev,
		&boot_params->net_config_dma_addr,
		&boot_params->net_config_size, out_buf, PAGE_SIZE);
}

static ssize_t _plx_get_sys_config_card(struct vca_csa_device *cdev,
				     char * out_buf)
{
	struct vca_bootparam * boot_params;
	struct plx_device *xdev = csadev_to_xdev(cdev);
	*out_buf = 0;

	if (!xdev->hs_done) {
		dev_info(&xdev->pdev->dev, "Can't read sys_config "
			"because !hs_done\n");
		return 0;
	}

	boot_params = xdev->rdp;
	return plx_read_dma_mapped_mem(xdev,
		&boot_params->sys_config_dma_addr,
		&boot_params->sys_config_size, out_buf, PAGE_SIZE);
}

static enum vca_lbp_states _plx_get_state(struct vca_csa_device *cdev)
{
	struct plx_device *xdev = csadev_to_xdev(cdev);
	return plx_lbp_get_state(xdev);
}

static enum vca_lbp_retval _plx_set_state(struct vca_csa_device *cdev, enum vca_lbp_states state)
{
	struct plx_device *xdev = csadev_to_xdev(cdev);
	return plx_lbp_set_state(xdev, state);
}

static void __plx_set_h2c_csa_mem_db(struct vca_csa_device *cdev, int db)
{
	struct plx_device *xdev = csadev_to_xdev(cdev);
	struct vca_bootparam * boot_params = xdev->rdp;

	dev_dbg(&cdev->dev, "setting csa mem doorbell %d\n", db);

	boot_params->h2c_csa_mem_db = db;
	return;
}

static void _plx_set_os_type(struct vca_csa_device *cdev, enum vca_os_type os_type)
{
	struct plx_device *xdev = csadev_to_xdev(cdev);
	struct vca_bootparam * boot_params = xdev->rdp;

	dev_dbg(&cdev->dev, "setting os type to %s\n", get_vca_os_type_string(os_type));

	boot_params->os_type = os_type;
	return;
}

struct vca_csa_hw_ops vca_csa_plx_hw_ops = {
	.get_net_config = _plx_get_net_config_card,
	.get_sys_config = _plx_get_sys_config_card,
	.set_csa_mem = _plx_set_card_csa_mem,
	.request_irq = _plx_request_irq_csa,
	.free_irq = _plx_free_irq_csa,
	.next_db = _plx_next_db,
	.set_h2c_csa_mem_db = __plx_set_h2c_csa_mem_db,
	.get_vca_agent_command = _plx_get_vca_agent_command,
	.get_state = _plx_get_state,
	.set_state = _plx_set_state,
	.set_os_type = _plx_set_os_type,
};

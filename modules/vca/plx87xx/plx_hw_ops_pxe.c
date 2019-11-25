/*
 * Intel VCA Software Stack (VCASS)
 *
 * Copyright(c) 2015-2019 Intel Corporation.
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
#include "plx_hw_ops_pxe.h"

static int __plx_is_pxe_booting(struct plx_device *xdev) {
	return plx_lbp_get_state(xdev) == VCA_BOOTING_PXE;
}

struct plx_pxe_hw_ops pxe_hw_ops = {
	.request_threaded_irq = plx_request_threaded_irq,
	.free_irq = plx_free_irq,
	.next_db = plx_next_db,
	.read_spad = plx_read_spad,
	.ioremap = plx_ioremap,
	.iounmap = plx_iounmap,
	.is_pxe_booting = __plx_is_pxe_booting,
};

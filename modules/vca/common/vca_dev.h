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
 * Intel VCA driver.
 *
 */
#ifndef __VCA_DEV_H__
#define __VCA_DEV_H__

/**
 * struct vca_mw - VCA memory window
 *
 * @pa: Base physical address.
 * @va: Base ioremap'd virtual address.
 * @len: Size of the memory window.
 */
struct vca_mw {
	phys_addr_t pa;
	void __iomem *va;
	resource_size_t len;
};

/*
 * These values are supposed to be in the config_change field of the
 * device page when the host sends a config change interrupt to the card.
 */
#define VCA_VIRTIO_PARAM_DEV_REMOVE 0x1

#endif

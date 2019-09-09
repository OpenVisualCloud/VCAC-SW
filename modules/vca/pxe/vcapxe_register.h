/*
 * Intel VCA Software Stack (VCASS)
 *
 * Copyright(c) 2016-2019 Intel Corporation.
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
 * Intel VCA PXE support code
 */

#ifndef _VCAPXE_REGISTER_H_
#define _VCAPXE_REGISTER_H_

struct vcapxe_device;

struct vcapxe_device *vcapxe_register(struct plx_device *xdev, struct device* parent, struct plx_pxe_hw_ops* hw_ops, int card_id, int cpu_id);
void vcapxe_unregister(struct vcapxe_device *pxe);

void vcapxe_go(struct vcapxe_device *pxe);

void vcapxe_finalize(struct vcapxe_device *pxe, void* shared);

void vcapxe_force_detach(struct vcapxe_device *pxe);

int vcapxe_is_ready_to_boot(struct vcapxe_device *pxe);

#endif /* _VCAPXE_REGISTER_H_ */

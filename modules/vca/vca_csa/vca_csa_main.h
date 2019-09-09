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
 * Intel Visual Compute Accelerator Cars State Agent (vca_csa) driver.
 *
 */

#ifndef _VCA_CSA_H_
#define _VCA_CSA_H_

#include <linux/miscdevice.h>
#include "../bus/vca_csa_bus.h"

void vca_csa_sysfs_init(struct vca_csa_device *cdev);

#endif
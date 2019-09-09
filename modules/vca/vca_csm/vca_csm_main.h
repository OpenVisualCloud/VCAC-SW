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
 * Intel VCA card state management driver.
 *
 */
#ifndef _VCA_CSM_H_
#define _VCA_CSM_H_

#include "../bus/vca_csm_bus.h"
/*#include "../scif/scif.h"*/

extern struct bus_type vca_csm_bus;

void vca_csm_sysfs_init(struct vca_csm_device *cdev);
int vca_csm_start(struct vca_csm_device *cdev);
void vca_csm_stop(struct vca_csm_device *cdev, bool force);
int vca_csm_shutdown(struct vca_csm_device *cdev);
void vca_csm_set_state(struct vca_csm_device *cdev, u8 state);
void vca_csm_set_shutdown_status(struct vca_csm_device *cdev, u8 status);
int vca_csm_scif_init(void);
void vca_csm_scif_exit(void);


int vca_mgr_init(struct vca_csm_device *cdev);
void vca_mgr_uninit(struct vca_csm_device *cdev);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 13, 0)
#define _reinit_completion(x) reinit_completion(&x)
#else
#define _reinit_completion(x) INIT_COMPLETION(x)
#endif

#endif

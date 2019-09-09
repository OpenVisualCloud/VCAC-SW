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
 * Intel VCA PXE support code
 *
 */

#ifndef _VCAPXE_IOCTL_H_
#define _VCAPXE_IOCTL_H_

#define VCA_PXE_ENABLE _IO('r', 1)
#define VCA_PXE_DISABLE _IO('r', 2)
#define VCA_PXE_QUERY _IOR('r', 3, __u32)

enum vcapxe_state {
	VCAPXE_STATE_INACTIVE = 0,
	VCAPXE_STATE_ACTIVE = 1,
	VCAPXE_STATE_RUNNING = 2,
};

#endif /* _VCAPXE_IOCTL_H_ */

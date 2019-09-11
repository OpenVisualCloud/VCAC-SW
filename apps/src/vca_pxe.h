/*
* Intel VCA Software Stack (VCASS)
*
* Copyright(c) 2016-2017 Intel Corporation.
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
* Intel VCA User Space Tools.
*/

#ifndef _VCA_PXE_H_
#define _VCA_PXE_H_

#include "vca_defs.h"
#include "linux_osal.h"
#include <string>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <linux/types.h>

#include <vcapxe_ioctl.h>

std::string stringify_pxe_state(enum vcapxe_state state);
enum vcapxe_state get_pxe_state(filehandle_t pxe_dev_fd);

bool is_pxe_exactly_inactive(filehandle_t pxe_dev_fd);
bool is_pxe_exactly_active(filehandle_t pxe_dev_fd);
bool is_pxe_exactly_running(filehandle_t pxe_dev_fd);
bool is_pxe_not_inactive(filehandle_t pxe_dev_fd);

std::string get_pxe_dev_name(int card_id, int cpu_id);

bool pxe_enable(filehandle_t pxe_dev_fd);

bool pxe_disable(filehandle_t pxe_dev_fd);
#endif /* _VCA_PXE_H_ */

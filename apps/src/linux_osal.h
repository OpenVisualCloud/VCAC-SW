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
* Intel VCA User Space Tools.
*/

#ifndef _LINUX_OSAL_
#define _LINUX_OSAL_

#include <stdbool.h>
typedef int filehandle_t;
typedef int Error;

Error close_file(filehandle_t fh);
bool file_create(const char *file_name, int flags);
filehandle_t open_file(const char *name, int flags, int mode);


#endif // _LINUX_OSAL_

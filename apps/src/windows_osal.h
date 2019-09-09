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

#ifndef _WINDOWS_OSAL_
#define _WINDOWS_OSAL_

#include <Windows.h>
#include <io.h>
#include <sys/stat.h>
#include <iostream>

#include "vca_defs.h"
#include "helper_funcs.h"


typedef HANDLE filehandle_t;
typedef DWORD Error;

#define INVALID_FILE_HANDLER	INVALID_HANDLE_VALUE
#define FILE_OPEN_MODE		(_S_IREAD | _S_IWRITE)

#define strncasecmp	_strnicmp
#define sleep(s)	Sleep(1000*s)
#define PATH_MAX	1024

#ifdef __cplusplus
extern "C" {
#endif

Error close_file(filehandle_t fh);

inline filehandle_t open_file(const char *name, int flags, int mode)
{
	filehandle_t fh = CreateFileA(
		name,
		flags,
		0,
		NULL,
		mode,
		FILE_ATTRIBUTE_NORMAL,
		NULL);

	return fh;
}

bool file_create(const char *file_name, int flags);

#ifdef __cplusplus
};
#endif
#endif // _WINDOWS_OSAL_
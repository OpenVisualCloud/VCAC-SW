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

#include "windows_osal.h"
#include <io.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>

#include <vca_defs.h>
#include "helper_funcs.h"


close_on_exit::~close_on_exit() {
	if (INVALID_HANDLE_VALUE != fd)
		CloseHandle(fd);
}


Error close_file(filehandle_t fh)
{
	return CloseHandle(fh) ? 0 : GetLastError();
}

bool file_create(const char *file_name, int flags)
{
	//int rc;
	filehandle_t fd = INVALID_FILE_HANDLER;
	fd = open_file(file_name, flags, OPEN_ALWAYS);
	if (INVALID_FILE_HANDLER == fd)
	{
		if (errno != EEXIST) {
			common_log("Cannot open file %s: %s!\n",
				file_name, strerror(errno));
			return false;
		}
	}
	else {
#pragma message "TODO: Need to change group and mode for Windows"
		/* file created - set file permissions */
		/* rc = change_group_and_mode(file_name);
		if (rc == FAIL) {
			close_file(fd);
			return INVALID_FILE_HANDLER;
		} */
	}

	close_file(fd);
	return true;
}

close_on_exit& close_on_exit::operator =(filehandle_t fd) {
	CloseHandle(fd);
	this->fd=fd;
	return *this;
}

close_on_exit::operator bool() const {
	return fd != INVALID_HANDLE_VALUE;
}

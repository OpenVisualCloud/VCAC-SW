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

#include "linux_osal.h"
#include "helper_funcs.h"

#define FILE_OPEN_MODE		(S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP)


Error close_file(filehandle_t fd) {
	return close(fd) ? errno : 0;
}


close_on_exit::~close_on_exit() {
	if(-1 != fd)
		close(fd);
}


// consider move it to helper_funcs
bool file_create(const char *file_name, int flags)
{
	/* first try to create file - getting error in case it already exists */
	filehandle_t fd = open_file(file_name, O_CREAT | O_EXCL | flags, FILE_OPEN_MODE);
	if (-1 == fd)
	{
		if (errno != EEXIST) {
			common_log("Cannot create file %s: %s!\n",
				file_name, strerror(errno));
			return false;
		}
	}
	else {
		/* file created - set file permissions */
		int rc = change_group_and_mode(file_name);
		if (rc == FAIL) {
			close_file(fd);
			return false;
		}
	}
	close_file(fd);
	return true;
}


close_on_exit::operator bool() const {
	return fd != -1;
}


close_on_exit& close_on_exit::operator =(filehandle_t fd) {
	close_file(fd);
	this->fd=fd;
	return *this;
}


filehandle_t open_file(const char *name, int flags, int mode)
{
	return open(name, flags, mode);
}

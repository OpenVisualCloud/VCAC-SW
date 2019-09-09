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

#ifndef _HELPER_FUNCS_H_
#define _HELPER_FUNCS_H_

#ifdef WIN32
#include "windows_osal.h"
#else
#include "linux_osal.h"
#endif

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <sys/wait.h> // waitpid
#include <sys/resource.h> // getrlimit

#include "vca_defs.h"

#ifdef __cplusplus
#include <string>
#include <iostream>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/filesystem.hpp>
#include <boost/interprocess/sync/named_mutex.hpp>
#include <boost/interprocess/sync/file_lock.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>


namespace Printer {

/* types of verbose level output */
enum e_verbose {
	ALWAYS = 0,
	VERBOSE_DEFAULT,
	DEBUG_INFO,
	FULL_INFO
};

/* global verbose level for LOG macro
 * if verbose <= verbose_level, then LOG macro will print */
extern e_verbose verbose;
} // namespace Printer

#define LOG(verbose_level, format, ...) \
	if (verbose_level <= Printer::verbose) fprintf(stderr, format, ##__VA_ARGS__)

#define LOG_INFO(format, ...)  LOG(Printer::VERBOSE_DEFAULT,format, ##__VA_ARGS__)
#define LOG_DEBUG(format, ...) LOG(Printer::DEBUG_INFO, format, ##__VA_ARGS__)
#define LOG_FULL(format, ...)  LOG(Printer::FULL_INFO, format ,##__VA_ARGS__)

#define LOG_ERROR(format, ...) fprintf(stderr, "ERROR: " format , ##__VA_ARGS__)
#define LOG_WARN(format, ...) fprintf(stderr, "WARNING: " format , ##__VA_ARGS__)

class close_on_exit {
public:
	filehandle_t fd;
	close_on_exit();
	close_on_exit(const close_on_exit&);//= delete;
	/*explicit*/ close_on_exit(filehandle_t fd) : fd(fd){}
	~close_on_exit();
	inline operator filehandle_t() const{
		return fd;
	}
	operator bool() const;
	close_on_exit& operator =(filehandle_t fd);
};

/* exception class to indicate VCA mutex timeout */
class vca_mutex_timeout : public std::runtime_error {
public:
	vca_mutex_timeout(const std::string& mutex_name) :
		std::runtime_error("Mutex timeout reached!\n "
			"If you see this message repeatedly, try to remove file " + mutex_name + "\n") {}
};

std::string int_to_string(int val);
std::string char_to_string(char *c_string);
bool is_forcing_cmd_confirmed();
boost::posix_time::ptime get_time();
unsigned int get_passed_time_ms(const boost::posix_time::ptime start);
boost::posix_time::ptime get_timeout(unsigned int timeout);
int replace_all(std::string &str, const std::string &what, const std::string &with);
std::string get_shm_mutex_path(const std::string &name);
bool could_open_file(const char *file_path);
filehandle_t open_path(const char* path, int flags = O_RDWR);

extern "C" {
#endif

enum unix_ownership_kind {
	USER,
	GROUP
};

#define STRCPY_S(dest, src, size) \
	do { \
		strncpy(dest, src, size-1); \
		(dest)[(size) - 1] = '\0'; \
	} while(0)

#define _STR(s) #s
#define STR(s) _STR(s)

#define MB_TO_B(m) (((size_t)m) * 1024ul * 1024ul)
#define B_TO_MB(b) ((((size_t)b) / 1024ul) / 1024ul)

bool is_correct_parameter(const char *str, long param_min, long param_max);
bool is_ip_address(const char *str);
bool is_hex_digit(char digit);
bool is_unsigned_number(const char *str);
bool is_root();
bool path_exists(const char *path);
bool file_exists(const char *filename);
bool character_device_exists(const char *filename);
size_t get_file_size(const char *filename);
int get_id(enum unix_ownership_kind kind);
int get_vcausers_default_user_id();
int apply_va_min_free_memory();
int get_vcausers_group_id();
int drop_root_privileges();
int change_group_and_mode(const char *file);
int run_cmd(const char *cmdline);
int run_cmd_with_output(const char *cmdline, char *output, unsigned int output_size);
bool vca_named_mutex_create(const char *sem_name);
filehandle_t lock_file(const char *name);

#ifdef __GNUC__
__attribute__((format(printf, 1, 2)))
#endif
void common_log(const char *format, ...);

#ifdef __cplusplus
};
#endif
#endif

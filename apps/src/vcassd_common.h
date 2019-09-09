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

#ifndef _VCASSD_COMMON_H_
#define _VCASSD_COMMON_H_

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <pthread.h>
#include <unistd.h>

#include "vca_defs.h"

#include <vca_common.h>

#ifdef __cplusplus
extern "C" {
#endif

struct thread_info {
	pthread_t thread;
	const char* name;
	bool is_active;
	bool is_reseting;
	volatile bool finish;
};

struct vca_ping_daemon_info {
	struct thread_info daemon_ti;
	char ip_address[IP_MAX_LEN + 1];
};

struct vca_reboot_info {
	struct thread_info reboot_ti;
	char os_image[PATH_MAX + 1];
};

struct vca_console_info {
	struct thread_info console_ti;
	int		virtio_console_fd;
	void		*console_dp;
};

struct vca_net_info {
	struct thread_info net_ti;
	int		virtio_net_fd;
	int		tap_fd;
	void		*net_dp;
};

struct vca_virtblk_info {
	struct thread_info block_ti;
	int		virtio_block_fd;
	void		*block_dp;
	volatile sig_atomic_t	signaled;
	char		*backend_file;
	int		backend;
	void		*backend_addr;
	long		backend_size;
};

struct vca_info {
	struct thread_info init_ti;
	bool init_virtio_dev;
	int card_id;
	int cpu_id;
	char		*name;
	struct vca_console_info	vca_console;
	struct vca_net_info	vca_net;
	struct vca_virtblk_info	vca_virtblk;
	struct vca_ping_daemon_info vca_ping;
	struct vca_reboot_info vca_reboot;
	struct vca_info *prev;
};

enum virtio_types {
	VIRTIO_TYPE_NET = 0,
	VIRTIO_TYPE_CONSOLE,
	VIRTIO_TYPE_BLOCK
};

#ifdef __GNUC__
__attribute__((format(printf, 1, 2)))
#endif
void vcasslog(const char *format, ...);
void interpret_error_code(int rc, const char *message);
enum vca_lbp_states get_vca_state(struct vca_info *vca);
char *readsysfs(const char *dir, const char *entry);
int setsysfs(const char *dir, char *entry, const char *value);

#ifdef __cplusplus
}
#endif
#endif

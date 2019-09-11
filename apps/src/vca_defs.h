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

#ifndef _VCA_DEFS_H_
#define _VCA_DEFS_H_

#include <linux/limits.h>

#define VCA_SAFE_PATH	"PATH=/usr/sbin:/usr/bin:/sbin:/bin"

#define VCASYSFSDIR	"/sys/class/vca"
#define VCA_CONFIG_DIR	"/etc/vca_config.d/"

#ifndef WIN32
#define VCA_CONFIG_PATH  VCA_CONFIG_DIR	"vca_config.xml"
#else
#define VCA_CONFIG_PATH "vca_config.xml"
#endif

#define MSG_FIFO_FILE			"/var/run/vcactld"
#define VCA_REINIT_DEV_CMD		"vca_reinit_dev"
#define VCA_PING_DAEMON_CMD		"ping_daemon"
#define VCA_CONFIG_USE_CMD		"config_use"
#define VCA_PWRBTN_BOOT_CMD		"pwrbtn_boot"

#define BUFFER_SIZE			1024
#define SMALL_OUTPUT_SIZE		128
#define PAGE_SIZE			4096
#define IP_MAX_LEN			15
#define IMG_MAX_SIZE			0x10000000	/* 256 MB */
#define MAX_CPU				3
#define MAX_CARDS			8
#define MODPROBE_TIMEOUT_MS		10000
#define MIN_MEM_FREE_OF_CACHE_HOST_SIDE "524288"
#define TIME_TO_POWER_DOWN_NODE_S	7
#define SGX_DISABLED			0

/* don't need to specify whole path, because there is a default path
 * for named mutexes (/dev/shm) */
#define VCACTL_SHM_PATH			"/dev/shm/sem."
#define VCACTL_CONFIG_NAMED_MTX_NAME	"vcactl_config"
#define VCACTL_CONFIG_NAMED_MTX_TIMEOUT	10

#define VCACTLD_LOCK_PATH		"/var/lock/vca/LCK.vcactld"
#define VCACTL_NODE_LOCK_PATH		"/var/lock/vca/LCK.vcactl_node"

/* Defines for separating blocks of code */
#define VCACTL_PARSING_FUNCTIONS 1

#define ECHO		"echo"
#define PING		"ping"
#define IP		"ip"
#define CAT		"cat"
#define DHCLIENT	"dhclient"
#define MKDIR		"mkdir"

#define MTU_VALUE	"65521"

#define NO_CARD	-1
#define NO_CPU	-1

#ifndef SUCCESS
#define SUCCESS 0
#endif

#ifndef FAIL
#define FAIL	(-1)
#endif

#endif

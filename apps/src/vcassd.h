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
#ifndef _VCASSD_H_
#define _VCASSD_H_

#define LOGFILE_NAME	"/var/log/vca/vcactld.log"

 /* Defines for separating blocks of code */
#define VCACTLD_SIGNAL_HANDLERS_FUNCTIONS 1
#define VCACTLD_THREAD_MANAGE_FUNCTIONS 1
#define VCACTLD_THREAD_FUNCTIONS 1
#define VCACTLD_HELPER_FUNCTIONS 1

 /* signal handling */
static void handle_SIGTERM(int signum);
static void handle_SIGUSR2(int signum);

/* function to manage vca threads */
void try_cancel_vca_thread(struct thread_info *ti, const char *vca_name);
void try_join_vca_thread(struct thread_info *ti, const char *vca_name);
void try_create_vca_thread(struct thread_info *ti,
	void *(*start_routine) (void *), void *arg, const char *vca_name);

/* thread functions */
static void *init_vca(void *arg);
static void *fifo_mgr();
static void *reload_config();
static void *scan_for_devices();

/* other functions */
static struct vca_info *vca_get_device_by_name(const char *name);
static struct vca_info *vca_get_device_by_card_and_cpu(int card_id, int cpu_id);
static void parse_command(char* line);
static bool parse_fifo_msg(char *buf, char *remainder, bool ignore);
static bool load_config();
static void try_auto_boot(struct vca_info *vca);

#endif

/*
 * Intel VCA Software Stack (VCASS)
 *
 * Copyright(c) 2015-2019 Intel Corporation.
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

#include <dirent.h>
#include <sys/poll.h>
#include <sys/eventfd.h>

#include "vcassd_common.h"
#include "vca_watchdog.h"
#include "vca_config_parser.h"
#include "vca_defs.h"
#include "vcassd_virtio_backend.h"
#include "vcactrl.h"

/* forward declarations */
static void *scan_for_devices(void *);
static struct vca_info *vca_get_device_by_name(const char *name);
static bool parse_fifo_msg(char *buf, char *remainder, bool ignore);
static bool load_config();
static void try_auto_boot(struct vca_info *vca);

#define MAX_COMMAND_SIZE PATH_MAX * 2
#define LOGFILE_NAME	"/var/log/vca/vcactld.log"
FILE *logfp;
struct vca_config *config = NULL;
/* to protect config which can be reloaded anytime */
pthread_mutex_t config_lock;
/* to have only one thread scanning */
pthread_mutex_t scan_for_devices_lock;
/* to have only one thread that reloads config */
pthread_mutex_t reload_config_lock;

/* top of devices stack implemented with single-linked list */
static struct vca_info* vca_dev_stack = NULL;

pthread_t msg_fifo_thread = 0;
pthread_t reboot_mgr_thread;

int terminate_msg_fifo_fd = FAIL;
int terminate_reboot_fd = FAIL;
volatile sig_atomic_t terminating = false;

static void handle_SIGTERM(int signum)
{
	vcasslog("Caught signal SIGTERM %d\n", signum);

	if (terminating) {
		vcasslog("Already in course of terminating. Exiting!\n");
		return;
	}
	terminating = true;
}

static void handle_SIGUSR2(int signum)
{
	int err;
	pthread_t scanning_for_devices_thread;
	vcasslog("Caught signal SIGUSR2 %d\n", signum);

	err = pthread_create(&scanning_for_devices_thread, NULL, scan_for_devices, NULL);
	if (err)
		vcasslog("Could not create scanning for devices thread %s\n",
			strerror(err));
}

void try_cancel_vca_thread(struct thread_info * ti, const char *vca_name)
{
	if (ti->is_active)
		ti->finish = true;
}

void try_join_vca_thread(struct thread_info * ti, const char *vca_name)
{
	int err;
	if (ti->is_active) {
		err = pthread_join(ti->thread, NULL);
		if (err)
			vcasslog("%s pthread_join failed on %s thread, %s!\n",
				vca_name, ti->name ? ti->name : "", strerror(err));
		else
			ti->is_active = false;
	}
}

void try_create_vca_thread(struct thread_info * ti,
	void *(*start_routine) (void *), void * arg,
	const char *vca_name)
{
	int err;
	if (ti->is_active) {
		vcasslog("%s error, thread %s is running!\n",
			vca_name, ti->name ? ti->name : "");
		return;
	}
	ti->finish = false;
	err = pthread_create(&ti->thread, NULL, start_routine, arg);
	if (err)
		vcasslog("%s pthread_create failed on %s error %s\n",
			vca_name, ti->name ? ti->name : "", strerror(err));
	else {
		ti->is_active = true;
		ti->is_reseting = false;
	}
}

static void *init_vca(void *arg)
{
	struct vca_info *vca = (struct vca_info *)arg;
	struct sigaction ignore = { 0 };
	ignore.sa_handler = SIG_IGN;

	/* if auto booting is enabled, then boot this vca cpu node */
	try_auto_boot(vca);

	/*
	* Currently, one virtio block device is supported for each VCA card
	* at a time. Any user (or test) can send a SIGUSR1 to the VCA daemon.
	* The signal informs the virtio block backend about a change in the
	* configuration file which specifies the virtio backend file name on
	* the host. Virtio block backend then re-reads the configuration file
	* and switches to the new block device. This signalling mechanism may
	* not be required once multiple virtio block devices are supported by
	* the VCA daemon.
	*/
	sigaction(SIGUSR1, &ignore, NULL);
	bool is_first_try = false;
	do {
		while (vca->init_virtio_dev == false) {
			if (vca->init_ti.finish)
				goto finish;
			if (is_first_try == false && get_vca_state(vca) == VCA_NET_DEV_DOWN) {
				vca->init_virtio_dev = true;
				is_first_try = true;
			}
			sleep(1);
		}

		is_first_try = true;

		if (vca->vca_net.virtio_net_fd != -1) {
			vcasslog("%s close file\n", vca->name);
			close(vca->vca_net.virtio_net_fd);
			vca->vca_net.virtio_net_fd = -1;
		}

		add_virtio_net_device(vca);

		vca->init_virtio_dev = false;

	} while (!vca->init_ti.finish);
finish:
	vca->init_ti.finish = false;
	vca->init_ti.is_active = false;
	return NULL;
}

static void *reboot_mgr(void *)
{
	struct pollfd pfd[2];
	pfd[0].events = POLLIN;
	pfd[1].events = POLLIN;

	pfd[0].fd = eventfd(0, EFD_CLOEXEC);
	terminate_reboot_fd = pfd[0].fd;
	interpret_error_code(pfd[0].fd, "Error eventfd\n");

	pfd[1].fd = open("/proc/vca_os_reboot", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	interpret_error_code(pfd[1].fd, "Cannot open msg_fifo file: /proc/vca_os_reboot! Exiting...\n");

	for (;;) {
		int err = poll(pfd, 2, -1);
		if (0 <= err) {
			if (pfd[0].revents){
				//skip read
				break;
			}
			if (pfd[1].revents){
				struct vca_info *vca;
				for (vca = vca_dev_stack; vca != NULL; vca = vca->prev)
					if (VCA_AFTER_REBOOT == get_vca_state(vca)) {
						char buf[MAX_COMMAND_SIZE];
						snprintf(buf, MAX_COMMAND_SIZE, "vcactl boot %d %d %s",
							vca->card_id, vca->cpu_id, FORCE_LAST_OS_IMAGE);
						vcasslog("run: %s\n", buf);
						err = run_cmd(buf);
						if (err)
							vcasslog("Error %i execute: %s\n", err, buf);
					}
			}
		}
		else if (EINTR != errno) { /* on signal continue */
			vcasslog("Poll error %i in reboot mgr: %s!\n", errno, strerror(errno));
		}
	}
	close(pfd[0].fd);
	close(pfd[1].fd);
	return NULL;
}

static void *fifo_mgr(void *)
{
	char buf[PIPE_BUF];
	char *remainder = (char*)malloc(MAX_COMMAND_SIZE);
	int len;
	struct pollfd pfd[2];
	pfd[0].events = POLLIN;
	pfd[1].events = POLLIN;
	int err;
	bool line_ok = true;

	if (!remainder) {
		vcasslog("ERROR: allocation failure\n");
		return NULL;
	}

	remainder[0] = '\0';

	pfd[0].fd = eventfd(0, EFD_CLOEXEC);
	terminate_msg_fifo_fd = pfd[0].fd;
	interpret_error_code(pfd[0].fd, "Error eventfd\n");

	pfd[1].fd = open(MSG_FIFO_FILE, O_RDWR | O_NONBLOCK);
	interpret_error_code(pfd[1].fd, "Cannot open msg_fifo file: " MSG_FIFO_FILE "! Exiting...\n");
	err = fcntl(pfd[1].fd, F_SETFL, O_NONBLOCK);
	interpret_error_code(err, "File control function error: " MSG_FIFO_FILE "! Exiting...\n");
	for (;;) {
		err = poll(pfd, 2, -1);
		if (0 <= err) {
			if (pfd[0].revents){
				//skip read, terminate daemon
				break;
			}
			if (pfd[1].revents){
				while ((len = read( pfd[1].fd, buf, PIPE_BUF - 1)) > 0) {
					/* for null termination */
					buf[len] = '\0';
					line_ok = parse_fifo_msg(buf, remainder, !line_ok);
				}
			}
		}
		else if (EINTR != errno) { /* on signal continue */
			vcasslog("Poll error %i in fifo mgr: %s!\n", errno, strerror(errno));
			break;
		}
	}
	close(pfd[0].fd);
	close(pfd[1].fd);
	unlink(MSG_FIFO_FILE);
	free(remainder);
	return NULL;
}

static void *reload_config(void *)
{
	struct vca_info *vca;
	int err;

	err = pthread_mutex_lock(&reload_config_lock);
	if (err != 0) {
		vcasslog("pthread_mutex_lock failed in function reload_config()"
			" with error message: %s\n", strerror(err));
		exit(0);
	}

	vcasslog("Trying to restart ping daemons!\n");
	for (vca = vca_dev_stack; vca != NULL; vca = vca->prev) {
		if (vca->vca_ping.daemon_ti.is_active) {
			vca->vca_ping.daemon_ti.is_reseting = true;
			try_cancel_vca_thread(&vca->vca_ping.daemon_ti, vca->name);
			try_join_vca_thread(&vca->vca_ping.daemon_ti, vca->name);
		}
	}

	vcasslog("Killed ping daemon threads!\n");

	if (!load_config())
		exit(0);

	vcasslog("Loaded new config file!\n");

	/* restarting only active threads */
	for (vca = vca_dev_stack; vca != NULL; vca = vca->prev) {
		if (vca->vca_ping.daemon_ti.is_reseting) {
			try_create_vca_thread(&vca->vca_ping.daemon_ti,
				vca_ping_daemon, vca, vca->name);
		}
	}

	vcasslog("Created new ping daemon threads!\n");

	pthread_mutex_unlock(&reload_config_lock);
	return NULL;
}
static void *vca_pwrbtn_boot_node(void *arg)
{
	struct vca_info *vca = (struct vca_info *)arg;
	int rc = SUCCESS;
	char buffer[PATH_MAX + 30]; // additional 30 chars is to hold rest of vcactl boot command

	snprintf(buffer, sizeof(buffer),
		"vcactl wait-BIOS %d %d\n",
		vca->card_id, vca->cpu_id);

	rc = run_cmd(buffer);
	if (vca->vca_reboot.reboot_ti.finish)
		goto forced_finish;
	if (rc == FAIL) {
		vcasslog("Cannot execute: %s\n", buffer);
		goto finish;
	}

	snprintf(buffer, sizeof(buffer),
		"vcactl boot %d %d %s\n",
		vca->card_id, vca->cpu_id, vca->vca_reboot.os_image);

	/* to make sure which os was booted */
	vcasslog("Booted OS image via power button: %s\n", vca->vca_reboot.os_image);

	rc = run_cmd(buffer);
	if (vca->vca_reboot.reboot_ti.finish)
		goto forced_finish;
	if (rc == FAIL)
		vcasslog("Cannot execute: %s\n", buffer);

finish:
	if (rc == SUCCESS)
		vcasslog("Booting thread for card %d and cpu %d has been finished successfully.\n",
			vca->card_id, vca->cpu_id);
	else
		vcasslog("Booting thread for card %d and cpu %d failed.\n",
			vca->card_id, vca->cpu_id);
forced_finish:
	vca->vca_reboot.reboot_ti.finish = false;
	vca->vca_reboot.reboot_ti.is_active = false;
	return NULL;
}

static void *scan_for_devices(void * = nullptr)
{
	struct vca_info *vca;
	struct dirent *file;
	DIR *dp;
	int err;
	int str_len;

	err = pthread_mutex_lock(&scan_for_devices_lock);
	if (err != 0) {
		vcasslog("pthread_mutex_lock failed in function scan_for_devices()"
			" with error message: %s\n", strerror(err));
		exit(0);
	}

	dp = opendir(VCASYSFSDIR);
	if (!dp)
		goto unlock;

	while ((file = readdir(dp)) != NULL) {
		if (strncmp(file->d_name, "vca", 3) ||
			vca_get_device_by_name(file->d_name))
			continue;

		vca = (vca_info *) calloc(1, sizeof(struct vca_info));
		if (!vca) {
			vcasslog("Could not allocate memory for %s\n",
				file->d_name);
			continue;
		}

		vca->card_id = (int)(file->d_name[3] - '0');
		vca->cpu_id = (int)(file->d_name[4] - '0');
		vca->vca_net.virtio_net_fd = -1;
		vca->vca_console.virtio_console_fd = -1;
		vca->vca_virtblk.virtio_block_fd = -1;
		vca->vca_net.net_ti.name = "net";
		vca->vca_console.console_ti.name = "console";
		vca->vca_virtblk.block_ti.name = "block";
		vca->vca_ping.daemon_ti.name = "ping_daemon";
		vca->vca_reboot.reboot_ti.name = "reboot";
		vca->init_ti.name = "init_vca";
		str_len = strlen(file->d_name) + 1;
		vca->name = (char *) malloc(str_len);
		if (vca->name) {
			STRCPY_S(vca->name, file->d_name, str_len);
			vcasslog("VCA name %s card id cpu id %d %d\n",
				vca->name, vca->card_id, vca->cpu_id);
		}
		else {
			vcasslog("Could not allocate memory for %s name\n",
				file->d_name);
			free(vca);
			continue;
		}

		vca->prev = vca_dev_stack;
		vca_dev_stack = vca;

		try_create_vca_thread(&vca->init_ti, init_vca, vca,
			vca->name);
	}

	closedir(dp);
unlock:
	pthread_mutex_unlock(&scan_for_devices_lock);
	return NULL;
}

static void terminate()
{
	int err;
	struct vca_info *vca;
	vcasslog("Trying to terminate gracefully!\n");
	{
		const long long semaphoreStep = 1;
		if (msg_fifo_thread)
			if (8 != write(terminate_msg_fifo_fd, &semaphoreStep, sizeof(semaphoreStep))) //eventfd using 8 byte values
				vcasslog("Invalid size writen into eventfd\n");
		if (8 != write(terminate_reboot_fd, &semaphoreStep, sizeof(semaphoreStep))) //eventfd using 8 byte values
			vcasslog("Invalid size writen into eventfd\n");
	}
	for (vca = vca_dev_stack; vca != NULL; vca = vca->prev) {
		try_cancel_vca_thread(&vca->init_ti, vca->name);
		try_cancel_vca_thread(&vca->vca_ping.daemon_ti, vca->name);
		try_cancel_vca_thread(&vca->vca_net.net_ti, vca->name);
	}
	if (msg_fifo_thread)
	{
		err = pthread_join(msg_fifo_thread, NULL);
		if (err) {
			vcasslog("pthread_join failed on msg_fifo thread, %s!\n",
				strerror(err));
		}
	}
	for (vca = vca_dev_stack; vca != NULL; vca = vca->prev) {
		try_join_vca_thread(&vca->init_ti, vca->name);
		try_join_vca_thread(&vca->vca_ping.daemon_ti, vca->name);
		try_join_vca_thread(&vca->vca_net.net_ti, vca->name);
	}
	vcasslog("Terminated gracefully!\n");
}

void common_log(const char *format, ...) {
	va_list args;
	char buffer[PAGE_SIZE];
	char ts[52], *ts1;
	time_t t;

	if (logfp == NULL)
		return;

	va_start(args, format);
	vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);

	time(&t);
	ts1 = ctime_r(&t, ts);
	ts1[strlen(ts1) - 1] = '\0';
	fprintf(logfp, "%s: %s", ts1, buffer);

	fflush(logfp);
}

static struct vca_info *vca_get_device_by_name(const char *name)
{
	struct vca_info *vca;
	for (vca = vca_dev_stack; vca != NULL; vca = vca->prev) {
		if(strcmp(vca->name, name) == 0)
			return vca;
	}
	return NULL;
}

static struct vca_info *vca_get_device_by_card_and_cpu(int card_id, int cpu_id)
{
	char tmp_buf[64];
	snprintf(tmp_buf, sizeof(tmp_buf), "vca%d%d", card_id, cpu_id);
	return vca_get_device_by_name(tmp_buf);
}

static void parse_command(char* line)
{
	char ip_addr[IP_MAX_LEN + 1] = "";
	char boot_image[PATH_MAX + 1] = "";
	struct vca_info *vca;
	int card_id, cpu_id, switch_on_off;

	vcasslog("Read from msg fifo: %s\n", line);

	if (sscanf(line, VCA_REINIT_DEV_CMD " %d %d\n", &card_id, &cpu_id) == 2) {
		vca = vca_get_device_by_card_and_cpu(card_id, cpu_id);
		if (vca)
			vca->init_virtio_dev = true;
		else
			vcasslog("Invalid vca name: vca%d%d\n", card_id, cpu_id);
	}
	else if (sscanf(line, VCA_PING_DAEMON_CMD " %d %d %d %" STR(IP_MAX_LEN) "s\n", &switch_on_off, &card_id, &cpu_id, ip_addr) >= 3) {
		/* We check if ip address is correct (once again) in case
		* someone added command directly to fifo file. */
		if (ip_addr[0] && !is_ip_address(ip_addr))
			vcasslog("Ip address incorrect!\n");
		else {
			vca = vca_get_device_by_card_and_cpu(card_id, cpu_id);
			if (vca) {
				try_cancel_vca_thread(&vca->vca_ping.daemon_ti, vca->name);
				try_join_vca_thread(&vca->vca_ping.daemon_ti, vca->name);
				if (switch_on_off == 1) {
					if (ip_addr[0]) {
						vcasslog("ip address=%s\n", ip_addr);
						STRCPY_S(vca->vca_ping.ip_address, ip_addr,
							sizeof(vca->vca_ping.ip_address));
					}
					else
						STRCPY_S(vca->vca_ping.ip_address, "",
							sizeof(vca->vca_ping.ip_address));

					try_create_vca_thread(&vca->vca_ping.daemon_ti, vca_ping_daemon, vca, vca->name);
				}
			}
			else
				vcasslog("Invalid vca name: vca%d%d\n", card_id, cpu_id);
		}
	}
	else if (sscanf(line, VCA_PWRBTN_BOOT_CMD " %d %d %" STR(PATH_MAX) "s\n", &card_id, &cpu_id, boot_image) == 3) {
		/* We check if os image really exist (once again) in case
		* someone added command directly to fifo file. */
		char resolved_path[PATH_MAX + 1];

		if (!strcmp(boot_image, BLOCKIO_BOOT_DEV_NAME)) {
			STRCPY_S(resolved_path, BLOCKIO_BOOT_DEV_NAME, sizeof(resolved_path));
		}
		else {
			if (!realpath(boot_image, resolved_path)) {
				vcasslog("Cannot canonicalize OS image path (got %s): %s!\n",
					boot_image, strerror(errno));
				resolved_path[0] = '\0';
			}
			else if (!file_exists(resolved_path)) {
				vcasslog("OS image does not exist or cannot open their path %s!\n", resolved_path);
				resolved_path[0] = '\0';
			}
		}

		if (resolved_path[0]) {
			vca = vca_get_device_by_card_and_cpu(card_id, cpu_id);
			if (vca) {
				if (vca->vca_reboot.reboot_ti.is_active) {
					try_cancel_vca_thread(&vca->vca_reboot.reboot_ti,
						vca->name);
					try_join_vca_thread(&vca->vca_reboot.reboot_ti,
						vca->name);
				}
				STRCPY_S(vca->vca_reboot.os_image, resolved_path,
					sizeof(vca->vca_reboot.os_image));
				try_create_vca_thread(&vca->vca_reboot.reboot_ti,
					vca_pwrbtn_boot_node, vca, vca->name);
			}
			else
				vcasslog("Invalid vca name: vca%d%d\n", card_id, cpu_id);
		}
	}
	else if (!strcmp(line, VCA_CONFIG_USE_CMD)) {
		vcasslog("Caught config-use command\n");
		pthread_t restart_thread;
		int err = pthread_create(&restart_thread, NULL, reload_config, NULL);
		if (err)
			vcasslog("Could not create reload config thread %s\n", strerror(err));
	}
	else {
		vcasslog("vca fifo command not recognized\n");
	}
}

static bool parse_fifo_msg(char *buf, char *remainder, bool ignore) {
	char *start_line = buf;
	char *end_line = strchr(start_line, '\n');

	while (end_line) {
		char* line = start_line;
		bool parse = true;

		/* replace \n with end of string marker */
		*end_line = 0;

		if (ignore) {
			/* we have reached end of excessively long string - ignore this read and
			proceed with next line*/
			vcasslog("parse_fifo_msg: ignoring line\n");
			ignore = false;
			parse = false;
		}
		else if (remainder[0] != '\0') {
			if (strlen(remainder) + strlen(start_line) >= MAX_COMMAND_SIZE) {
				/* string is too long */
				vcasslog("parse_fifo_msg: string too long\n");
				parse = false;
			}
			else {
				strcat(remainder, start_line);
				line = remainder;
			}
		}

		if (parse) {
			parse_command(line);
		}

		/* remainder is either parsed or ignored - forget it */
		remainder[0] = '\0';

		start_line = end_line + 1;
		end_line = strchr(start_line, '\n');
	}

	if (ignore) {
		vcasslog("parse_fifo_msg: ignore too long string\n");
		return false;
	}

	if (strlen(remainder) + strlen(start_line) >= MAX_COMMAND_SIZE) {
		remainder[0] = '\0';
		return false;
	}
	strcat(remainder, start_line);

	return true;
}

static void run_mgrs()
{
	int err;
	vcasslog("Creating FIFO thread.\n");
	err = pthread_create(&msg_fifo_thread, NULL, fifo_mgr, NULL);
	if (err)
		vcasslog("Could not create FIFO thread %s\n", strerror(err));
	vcasslog("Creating reboot thread.\n");
	err = pthread_create(&reboot_mgr_thread, NULL, reboot_mgr, NULL);
	if (err)
		vcasslog("Could not create reboot thread %s\n", strerror(err));
}

static bool load_config()
{
	int err;

	err = pthread_mutex_lock(&config_lock);
	if (err != 0) {
		vcasslog("pthread_mutex_lock failed in function load_config()"
			" with error message: %s\n", strerror(err));
		return false;
	}
	if (config)
		delete_vca_config(config);

	config = new_vca_config(VCA_CONFIG_PATH);
	if (!vca_config_get_config_from_file(config)) {
		vcasslog("ERROR: Could not read config file\n");
		pthread_mutex_unlock(&config_lock);
		return false;
	}

	pthread_mutex_unlock(&config_lock);
	return true;
}

static void try_auto_boot(struct vca_info *vca)
{
	int err;
	err = pthread_mutex_lock(&config_lock);
	if (err != 0) {
		vcasslog("pthread_mutex_lock failed in function try_auto_boot()"
			" with error message: %s\n", strerror(err));
		exit(0);
	}
	bool const autoboot= vca_config_is_auto_boot( config);
	pthread_mutex_unlock(& config_lock);
	if( autoboot) {
		char buffer[BUFFER_SIZE] = "";
		snprintf(buffer, sizeof(buffer),
			"vcactl wait-BIOS %d %d\n",
			vca->card_id, vca->cpu_id);

		if (run_cmd(buffer) == FAIL)
			vcasslog("Cannot execute: %s\n", buffer);

		snprintf(buffer, sizeof(buffer),
			"vcactl boot %d %d\n",
			vca->card_id, vca->cpu_id);

		if (run_cmd(buffer) == FAIL)
			vcasslog("Cannot execute: %s\n", buffer);
	}
}

int main(int argc, char *argv[])
{
	int rc;
	int lock_file_fd;

	/* we ignore SIGUSR2 in case when it occurred before specific handler was registered */
	if (signal(SIGUSR2, SIG_IGN) == SIG_ERR)
		fprintf(stderr, "Can't catch SIGUSR2 with SIG_IGN flag.\n");

	pthread_mutex_init(&config_lock, NULL);
	pthread_mutex_init(&scan_for_devices_lock, NULL);
	pthread_mutex_init(&reload_config_lock, NULL);

	auto grp = get_vcausers_group_id();
	if (grp == -1) {
		vcasslog("Cannot find group 'vcausers'!\n");
		exit(-1);
	}
	logfp = fopen(LOGFILE_NAME, "a+");
	if (!logfp) {
		fprintf(stderr, "cannot open logfile '%s', continuing anyway\n", LOGFILE_NAME);
	}

	rc = chown(LOGFILE_NAME, -1, grp);
	interpret_error_code(rc, "chown(LOGFILE_NAME): %s\n");

	rc = chmod(LOGFILE_NAME, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
	interpret_error_code(rc, "chmod(LOGFILE_NAME): %s\n");


	/* use this lock as semaphore to block potential scan_for_devices()
	 * execution until initialization is done.
	 * unlock is called in fifo_mgr() */
	rc = pthread_mutex_lock(&scan_for_devices_lock);
	if (rc != 0) {
		vcasslog("pthread_mutex_lock failed in function main()"
			" with error message: %s\n", strerror(rc));
		exit(2);
	}

	if (signal(SIGUSR2, handle_SIGUSR2) == SIG_ERR)
		vcasslog("Can't register handler for SIGUSR2");
	if (signal(SIGTERM, handle_SIGTERM) == SIG_ERR)
		vcasslog("Can't register handler for SIGTERM");

	/* create fresh fifo message file */
	unlink(MSG_FIFO_FILE);
	rc = mkfifo(MSG_FIFO_FILE, 0660);
	interpret_error_code(rc, "mkfifo " MSG_FIFO_FILE " error: %s! Exiting...\n");
	rc = chown(MSG_FIFO_FILE, -1, grp);
	interpret_error_code(rc, "chown(MSG_FIFO_FILE): %s\n");
	rc = chmod(MSG_FIFO_FILE, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
	interpret_error_code(rc, "chmod(MSG_FIFO_FILE): %s\n");

	switch (fork()) {
	case 0:
		break;
	case -1:
		vcasslog("fork error\n");
		exit(2);
	default:
		exit(0);
	}

	lock_file_fd = lock_file(VCACTLD_LOCK_PATH);
	if (lock_file_fd < 0) {
		vcasslog("Vcactl deamon process already running!\n");
		exit(EBUSY);
	}

	if (!load_config())
		exit(-1);

	if (vca_config_is_va_min_free_memory_enabled(config)){
		rc = apply_va_min_free_memory();

		if (rc == FAIL){
			vcasslog("Cannot change min cache-free available memory!\n");
			exit(-1);
		}
	}

	/* drop root privileges to make daemon secure */
	rc = drop_root_privileges();
	if (rc == FAIL) {
		vcasslog("Cannot change real and effective user id or group id"
			" of vca daemon process!\n");
		exit(-1);
	}
	vcasslog("Root privileges were dropped,"
		" daemon is now run as user 'vcausers_default'.\n");

	vcasslog("VCA Daemon start\n");

	/* enable communication with daemon using named fifo */
	run_mgrs();

	/* in case daemon was killed and run again */
	scan_for_devices();

	while( !terminating )
		sleep(1);

	terminate();
	if (logfp)
		fclose(logfp);
	close(lock_file_fd);
	return EXIT_SUCCESS;
}

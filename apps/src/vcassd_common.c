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

#include "vcassd_common.h"
#include "helper_funcs.h"

extern FILE *logfp;

void vcasslog(const char *format, ...)
{
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

void interpret_error_code(int rc, const char *message)
{
	if (rc == -1) {
		vcasslog(message, strerror(errno));
		exit( errno?:1);
	}
}

enum vca_lbp_states get_vca_state(struct vca_info *vca)
{
	char *state = NULL;
	enum vca_lbp_states vca_state;

	while (!(state = readsysfs(vca->name, "state"))) {
		sleep(1);
	}

	if (!strcmp(state, get_vca_state_string(VCA_BIOS_DOWN)))
		vca_state = VCA_BIOS_DOWN;
	else if (!strcmp(state, get_vca_state_string(VCA_BIOS_UP)))
		vca_state = VCA_BIOS_UP;
	else if (!strcmp(state, get_vca_state_string(VCA_BIOS_READY)))
		vca_state = VCA_BIOS_READY;
	else if (!strcmp(state, get_vca_state_string(VCA_OS_READY)))
		vca_state = VCA_OS_READY;
	else if (!strcmp(state, get_vca_state_string(VCA_BOOTING)))
		vca_state = VCA_BOOTING;
	else if (!strcmp(state, get_vca_state_string(VCA_NET_DEV_READY)))
		vca_state = VCA_NET_DEV_READY;
	else if (!strcmp(state, get_vca_state_string(VCA_FLASHING)))
		vca_state = VCA_FLASHING;
	else if (!strcmp(state, get_vca_state_string(VCA_RESETTING)))
		vca_state = VCA_RESETTING;
	else if (!strcmp(state, get_vca_state_string(VCA_BUSY)))
		vca_state = VCA_BUSY;
	else if( !strcmp(state, get_vca_state_string(VCA_DONE)))
		vca_state = VCA_DONE;
	else if( !strcmp(state, get_vca_state_string(VCA_ERROR)))
		vca_state = VCA_ERROR;
	else if( !strcmp(state, get_vca_state_string(VCA_NET_DEV_UP)))
		vca_state = VCA_NET_DEV_UP;
	else if( !strcmp(state, get_vca_state_string(VCA_NET_DEV_DOWN)))
		vca_state = VCA_NET_DEV_DOWN;
	else if( !strcmp(state, get_vca_state_string(VCA_NET_DEV_NO_IP)))
		vca_state = VCA_NET_DEV_NO_IP;
	else if( !strcmp(state, get_vca_state_string(VCA_DRV_PROBE_DONE)))
		vca_state = VCA_DRV_PROBE_DONE;
	else if( !strcmp(state, get_vca_state_string(VCA_DRV_PROBE_ERROR)))
		vca_state = VCA_DRV_PROBE_ERROR;
	else if( !strcmp(state, get_vca_state_string(VCA_DHCP_IN_PROGRESS)))
		vca_state = VCA_DHCP_IN_PROGRESS;
	else if( !strcmp(state, get_vca_state_string(VCA_DHCP_DONE)))
		vca_state = VCA_DHCP_DONE;
	else if( !strcmp(state, get_vca_state_string(VCA_DHCP_ERROR)))
		vca_state = VCA_DHCP_ERROR;
	else if( !strcmp(state, get_vca_state_string(VCA_NFS_MOUNT_DONE)))
		vca_state = VCA_NFS_MOUNT_DONE;
	else if( !strcmp(state, get_vca_state_string(VCA_NFS_MOUNT_ERROR)))
		vca_state = VCA_NFS_MOUNT_ERROR;
	else if( !strcmp(state, get_vca_state_string(VCA_AFTER_REBOOT)))
		vca_state = VCA_AFTER_REBOOT;
	else if( !strcmp(state, get_vca_state_string(VCA_OS_REBOOTING)))
		vca_state = VCA_OS_REBOOTING;
	else if (!strcmp(state, get_vca_state_string(VCA_POWER_DOWN)))
		vca_state = VCA_POWER_DOWN;
	else if (!strcmp(state, get_vca_state_string(VCA_POWERING_DOWN)))
		vca_state = VCA_POWERING_DOWN;
	else {
		vcasslog("%s: BUG invalid state %s\n", vca->name, state);
		assert(0);
	}

	free(state);
	return vca_state;
}

char *readsysfs(const char *dir, const char *entry)
{
	char filename[PATH_MAX];
	char value[PAGE_SIZE];
	char *string = NULL;
	int str_len;
	filehandle_t fd;
	int len;

	if (dir == NULL)
		snprintf(filename, PATH_MAX, "%s/%s", VCASYSFSDIR, entry);
	else
		snprintf(filename, PATH_MAX,
			"%s/%s/%s", VCASYSFSDIR, dir, entry);

	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		vcasslog("Failed to open sysfs entry '%s': %s\n",
			filename, strerror(errno));
		return NULL;
	}

	len = read(fd, value, sizeof(value) - 1);
	if (len < 0) {
		vcasslog("Failed to read sysfs entry '%s': %s\n",
			filename, strerror(errno));
		goto readsys_ret;
	}
	if (len == 0)
		goto readsys_ret;

	value[len - 1] = '\0';

	str_len = strlen(value) + 1;
	string = malloc(str_len);
	if (string)
		STRCPY_S(string, value, str_len);

readsys_ret:
	close(fd);
	return string;
}

int setsysfs(const char *dir, char *entry, const char *value)
{
	char filename[PATH_MAX];
	char *oldvalue;
	filehandle_t fd;
	int ret = 0;

	if (dir == NULL)
		snprintf(filename, PATH_MAX, "%s/%s", VCASYSFSDIR, entry);
	else
		snprintf(filename, PATH_MAX, "%s/%s/%s",
			VCASYSFSDIR, dir, entry);

	oldvalue = readsysfs(dir, entry);

	fd = open(filename, O_RDWR);
	if (fd < 0) {
		ret = errno;
		vcasslog("Failed to open sysfs entry '%s': %s\n",
			filename, strerror(errno));
		goto done;
	}

	if (!oldvalue || strcmp(value, oldvalue)) {
		if (write(fd, value, strlen(value)) < 0) {
			ret = errno;
			vcasslog("Failed to write new sysfs entry '%s': %s\n",
				filename, strerror(errno));
		}
	}
	close(fd);
done:
	if (oldvalue)
		free(oldvalue);
	return ret;
}

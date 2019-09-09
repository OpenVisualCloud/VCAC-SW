/*
 * Intel VCA Software Stack (VCASS)
 *
 * Copyright(c) 2017 Intel Corporation.
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
 * Intel VCA Block IO driver.
 *
 */
 
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <linux/limits.h>

#include "../../vcablk_bcknd/vcablk_bcknd_ioctl.h"

static int check_device(int fd_dev)
{
	int err;
	__u32 ver, max;

	err = ioctl(fd_dev, VCA_BLK_GET_VERSION, &ver);
	if (err < 0) {
		printf("Ioctl VCA_BLK_GET_VERSION error %s\n", strerror(errno));
		return err;
	}
	if (ver != VCA_BLK_VERSION) {
		printf("Version not correct, get %u but expected %u\n",
				ver, VCA_BLK_VERSION);
		err = -EINVAL;
		return err;
	}
	printf("Version driver: %u\n", ver);

	err = ioctl(fd_dev, VCA_BLK_GET_DISKS_MAX, &max);
	if (err < 0) {
		printf("Ioctl VCA_BLK_GET_DISKS_MAX error %s\n", strerror(errno));
		return err;
	}

	printf("Max disks: %u\n", max);

	return 0;
}

static int list_device(int fd_dev)
{
	int err;
	__u32 numbers;
	struct vcablk_disk_info_desc info;
	int i;

	err = ioctl(fd_dev, VCA_BLK_GET_DISKS_MAX, &numbers);
	if (err < 0) {
		printf("Ioctl VCA_BLK_GET_DISKS_MAX error %s\n", strerror(errno));
		return err;
	}
	printf("List disks number max: %u\n", numbers);
	printf("[ ID][MODE][STATE][    SIZE    ][SIZE MB] File\n");

	for (i = 0; i < numbers; ++i) {
		info.disk_id = i;
		info.exist = 0;
		err = ioctl(fd_dev, VCA_BLK_GET_DISK_INFO, &info);
		if (err < 0) {
			printf("Ioctl VCA_BLK_GET_DISK_INFO error %s\n", strerror(errno));
			return err;
		}
		if (info.exist) {
			printf("[%3u][%s][%5u][%12llu][%7llu] %s\n", i,
					info.mode == VCABLK_DISK_MODE_READ_ONLY?" RO ":
					info.mode == VCABLK_DISK_MODE_READ_WRITE?" RW ":"UNKN",
					info.state, info.size, info.size>>20,
					info.file_path );
		}
	}
	return 0;
}

off_t fsize(char *file) {
	struct stat st;
	if (stat(file, &st) == 0) {
		return st.st_size;
	}

	return -ENOENT;
}

off_t file_size(char *file, bool ramdisk) {
	long int size;

	if (!ramdisk)
		return fsize(file);

	if (1 != sscanf(file, "%li", &size)) {
		return -EBADF;
	}

	return size * 1024l * 1024l;
}

int disk_open(int fd_dev, int id, char *file_path, bool readonly, bool ramdisk)
{
	int err = 0;
	__u32 numbers;
	struct vcablk_disk_open_desc disk;
	off_t size;
	disk.disk_id = id;
	disk.type = ramdisk?VCABLK_DISK_TYPE_MEMORY:VCABLK_DISK_TYPE_FILE;
	disk.mode = readonly?VCABLK_DISK_MODE_READ_ONLY:VCABLK_DISK_MODE_READ_WRITE;

	err = ioctl(fd_dev, VCA_BLK_GET_DISKS_MAX, &numbers);
	if (err < 0) {
		printf("Ioctl VCA_BLK_GET_DISKS_MAX error %s\n", strerror(errno));
		return err;
	}

	if (id < 0 || id >= numbers) {
		printf("Incorrect disk ID %i out of a range [0-%u]\n", id, numbers -1);
		err = -EINVAL;
		return err;
	}

	size = file_size(file_path, ramdisk);
	if (size <= 0) {
		printf("ERROR: Can't open file %s %s\n", file_path, strerror(errno));
		err = -EBADF;
		return err;
	}
	disk.size = size;

	if (disk.size % 512) {
		printf("Wrong file %s size %llu, not multiple of 512, correct size of file to %llu\n", file_path, disk.size, 512 * (disk.size/512));
		disk.size = 512 * (disk.size/512);
	}

	if (!ramdisk) {
		strncpy(disk.file_path, file_path, PATH_MAX-1);
		disk.file_path[PATH_MAX-1] = '\0';
	} else {
		strcpy(disk.file_path, "RAM DISK");
	}

	printf("Create disk path: %s, size %llu\n", file_path, disk.size);

	err = ioctl(fd_dev, VCA_BLK_OPEN_DISC, &disk);
	if (err < 0) {
		printf("Ioctl error %s\n", strerror(errno));
	} else {
		printf("Create disk successful %s path: %s\n", strerror(errno), file_path);
	}

	return err;
}

int disk_close(int fd_dev, int id)
{
	int err = 0;
	__u32 numbers;
	__u32 id_close = id;

	err = ioctl(fd_dev, VCA_BLK_GET_DISKS_MAX, &numbers);
	if (err < 0) {
		printf("Ioctl VCA_BLK_GET_DISKS_MAX error %s\n", strerror(errno));
		return err;
	}

	if (id < 0 || id >= numbers) {
		printf("Incorrect disk ID %i out of a range [0-%u]\n", id, numbers -1);
		err = -EINVAL;
		return err;
	}
	err = ioctl(fd_dev, VCA_BLK_CLOSE_DISC, &id_close);
	if (err < 0) {
		printf("Ioctl VCA_BLK_CLOSE_DISC error %s\n", strerror(errno));
		return err;
	}

	printf("Close disk %i successful\n", id);


	return err;
}

void help()
{
	printf ("Help: /dev/device list\n");
	printf ("      /dev/device open [id] [ro/rw file_path]/[ramdisk SIZE_MB]\n");
	printf ("      /dev/device close [id]\n");
}

int
main (int argc, char *argv[])
{
	int err = 0;
	printf ("VCA DISKS Backend\n");
	if (argc >= 3) {
		char *device_path = argv[1];
		int fd_dev;
		printf ("Open device: %s ", device_path);
		fd_dev = open(device_path, O_RDWR);
		if (fd_dev < 0) {
			printf ("[FAILED]\n");
		} else {
			printf ("[OK]\n");
			if (!check_device(fd_dev)) {
				char *cmd = argv[2];
				if (!strcmp(cmd, "list")) {
					list_device(fd_dev);
				} else if (!strcmp(cmd, "open")) {
					if (argc == 6) {
						int id;
						char *file_path = argv[5];
						bool readonly;
						bool ramdisk;
						if (1 != sscanf(argv[3], "%i", &id)) {
							printf("Invalid disk ID: %s\n", argv[3]);
							err = -1;
						}
						if (!strcmp(argv[4], "ro")) {
							readonly = true;
							ramdisk = false;
						} else if (!strcmp(argv[4], "rw")) {
							readonly = false;
							ramdisk = false;
						} else if (!strcmp(argv[4], "ramdisk")) {
							readonly = false;
							ramdisk = true;
						} else {
							printf("Unknown open param: %s\n", argv[4]);
							err = -1;
						}
						if (err) {
							help();
						} else {
							err = disk_open(fd_dev, id, file_path, readonly, ramdisk);
							//printf("Press Any Key to Continue\n");
							//getchar();
						}
					} else {
						printf("Unknown open wrong param list: %i expected 3\n", argc - 3);
					}
				} else if (!strcmp(cmd, "close") && argc == 4) {
					int id;
					if (1 != sscanf(argv[3], "%i", &id)) {
						printf("Invalid disk ID: %s\n", argv[3]);
						err = -1;
						help();
					} else {
						err = disk_close(fd_dev, id);
					}
				} else {
					printf("Unknown command: %s\n", cmd);
					help();
				}
			}
			close(fd_dev);
		}
	} else {
		help();
		err = -1;
	}
	return err;
}

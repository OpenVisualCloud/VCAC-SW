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
 * Intel PLX87XX VCA PCIe driver
 */
#include "plx_procfs.h"
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <linux/poll.h>
#include <linux/wait.h>
#include <linux/uaccess.h>

static DECLARE_WAIT_QUEUE_HEAD( queue);
static volatile bool pollin;
static bool singleOpen = false;

static unsigned int poll(struct file *file,poll_table* table)
{
	poll_wait( file, &queue, table);
	if( pollin)
	{
		pollin= 0;
		return POLLIN;

	}
	return 0;
}

static int open(struct inode *inode, struct file *file)
{
	if(!singleOpen)
	{
		singleOpen = true;
		return 0;
	}
	return -EBUSY;
}

static int release(struct inode* i, struct file* f)
{
	singleOpen = false;
	return 0;
}

static const struct file_operations os_reboot=
{
    .owner      = THIS_MODULE,
    .open       = open,
    .poll       = poll,
    .release    = release
};

void plx_reboot_notify()
{
	pollin= POLLIN;
	wake_up( &queue);
}

int __init plx_init_procfs(void)
{
	init_waitqueue_head( &queue);
	if( ! proc_create("vca_os_reboot", 0444, NULL, &os_reboot) )
		return -ENOMEM;
	return 0;
}

void plx_exit_procfs(void)
{
	remove_proc_entry( "vca_os_reboot", NULL);
}

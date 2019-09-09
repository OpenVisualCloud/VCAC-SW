#!/bin/bash
#
# Intel VCA Software Stack (VCASS)
#
# Copyright(c) 2015 Intel Corporation.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License, version 2, as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
# General Public License for more details.
#
# The full GNU General Public License is included in this distribution in
# the file called "COPYING".
#
# Intel VCA Scripts.
#

export PATH=$PATH:/sbin
export PATH=$PATH:/bin

start()
{
	/usr/sbin/vcactld
}

stop()
{
	vcactl os-shutdown || :
	VCACTRLD_PID=`pidof vcactld`
	kill -s SIGTERM $VCACTRLD_PID
}

scan_for_devices()
{
	VCACTRLD_PID=`pidof vcactld`
	if [ -p /var/run/vcactld ] && [ -n "${VCACTRLD_PID}" ]
	then
		logger "[VCA] Daemon ready with pid ${VCACTRLD_PID}"
		kill -s SIGUSR2 $VCACTRLD_PID
	else
		logger "[VCA] Daemon not ready for scan for devices."
	fi
}

vop_reset()
{
	echo vca_reinit_dev ${1:0:1} ${1:1:2}  > /var/run/vcactld
}

case $1 in
	start)
		start
		;;
	stop)
		stop
		;;
	scan)
		scan_for_devices
		;;
	recreate_vop_virtio_device)
		vop_reset $2
		;;
esac

#!/bin/bash
#
# Intel VCA Software Stack (VCASS)
#
# Copyright(c) 2017 Intel Corporation.
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

DEV=/dev/vcablk1
PART=$DEV"p1"


CLEAN=0
if [ $1 == 'clean' ]; then
	CLEAN=1
fi

echo "Clean:     $CLEAN"
echo "DEV:       $DEV"
echo "PART:      $PART"

./test_stop.sh || exit 1


./build.sh

./modules_load.sh
ls /dev/vcab*

[ $CLEAN != 0 ] && rm -fR disk_file

#[ $CLEAN != 0 ] && dd if=/dev/zero of=disk_file bs=1024 count=307200
[ $CLEAN != 0 ] && dd if=/dev/zero of=disk_file bs=2048 count=5072000

./vcablkctrl /dev/vcablk_bcknd_local list
./vcablkctrl /dev/vcablk_bcknd_local open 1 rw ./disk_file
./vcablkctrl /dev/vcablk_bcknd_local list

sleep 2

mkdir mnt

#Create partition
[ $CLEAN != 0 ] && echo "n
p
1





w
" | fdisk $DEV

fdisk -l $DEV
[ $CLEAN != 0 ] && mkfs.ext3 $PART

ls /dev/vcabl*

sleep 2

echo mount $PART ./mnt
mount $PART ./mnt

dd if=/dev/zero of=./mnt/test_file bs=1024 count=30720

df -h











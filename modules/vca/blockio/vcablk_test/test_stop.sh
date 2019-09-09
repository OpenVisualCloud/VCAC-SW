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

echo "Stop start"
umount ./mnt

if [ -f ./vcablkctrl ]; then
./vcablkctrl /dev/vcablk_bcknd_local list
./vcablkctrl /dev/vcablk_bcknd_local close 1
./vcablkctrl /dev/vcablk_bcknd_local list
./vcablkctrl /dev/vcablk_bcknd_local list | grep '\[  1\]\[ RW \]' && exit 1
fi

./modules_unload.sh

echo "Stop done"
exit 0



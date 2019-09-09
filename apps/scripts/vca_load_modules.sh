#!/bin/bash
#
# Intel VCA Software Stack (VCASS)
#
# Copyright(c) 2019 Intel Corporation.
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
MODULES_LIST=("$(find /lib/modules/"$(uname -r)"/extra/vca/ -name '*.ko' ! -name '*vca_csa.ko' -printf '%f\n' | sed 's/\.ko$//')")
MODULES_LIST+=('plx87xx')
FAILED_MODULES_CNT=0

for MODULE in ${MODULES_LIST[*]}
do
	if lsmod | grep -q ^"$MODULE "; then
		echo "Module: $MODULE is already loaded."
	else
		echo -e "Module: $MODULE is not loaded.\nLoading module: $MODULE."
		if modprobe "$MODULE"; then
			echo "Module: $MODULE loaded succesfully."
		else
			((FAILED_MODULES_CNT++))
		fi
	fi
done

[[ $FAILED_MODULES_CNT -eq 0 ]] ||
{
	echo "Number of modules that weren't loaded correctly: $FAILED_MODULES_CNT".
	exit $FAILED_MODULES_CNT
}

#!/bin/bash
#
# Intel VCA Software Stack (VCASS)
#
# Copyright(c) 2015-2017 Intel Corporation.
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

#when cleaning, higher priority of global config file
export VCA_CONFIG='/etc/vca_config'
if [ ! -f $VCA_CONFIG ]; then
        export VCA_CONFIG='vca_config'
fi

source $VCA_CONFIG

echo "Remove auto-generated files"
rm -rf $VCA_ETH_RULE
rm -rf $VCA_MODPROBE_DEP_FILE
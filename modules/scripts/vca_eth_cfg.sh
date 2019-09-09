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

#higher priority of local config file
export VCA_CONFIG_FILE='vca_config'
if [ ! -f $VCA_CONFIG_FILE ]; then
        export VCA_CONFIG_FILE='/etc/vca_config'
fi

export ETH_CONFIG_FILE='/etc/sysconfig/network-scripts/ifcfg-eth0'
export ECHO_CMD='/usr/bin/echo'
export LSPCI_CMD='/usr/sbin/lspci'
export IFUP_CMD='/sbin/ifup'
export GREP_CMD='/usr/bin/grep'
export CAT_CMD='/usr/bin/cat'
export SHELL_CMD='/usr/bin/bash'

export PATH=$PATH:/sbin
export PATH=$PATH:/bin

bash "/sys/class/vca/vca/net_config"

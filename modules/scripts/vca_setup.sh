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
export VCA_CONFIG='vca_config'
if [ ! -f $VCA_CONFIG ]; then
	export VCA_CONFIG='/etc/vca_config'
fi

configure_host()
{
	echo "Adding soft module dependencies"
	if [ $SOFTDEP == 1 ]; then
		echo "softdep plx87xx pre: vca_virtio vca_csm vop vca_mgr vca_mgr_extd plx87xx_dma" > $VCA_MODPROBE_DEP_FILE
	else
		echo "install plx87xx /sbin/modprobe vca_virtio; /sbin/modprobe vop; /sbin/modprobe vca_csm; /sbin/modprobe vca_mgr; /sbin/modprobe vca_mgr_extd; /sbin/modprobe plx87xx_dma; /sbin/modprobe --ignore-install plx87xx" > $VCA_MODPROBE_DEP_FILE
	fi
}

configure_card()
{
	echo "Adding soft module dependencies"

	if [ $SOFTDEP == 1 ]; then
		echo "softdep plx87xx pre: vca_virtio vop vca_csa plx87xx_dma" > $VCA_MODPROBE_DEP_FILE
	else
		echo "install plx87xx /sbin/modprobe vca_virtio; /sbin/modprobe vop; /sbin/modprobe vca_csa; /sbin/modprobe plx87xx_dma; /sbin/modprobe --ignore-install plx87xx" > $VCA_MODPROBE_DEP_FILE
	fi

	echo "Configure VirtIO Ethernet autostart"
	echo "SUBSYSTEM==\"net\", ACTION==\"add\", KERNEL==\"eth*\", RUN+=\"$VCA_ETH_SCRIPT \$name\"" > $VCA_ETH_RULE

	if hash systemctl 2>/dev/null; then
		systemctl enable vca_agent.service
	else
		/sbin/chkconfig --add vca_agent
		/sbin/chkconfig vca_agent on
	fi
}


source $VCA_CONFIG

# Side of the PCIe interface (card or host) based on parameter. Assume host side, if no parameter given

if [ -z "$1" ];then
	echo 'Assuming host side'
	export VCA_SIDE=1
elif [ -n "$1" ] && [ $1 == 'host' ]; then
	echo 'Forcing host side'
	export VCA_SIDE=1
elif [ -n "$1" ] && [ $1 == 'card' ]; then
	echo 'Forcing card side'
	export VCA_SIDE=2
else
	#Unknown side
	export VCA_SIDE=0
fi

# check if given version string indicates library version greater or equal then given
# parameters:
# $1 version string
# $2 tool name
# $3 minimal tool version - major number
# $4 [optional] minimal tool version - minor number
function version_ge()
{
	if [ -z "$4" ]; then
		# single number version (for example 3)
		regexp="^$2.*\s+([0-9]+)"
		[[ $1 =~ $regexp ]]
		major="${BASH_REMATCH[1]}"
		if  [ -n "$major" ]; then
			if [ "$major" -ge "$3" ]; then
				return 0
			fi
		fi
	else
		# composite version (for example 3.11)
		regexp="^$2.*\s+([0-9]+).([0-9]+)"
		[[ $1 =~ $regexp ]]
		major="${BASH_REMATCH[1]}"
		minor="${BASH_REMATCH[2]}"
		if  [ -n "$major" ] && [ -n "$minor" ]; then
			if [ "$major" -gt "$3" ]; then
				return 0
			fi
			if [ "$major" -eq "$3" ] &&  [ "$minor" -ge "$4" ]; then
				return 0
			fi
		fi
	fi
	# not found
	return 1
}


MODPROBE_TOOLS_VERSION=`/sbin/modprobe -V`

# if kmod version is >=2 or module init tools version is >= 3.11 then
# softdeps should be supported
SOFTDEP=0
if version_ge "$MODPROBE_TOOLS_VERSION" "module-init-tools" 3 11; then
	SOFTDEP=1
fi
if version_ge "$MODPROBE_TOOLS_VERSION" "kmod" 2; then
	SOFTDEP=1
fi

if [ $VCA_SIDE == 1 ]; then
	echo 'Configuring host side'
	configure_host
elif [ $VCA_SIDE == 2 ]; then
	echo 'Configuring card side'
	configure_card
else
	echo 'Unknown VCA side; configuration skipped'
	exit -1
fi

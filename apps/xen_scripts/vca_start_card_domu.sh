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
export EXESCRIPT="/usr/lib/vca/domUsetup.sh"
export VCACTRL="vcactl"

parse_parameters () {
	if [ $# == 0 ] || [ $1 == "-h" ] || [ $1 == "--help" ] ; then
		echo "Usage: $0 [OPTIONS] -f <filename> -n <ID>"
		echo $#
		echo "Options: 
-f filename	VM configuration file. Must be located within NFS shared directory
-g		Enable graphics passthrough 
-m MAC		Provide custom interface MAC address for DomU (overrides MAC configured in config file) 
-p path		NFS share location (path at host); using /share if not configured 
-a IP		Card's IP address
-h IP		Host's IP address
-u IP		DomU IP address (for routing purposes)
-N IP		NFS address (if different than host) 
-c ID		Card ID (0, 1, 2, 3); using 0 if not provided
-n ID		Node ID (CPU at card; 0, 1, 2); used to generate DomU MAC address (if not provided with -m option) and and card's/host's address (if not provided by -a/-h) 
-i filename	VM image filename. Must be located within NFS shared directory. Overrides image configured in config file 
-b		Use network bridging instead of routing
-k filename	Kernel image filename. Must be located within NFS shared directory. For booting VMs without bootloader
-r filename	InitRamFs image name. Must be located within NFS shared directory. For booting VMs without bootloader
--multicast	Enable multicast addressing and mrouted daemon
--card-mac	Use card's MAC address as DomU address (with modified first octet to 0xFE)
--legacy-net	Use legacy (emulated) network model (usable when PV drivers not installed yet)

"
		exit 0
fi
	
	while [ "$1" != "" ]
	do
		case "$1" in
			-f)
				export VM_CONFIG_FILE="$2"
				shift
				shift
				;;
			-g)
				export GFX_PASSTHROUGH="YES"
				shift
				;;
			-m)
				export MAC_OVERRIDE="$2"
				shift
				shift
				;;
			-p)	
				export NFS_PATH="$2"
				shift
				shift
				;;
			-N)
				export NFS_ADDR="$2"
				shift
				shift
				;;
			-a)
				export CARD_ADDR="$2"
				shift
				shift
				;;
			-h)
				export HOST_ADDR="$2"
				shift
				shift
				;;
			-N)
				export NFS_ADDR="$2"
				shift
				shift
				;;
			-c)	
				export CARD_ID="$2"
				shift
				shift
				;;
			-n)	
				export NODE_ID="$2"
				shift
				shift
				;;
			-i)
				export IMAGE_NAME=""
				shift
				shift
				;;
			-b)
				export USE_BRIDGING="Y"
				export USE_ROUTING="N"
				shift
				;;
			-r)
				export INITRAMFS_FILE="$2"
				shift
				shift
				;;
			-k)
				export KERNEL_FILE="$2"
				shift
				shift
				;;
			--multicast)
				export USE_MULTICAST="Y"
				shift
				;;
			--card-mac)
				export USE_CARD_MAC="Y"
				shift
				;;
			--legacy-net)
				export USE_LEGACY_NET=""
				shift
				;;
			*)
				echo "unknown parameter '$1'"
				exit 1
				;;	

		esac
	done
}

export CARD_ADDR=""
export HOST_ADDR=""
export NODE_ID=""
export CARD_ID=""

parse_parameters "$@"

if [ "$CARD_ID" == "" ]; then
	export CARD_ID=0
fi

if [ "$CARD_ADDR" == "" ] || [ "$HOST_ADDR" == "" ] && [ "$NODE_ID" == "" ]; then
	echo "Provide both, card and host IP addresses, or node (and optionaly card) ID"
	exit 1
fi

if [ "$CARD_ADDR" == "" ]; then
	export CARD_ADDR=`$VCACTRL config-show $CARD_ID $NODE_ID | egrep '\sip:' | cut -d' ' -f4-`
fi

if [ "$CARD_ADDR" == "dhcp" ]; then
	CARD_ADDR=`$VCACTRL network ip $CARD_ID $NODE_ID`
	if [ "$CARD_ADDR" == "" ] || [[ $(echo $CARD_ADDR |grep "ip not found") ]] ; then
		echo "Cannot read node's IP"
		exit 1
	else
		echo "Card's IP: $CARD_ADDR"
	fi
fi


if [ "$HOST_ADDR" == "" ]; then
	BRIDGE=`$VCACTRL config-show $CARD_ID $NODE_ID | egrep "bridge-interface" | cut -d' ' -f4-`
	if [ "$BRIDGE" == "" ]; then
		export HOST_ADDR=`$VCACTRL config-show $CARD_ID $NODE_ID | egrep '\shost-ip:' | cut -d' ' -f4-`
	else
		export HOST_ADDR=`ifconfig $BRIDGE |grep "inet " | cut -d' ' -f10`
	fi

	if [ "$BRIDGE" != "" ] && [ "$USE_BRIDGING" != "Y" ]; then
		echo "Card interface is specified to be a bridge, missing expected -b option"
		exit 1
	fi
fi



tar cf - $EXESCRIPT | ssh root@$CARD_ADDR 'export D=`mktemp -d`; tar xf - -C $D; chmod +x $D/'$EXESCRIPT'; $D/'$EXESCRIPT -a $CARD_ADDR -h $HOST_ADDR $@

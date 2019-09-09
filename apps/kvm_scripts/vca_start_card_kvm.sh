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
export EXESCRIPTDIR="/usr/lib/vca" # to avoid leading '/' in tar archive
export EXESCRIPT="kvmsetup.sh"
export VCACTRL="vcactl"
export NFS_PATH="/share"
export NFS_MOUNT="/mnt"
export DOM0_RAM=""

# Parse parameters passed to the script. All parameters used by kvmsetup.sh must
# be defined here, even if are not used by this script
parse_parameters () {
	if [ $# == 0 ] || [ $1 == "-h" ] || [ $1 == "--help" ] ; then
		echo "Usage: $0 [OPTIONS] -f <filename> -n <ID>"
		echo ""
		# Keep parameters alphabetically sorted in help and in the 'case' switch which parses them.
		# Prefer single-character options, i.e. do not introduce long options without an apparent reason.
		echo "Options:
-a      <IP>		Card's IP address
-b			Use network bridging instead of routing
-c      <ID>		Card ID (0, 1, 2, 3); using 0 if not provided
--card-mac		Use card's MAC address as DomU address (with modified first octet to 0xFE)
--cdrom <filename>	Cdrom image filename. Must be located within NFS shared directory.
-d		        Disable graphics passthrough
-g		        Enable graphics passthrough
-h      <IP>		Host's IP address
-i      <filename>	VM image filename. Must be located within NFS shared directory.
--install <filename>	Bootable cdrom image to start OS installation.  Must be located within NFS shared directory.
--kvm_vnc_port <PORT>	Create iptables rule, to expose KVM's host VNC port at a given Host port
-n      <ID>		Node ID (CPU at card; 0, 1, 2); used to generate DomU MAC address (if not provided with -m option) and and card's/host's address (if not provided by -a/-h)
-N      <IP>		NFS address (if different than host)
-p      <path>		NFS share location (path at host); using $NFS_PATH if not configured
-s      <size>		RAM memory size reserved for KVM/Dom0, in MBs. Remaining RAM will be left for use by the VM.
-u      <IP>		DomU IP address (for routing purposes)
--vm_rdp_port <PORT>	Create iptables rule, to expose KVM's target Windows Remote Desktop port at a given Host port
--vm_vnc_port <PORT>	Create iptables rule, to expose KVM's target VNC port at a given Host port
"
		exit 0
	fi

	while [ "$1" != "" ]
	do
		case "$1" in
			-a)
				export CARD_ADDR="$2"
				shift
				shift
				;;
			-b)
				export USE_BRIDGING="Y"
				export USE_ROUTING="N"
				shift
				;;
			--boot-uefi)
				export BOOT_UEFI="--boot-uefi"	# only passing this parameter further to the ${EXESCRIPT}
				shift
				;;
			-c)
				export CARD_ID="$2"
				shift
				shift
				;;
			--card-mac)
				export USE_CARD_MAC="Y"
				shift
				;;
			--cdrom)
				export CDROM_IMAGE="$2"
				shift
				shift
				;;
			-i)
				export IMAGE_NAME=""
				shift
				shift
				;;
			--install)
				export INSTALL_IMAGE="$2"
				shift
				shift
				;;
			-g)
				export GFX_PASSTHROUGH="YES"
				shift
				;;
			-h)
				export HOST_ADDR="$2"
				shift
				shift
				;;
			--kvmgt)
				export KVMGT="--kvmgt"
                                shift
                                ;;
			--kvm_vnc_port)
				export KVM_VNC_PORT="$2"
				shift
				shift
				;;
			-M)
				export NFS_MOUNT="$2"
                                shift
                                shift
                                ;;
			-m)
				export MAC_OVERRIDE="$2"
				shift
				shift
				;;
			-N)
				export NFS_ADDR="$2"
				shift
				shift
				;;
			-n)
				export NODE_ID="$2"
				shift
				shift
				;;
			-p)
				export NFS_PATH="$2"
				shift
				shift
				;;
			-s)
				export DOM0_RAM="-s $2"	# only passing this parameter further to the ${EXESCRIPT}
				shift
				shift
				;;
			-u)
				export DOMU_ADDR="$2"
				shift
				shift
				;;
			--vm_rdp_port)
				export VM_RDP_PORT="$2"
				shift
				shift
				;;
			--vm_vnc_port)
				export VM_VNC_PORT="$2"
				shift
				shift
				;;
			*)
				echo "unknown parameter '$1'"
				exit 1
				;;

		esac
	done
}


clear_nat_rules () {
	for LINE in $(iptables -L PREROUTING -t nat -n --line-numbers |grep $1 |cut -d" " -f1|tac)
	do
		iptables -t nat -D PREROUTING $LINE
	done
}

clear_forward_rules () {
	for LINE in $(iptables -L FORWARD -n --line-numbers |grep $1 |cut -d" " -f1|tac)
	do
		iptables -D FORWARD $LINE
	done
}

enable_port_forwarding () {
	if [ "$KVM_VNC_PORT" != "" ] || [ "$VM_VNC_PORT" != "" ] || [ "$VM_RDP_PORT" != "" ]; then
		echo 1 > /proc/sys/net/ipv4/ip_forward
		clear_forward_rules $CARD_ADDR
		clear_forward_rules $DOMU_ADDR
	fi

	if [ "$KVM_VNC_PORT" != "" ]; then
		clear_nat_rules $CARD_ADDR:5900
		iptables -t nat -I PREROUTING -p tcp --dport $KVM_VNC_PORT -j DNAT --to-destination $CARD_ADDR:5900
		iptables -I FORWARD -p tcp --dport 5900 -d $CARD_ADDR -j ACCEPT
	fi

	if [ "$VM_VNC_PORT" != "" ]; then
		clear_nat_rules DOMU_ADDR:5900
		iptables -t nat -I PREROUTING -p tcp --dport $VM_VNC_PORT  -j DNAT --to-destination $DOMU_ADDR:5900
		iptables -I FORWARD -p tcp --dport 5900 -d $DOMU_ADDR -j ACCEPT
	fi

	if [ "$VM_RDP_PORT" != "" ]; then
		clear_nat_rules DOMU_ADDR:3389
		iptables -t nat -I PREROUTING -p tcp --dport $VM_RDP_PORT -j DNAT --to-destination $DOMU_ADDR:3389
		iptables -I FORWARD -p tcp --dport 3389 -d $DOMU_ADDR -j ACCEPT
	fi

	#iptables -I FORWARD -j ACCEPT
}

export CARD_ADDR=""
export HOST_ADDR=""
export NODE_ID=""
export CARD_ID=""
export BOOT_UEFI=""
export KVMGT=""

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

if [ "$DOMU_ADDR" == "" ]; then
	export DOMU_ADDR=`echo $CARD_ADDR |cut -d'.' -f1,2,3`.2
fi

# Send script to card. Pass all parameters passed to this script and extend it with some calculated overrides
(cd ${EXESCRIPTDIR} ; tar cf - $EXESCRIPT ) | ssh root@$CARD_ADDR 'export D=`mktemp -d`; \
								tar xf - -C $D; \
								chmod +x $D/'$EXESCRIPT'; \
								$D/'$EXESCRIPT -a $CARD_ADDR -h $HOST_ADDR -u $DOMU_ADDR -p $NFS_PATH -M $NFS_MOUNT $@ $DOM0_RAM $BOOT_UEFI $KVMGT
if [ "$BRIDGE" == "" ]; then
	# Add port forwarding on host (only if bridge isn't used)
	enable_port_forwarding
fi

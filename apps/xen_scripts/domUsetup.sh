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

export ETH_IF="eth0"
export BRIDGE_IF="xenbr0"
export ROUTE_IF="xenif0"
export NFS_MOUNT="/mnt"
export MAC_PREFIX="c0:ff:ee"
export NFS_PATH="/share"
export MASK="24"
export IP_PREFIX="172.31"
export DEF_H_IP="254"
export DEF_C_IP="1"
export DEF_U_IP="2"
export DEF_C_IP_MCAST="129"
export DEF_U_LINK_IP_MCAST="1"
export ROUTING_MTU="65262"
export BRIDGING_MTU="65232"
export CARD_MAC_PREFIX="7e"
export MAX_NFS_RETRIES="5"
export ETH_TYPE="type=vif"

parse_parameters () {
	if [ $# == 0 ] || [ $1 == "-h" ] || [ $1 == "--help" ] ; then
		echo "Usage: $0 [OPTIONS] -f filename "
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
			-u)
				export DOMU_ADDR="$2"
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
				export VM_IMAGE_NAME="$2"
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
				export ETH_TYPE="model=e1000"
				shift
				;;
			*)
				echo "unknown parameter '$1'"
				exit 1
				;;	
		esac
	done
}

start_xen () {
	#Start Xen
	/etc/init.d/xencommons start
	xenstore-write "/local/domain/0/domid" 0
}

wait_for_ip () {
	IP_ADDR=""
	TIMEOUT=20
	while [ "$IP_ADDR" == "" ] && [ $TIMEOUT -gt 0 ]; do
		sleep 1
		TIMEOUT=$((TIMEOUT - 1))
		IP_ADDR=`ip addr show dev $1 |grep -e "inet " | cut -f6 -d' '`
	done

	if [ "$IP_ADDR" == "" ]; then
		echo "Couldn't obtain IP address from DHCP"
	fi
}

start_bridge () {
#This part configures bridge connection and shall be removed, if initial network config already does it
	export CHECK_IF=`ip a show dev $BRIDGE_IF`
	if [ "$CHECK_IF" == "" ]; then
		echo "Adding bridge $BRIDGE_IF"
		brctl addbr $BRIDGE_IF
	else
		return
	fi
	brctl addif $BRIDGE_IF $ETH_IF
	export CHECK_IF=`ip a show dev $BRIDGE_IF |grep "state UP"`
	if [ "$CHECK_IF" == "" ]; then
		#save default gateway, to recreate it after bringing up bridge
		GATEWAY=`ip route |grep default |cut -d' ' -f3`
		echo "Bringing up $BRIDGE_IF"
		ip link set dev $BRIDGE_IF up
		sleep 2

		IP_ADDR=`ip addr show dev $ETH_IF |grep -e "inet " | cut -f6 -d' '`
		if [ -e /var/lib/dhclient/dhclient.leases ]; then
			dhclient -r
			ip addr flush $ETH_IF
			ifconfig
			dhclient $BRIDGE_IF
			wait_for_ip $BRIDGE_IF
		elif [ "$IP_ADDR" != "" ]; then
			ip addr del $IP_ADDR dev $ETH_IF
			ip addr add $IP_ADDR dev $BRIDGE_IF
			if [ "$GATEWAY" != "" ];then
				ip route add $GATEWAY dev $BRIDGE_IF
				ip route add default via $GATEWAY
			fi
		fi
	fi
}

mount_nfs () {
#Mount nfs resources, if not mounted yet
	export CHECK_NFS=`mount |grep $NFS_ADDR:$NFS_PATH`
	if [ "$CHECK_NFS" == "" ]; then
		export CHECK_NFS=`mount |grep $NFS_MOUNT`
		if [ "$CHECK_NFS" != "" ]; then
			umount $NFS_MOUNT
		fi

		# mount desired nfs share at defined mountpoint
		i=0
		until [ $i -ge  $MAX_NFS_RETRIES ]
		do
			mount -t nfs $NFS_ADDR:$NFS_PATH $NFS_MOUNT && break
			i=$[$i+1]
			sleep 2
		done
		if [ $i -ge  $MAX_NFS_RETRIES ]
		then
			echo "$MAX_NFS_RETRIES unsuccessful attempts to mount nfs, aborting"
			exit 1
		fi
	fi
	echo "nfs: $NFS_ADDR:$NFS_PATH"
}

start_vm () {
	#create VM additional options
	export VM_OPTIONS=
	#create mac
	if [ "$NODE_ID" != "" ]; then
		export VM_MAC="$MAC_PREFIX:00:0$(($CARD_ID+1)):0$(($NODE_ID+1))"
	fi
	#check mac override
	if [ "$MAC_OVERRIDE" != "" ]; then
		export VM_MAC="$MAC_OVERRIDE"
	fi
	if [ "$VM_MAC" != "" ]; then
		if [ "$USE_BRIDGING" == "Y" ]; then
			export VM_OPTIONS=$VM_OPTIONS" vif = [ \"mac=$VM_MAC,bridge=$BRIDGE_IF, vifname=$ROUTE_IF, $ETH_TYPE\" ];"
		elif [ "$USE_ROUTING" == "Y" ]; then
			export VM_OPTIONS=$VM_OPTIONS" vif = [ \"mac=$VM_MAC,script=vif-route, vifname=$ROUTE_IF, ip=$DOMU_LINK_ADDR, $ETH_TYPE\" ];"
		fi
		echo $VM_MAC > /etc/vca_vm_mac
	fi

	if [ "$VM_IMAGE_NAME" != "" ]; then
		export VM_OPTIONS=$VM_OPTIONS" disk = [ \"file:$NFS_MOUNT/$VM_IMAGE_NAME,xvda,w\"];"
	fi

	if [ "$INITRAMFS_FILE" != "" ]; then
		export VM_OPTIONS=$VM_OPTIONS" ramdisk = \"$NFS_MOUNT/$INITRAMFS_FILE\";"
	fi
	
	if [ "$KERNEL_FILE" != "" ]; then
		export VM_OPTIONS=$VM_OPTIONS" kernel = \"$NFS_MOUNT/$KERNEL_FILE\";"
	fi

	#Start VM
	echo "Starting vm as xl create $NFS_MOUNT/$VM_CONFIG_FILE '$VM_OPTIONS'"
	xl create $NFS_MOUNT/$VM_CONFIG_FILE "$VM_OPTIONS"
	xl list

}

network_addressing () {
	if [ "$NODE_ID" != "" ]; then
		export NODE_IP=$(($(($CARD_ID))*3+$(($NODE_ID))+1))
		echo "Calculated node index $NODE_IP"
		if [ "$HOST_ADDR" == "" ]; then
			export HOST_ADDR="$IP_PREFIX.$NODE_IP.$DEF_H_IP"
			echo "Calculated host address $HOST_ADDR"
		fi
		if [ "$CARD_ADDR" == "" ]; then
			export CARD_ADDR="$IP_PREFIX.$NODE_IP.$DEF_C_IP"
			echo "Calculated card address $CARD_ADDR"
		fi
		if [ "$DOMU_ADDR" == "" ]; then
			export DOMU_ADDR="$IP_PREFIX.$NODE_IP.$DEF_U_IP"
			echo "Calculated DomU address $DOMU_ADDR"
		fi
		
		if [ "$USE_MULTICAST" == "Y" ]; then
			export DOMU_LINK_ADDR="$IP_PREFIX.$NODE_IP.$DEF_U_LINK_IP_MCAST"
		else
			export DOMU_LINK_ADDR=$DOMU_ADDR
		fi

		if [ "$USE_ROUTING" == "Y" ]; then
			echo "Calculated DomU link address $DOMU_LINK_ADDR"
		fi
	fi

	if [ "$NFS_ADDR" == "" ]; then
		export NFS_ADDR=$HOST_ADDR
	fi
	
}

configure_gfx () {
	xl pci-assignable-add 00:02.0
}

# Count the smallest network mask, containing two network addresses
count_network_mask () {
	ADDR1=$1
	ADDR2=$2
	
	ADDR1_SUM=0
	ADDR2_SUM=0
	
	BYTE=1
	while [ $BYTE -lt 5 ]; do
		PART=`echo $ADDR1|cut -f$BYTE -d'.'`
		ADDR1_SUM=`echo "$ADDR1_SUM*256+$PART"|bc`
		PART=`echo $ADDR2|cut -f$BYTE -d'.'`
		ADDR2_SUM=`echo "$ADDR2_SUM*256+$PART"|bc`
		let BYTE=$BYTE+1
	done
	
	COMMON=$(($ADDR1_SUM^$ADDR2_SUM))
	if (( $COMMON > 0 )); then
		EXP=`echo "l($COMMON)/l(2)"|bc -l`
		EXP=`echo "$EXP/1"|bc`
	else
		EXP=0
	fi
	
	MASK_LEN=$((32-$EXP))
	(( $MASK_LEN < 30)) || MASK_LEN=30
}

use_card_mac () {
	#get card's mac and replace first octet with "fe"
	CARD_MAC=`ip link show $ETH_IF | grep "link/ether" | cut -f6 -d' '`
	MAC_TAIL=`echo $CARD_MAC |cut -f2- -d':'`
	MAC_OVERRIDE="$CARD_MAC_PREFIX:$MAC_TAIL"
	echo "DomU MAC address: $MAC_OVERRIDE"
	DOMU_MDNS_NAME="vca_"`echo $MAC_OVERRIDE |tr -d :`".local"
}

export VM_IMAGE_NAME=""
export CARD_ADDR=""
export HOST_ADDR=""
export DOMU_ADDR=""
export VM_CONFIG_FILE=""
export CARD_ID="0"
export NODE_ID=""
export GFX_PASSTHROUGH=""
export USE_BRIDGING="N"
export USE_ROUTING="Y"
export INITRAMFS_FILE=""
export KERNEL_FILE=""

parse_parameters "$@"
if [ "$VM_CONFIG_FILE" == "" ]; then
	echo "VM config file not provided"
	exit 1
fi

if [ "$USE_MULTICAST" == "Y" ]; then
	DEF_C_IP=$DEF_C_IP_MCAST
fi

if [ "$USE_CARD_MAC" == "Y" ]; then
	use_card_mac
fi

network_addressing

if [ "$CARD_ADDR" == "" ]; then
	echo "Unknown card address. Please provide address directly, or provide card and node number to use default ones"
	exit 1
fi
if [ "$NFS_ADDR" == "" ]; then
	echo "Unknown NFS address. Please provide address directly, provide host address or card and node number to calculate default ones"
	exit 1
fi

#start_xen
if [ "$GFX_PASSTHROUGH" == "YES" ]; then
	configure_gfx
fi

if [ "$USE_BRIDGING" == "Y" ]; then
	start_bridge
fi
mount_nfs
start_vm
echo "VM started"

if [ "$USE_ROUTING" == "Y" ]; then
	echo "Enabling routing"
	ifconfig $ROUTE_IF mtu $ROUTING_MTU

	if [ "$USE_MULTICAST" == "Y" ]; then
		count_network_mask $DOMU_LINK_ADDR $DOMU_ADDR
		ifconfig $ROUTE_IF $DOMU_LINK_ADDR/$MASK_LEN
	fi

	ifconfig $ROUTE_IF
	service firewalld stop
	echo 1 > /proc/sys/net/ipv4/ip_forward
	echo 1 > /proc/sys/net/ipv4/conf/eth0/proxy_arp
elif [ "$USE_BRIDGING" == "Y" ]; then
	ifconfig $ROUTE_IF mtu $BRIDGING_MTU
fi

if [ "$DOMU_MDNS_NAME" != "" ]; then
	echo "If you don't know an IP of DomU, because it is assigned by DHCP, install Avahi daemon and nss-mdns at host and connect to:"
	echo -e "\t$DOMU_MDNS_NAME"
fi


if [ "$USE_MULTICAST" == "Y" ]; then
	killall mrouted
	mrouted
fi

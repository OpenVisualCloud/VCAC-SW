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
export BRIDGE_IF="kvmbr0"
export ROUTE_IF="kvmif0"
export ROUTE_IF_NIC="$ROUTE_IF-nic"
export KVM_IF="vnet0"
export NFS_MOUNT="/mnt"
export MAC_PREFIX="c0:ff:ee"
export NFS_PATH="/share"
export MASK="24"
export IP_PREFIX="172.31"
export DEF_H_IP="254"
export DEF_C_IP="1"
export DEF_U_IP="2"
export ROUTING_MTU="65500"
export NETWORK_CFG_XML="/etc/libvirt/qemu/networks/vca_net.xml"
export NET_CFG="vca_net"
export VM_NAME="vca_vm"
export CARD_MAC_PREFIX="7e"
export MAX_NFS_RETRIES="5"
export DOM0_RAM_MB=6656			# RAM (in MB) reserved for KVM/Dom0 OS.

# KVMGT params
export QEMU_PATH="/usr/bin/qemu-system-x86_64"
export MEMORY="3072"
export SMP="2"
export VM_NAME="kvmgt_vm"
export NET_SCRIPT_PATH="/etc/"
export BRIDGE="vcabr0"

# parse parameters passed to the script and store them in related variables
# exit with error, if unsupported parameter passed
parse_parameters () {
	if [ $# == 0 ] || [ $1 == "-h" ] || [ $1 == "--help" ] ; then
		echo "Usage: $0 [OPTIONS] -f <filename>"
		echo ""
		# Keep parameters alphabetically sorted in help and in the 'case' switch which parses them.
		# Prefer single-character options, i.e. do not introduce long options without an apparent reason.
		echo "Options:
-a <IP>		Card's IP address
-b		Use network bridging instead of routing
-c <ID>		Card ID (0, 1, 2, 3); using 0 if not provided
--card-mac	Use card's MAC address as DomU address (with modified first octet to 0xFE)
-f <filename>	VM configuration file. Must be located within NFS shared directory
-g		Enable graphics passthrough
-h <IP>		Host's IP address
-i <filename>	VM image filename. Must be located within NFS shared directory. Overrides image configured in config file
--install <filename>	Bootable cdrom image to start OS installation.  Must be located within NFS shared directory.
-m <MAC>	Provide custom interface MAC address for DomU (overrides MAC configured in config file)
-n <ID>		Node ID (CPU at card; 0, 1, 2); used to generate DomU MAC address (if not provided with -m option) and and card's/host's address (if not provided by -a/-h)
-N <IP>		NFS address (if different than host)
-p <path>	NFS share location (path at host); using $NFS_PATH if not configured
-s <size>	RAM memory size reserved for KVM/Dom0, in MBs. Defaults to ${DOM0_RAM_MB} MB. Remaining RAM will be left for use by the VM.
-u <IP>		DomU IP address (for routing purposes)
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
				export BOOT_UEFI="--boot uefi"
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
			-f)
				export VM_CONFIG_FILE="$2"
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
			-i)
				export IMAGE_NAME="$2"
				shift
				shift
				;;
			--install)
				export INSTALL_IMAGE="$2"
				shift
				shift
				;;
			-k)
				export KERNEL_FILE="$2"
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
			-r)
				export INITRAMFS_FILE="$2"
				shift
				shift
				;;
			-s)
				export DOM0_RAM_MB="$2"
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

# start libvirtd
# if scripts configuring network already exists in KVM, destroy it
# create new network config using xml file with calculated addresses
# if VM already exists, undefine it, so new VM with the same name can be created
start_kvm () {
	#Start KVM
	if hash systemctl 2>/dev/null; then
		systemctl start libvirtd
	else
		service libvirtd start
	fi
	if [[ $(virsh net-list |grep $NET_CFG) != "" ]]; then
		virsh net-destroy $NET_CFG
	fi
	if [ "$USE_BRIDGING" == "N" ]; then
		virsh net-create $NETWORK_CFG_XML
	fi
	if [[ $(virsh list --all |grep $VM_NAME) != "" ]]; then
		virsh undefine $VM_NAME
	fi
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

# Check if file provided as a parameter exists on nfs shared resource
# Exit the script with error if the file doesn't exist or is unreadable
check_nfs_file () {
	if [ ! -f "$NFS_MOUNT/$1" ]; then
		echo "Cannot find $NFS_PATH/$1 on nfs shared resource"
		exit 1
	fi
	if [ ! -r "$NFS_MOUNT/$1" ]; then
		echo "NFS shared file $NFS_PATH/$1 doesn't have read permission"
		exit 1
	fi
}


# Mount nfs resources, if not mounted yet
mount_nfs () {
	# check if desired nfs share is already mounted
	export CHECK_NFS=`mount |grep $NFS_ADDR:$NFS_PATH`
	if [ "$CHECK_NFS" == "" ]; then
		# check if anything other is mounted at defined mountpoint; unmount it
		export CHECK_NFS=`mount |grep $NFS_MOUNT`
		if [ "$CHECK_NFS" != "" ]; then
			umount $NFS_MOUNT
		fi

		# mount desired nfs share at defined mountpoint
		i=0
		until [ $i -ge  $MAX_NFS_RETRIES ]
		do
			mkdir -p $NFS_MOUNT
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

# Create VM starting command line and invoke it
start_vm () {
	# create VM basic command line
	export VM_OPTIONS=" --hvm --connect qemu:///system -n $VM_NAME --noautoconsole --accelerate --vcpus=8"
	# assumed Windows
	#TODO Add support for other operating systems
	VM_OPTIONS=$VM_OPTIONS" --os-type windows --os-variant=win2k8"

	# extend command line with graphics passthrough string, if needed
	if [ "$GFX_PASSTHROUGH" != "" ]; then
		VM_OPTIONS=$VM_OPTIONS" $GFX_PASSTHROUGH"
	fi

	if [ "$BOOT_UEFI" != "" ]; then
		VM_OPTIONS=$VM_OPTIONS" $BOOT_UEFI"
	fi

	# start VNC server on KVM
	VM_OPTIONS=$VM_OPTIONS" --graphics vnc,listen=$CARD_ADDR"
	# create mac
	if [ "$NODE_ID" != "" ]; then
		export VM_MAC="$MAC_PREFIX:00:0$(($CARD_ID+1)):0$(($NODE_ID+1))"
	fi
	# check mac override
	if [ "$MAC_OVERRIDE" != "" ]; then
		export VM_MAC="$MAC_OVERRIDE"
	fi

	# Pass network configuration to VM
	if [ "$VM_MAC" != "" ]; then
		if [ "$USE_BRIDGING" == "Y" ]; then
			export VM_OPTIONS=$VM_OPTIONS" --network bridge=$BRIDGE_IF,model=virtio,mac=$VM_MAC"
		elif [ "$USE_ROUTING" == "Y" ]; then
			export VM_OPTIONS=$VM_OPTIONS" --network network,model=virtio,source=$NET_CFG"
		fi
		echo $VM_MAC > /etc/vca_vm_mac
	fi

	# Check if installation from external media required; if no, look for OS on disk
	if [ "$INSTALL_IMAGE" != "" ]; then
		check_nfs_file $INSTALL_IMAGE
		export VM_OPTIONS=$VM_OPTIONS" --cdrom $NFS_MOUNT/$INSTALL_IMAGE"
	else
		export VM_OPTIONS=$VM_OPTIONS" --import"
	fi

	# Provide storage to VM
	if [ "$IMAGE_NAME" != "" ]; then
		check_nfs_file $IMAGE_NAME
		export VM_OPTIONS=$VM_OPTIONS" --disk $NFS_MOUNT/$IMAGE_NAME"
	fi

	# Provide unbootable cdrom image to VM
	if [ "$CDROM_IMAGE" != "" ]; then
		check_nfs_file $CDROM_IMAGE
		export VM_OPTIONS=$VM_OPTIONS" --disk $NFS_MOUNT/$CDROM_IMAGE,device=cdrom"
	fi

	# Reserve RAM for KVM/Dom0 and pass remaining memory to VM
	export MEM_TOTAL_MB=`awk '/MemTotal/ { printf "%d\n", $2/1024 }' /proc/meminfo`	# Total RAM installed in MB
	if [ ${DOM0_RAM_MB} -lt ${MEM_TOTAL_MB} ] ; then
		export VM_MEM_MB=$((${MEM_TOTAL_MB}-${DOM0_RAM_MB}))
		echo "Reserving ${DOM0_RAM_MB} MB of RAM for KVM/Dom0. Leaving ${VM_MEM_MB} MB of RAM for VM"
	else
		echo "To little RAM: ${DOM0_RAM_MB} MBs requested for KVM/Dom0, ${MEM_TOTAL_MB} MB available"
		exit 1
	fi
	VM_OPTIONS=$VM_OPTIONS" -r ${VM_MEM_MB}"

	# Expose exactly the same cpu as in Dom0
	VM_OPTIONS=$VM_OPTIONS" --cpu host-passthrough"

	# Start VM
	echo "Starting VM as virt-install $VM_OPTIONS"
	virt-install $VM_OPTIONS
	# Display list of VMs (for reporting purposes only)
	virsh list
}

# Configure network addressing, basing on card and CPU ID
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

# Create network config file to enable KVM DHCP server
create_network_cfg_file () {
	cat > $NETWORK_CFG_XML << EOL
<network>
	<name>$NET_CFG</name>
	<forward mode='route'/>
	<bridge name='$ROUTE_IF' stp='on' delay='0'/>
	<mac address='52:54:de:ad:be:ef'/>
	<ip address='$CARD_ADDR' prefix='$MASK_LEN'>
		<dhcp>
			<range start='$DOMU_ADDR' end='$DOMU_ADDR'/>
		</dhcp>
	</ip>
</network>
EOL
}

# Detach graphics from host OS and create boot line to pass it to VM
configure_gfx () {
	echo "Using graphics passthrough"
	GFX_PASSTHROUGH=""
	for ADDRESS in $(lspci | awk '/VGA compatible/ {print "pci_0000_"$1}' | sed "s/:\|\./_/g"); do
		virsh nodedev-dettach $ADDRESS
		export GFX_PASSTHROUGH="$GFX_PASSTHROUGH --host-device $ADDRESS"
	done;
	[ -z "$GFX_PASSTHROUGH" ] && echo "No graphics found"
	# due to problem with new gfx driver (for SKL(MV/VV)) guest's msrs must be ignored
	echo "KVM Guest's MSRs are ignored"
	echo 1 > /sys/module/kvm/parameters/ignore_msrs
}

# Count the smallest network mask, containing two network addresses
count_network_mask () {
	ADDR1=$1
	ADDR2=$2

	ADDR1_SUM=0
	ADDR2_SUM=0

	BYTE=1
	while [  $BYTE -lt 5 ]; do
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

# KVMGT methods
# Create UUID for VM
create_uuid() {
	UUID=$(uuid)
	(set -x; echo "$UUID"  > "/sys/bus/pci/devices/0000:00:02.0/mdev_supported_types/i915-GVTg_V5_8/create")
}

# Create network scripts for VM
create_net_scripts() {
        echo "Create net scripts"
        echo "#!/bin/sh -x
                if [ -n "\$1" ]; then
                        ip link set "\$1" nomaster
                        ip link set "\$1" down
                        exit 0
                fi
                echo "Error: no interface specified"
                exit 1" > $NET_SCRIPT_PATH"qemu-ifdown"
        echo "#!/bin/sh -x
                if [ -n "\$1" ]; then
                        ip tuntap add "\$1" mode tap user `whoami`
                        ip link set "\$1" up
			ip link set mtu "$ROUTING_MTU" dev "\$1"
                        sleep 0.5s
                        ip link set "\$1" master "$BRIDGE"
                        exit 0
                fi
                echo "Error: no interface specified"
                exit 1" > $NET_SCRIPT_PATH"qemu-ifup"
        chmod +x $NET_SCRIPT_PATH"qemu-ifdown"
        chmod +x $NET_SCRIPT_PATH"qemu-ifup"
}

# Create VM starting command line and invoke it
run_vm_kvmgt_cmd() {
	(set -x; $QEMU_PATH -daemonize -m $MEMORY -smp $SMP -M pc \
		-name $VM_NAME -hdb $NFS_MOUNT/$IMAGE_NAME \
		-bios /usr/bin/bios.bin -enable-kvm \
		-net nic,macaddr=$MAC_OVERRIDE -net tap,script=$NET_SCRIPT_PATH/qemu-ifup \
		-machine kernel_irqchip=on -vnc :$VM_VNC_PORT \
		-vga qxl -device vfio-pci,sysfsdev=/sys/bus/pci/devices/0000:00:02.0/$UUID,rombar=0 \
		-cpu host -usb -usbdevice tablet)
}

start_vm_kvmgt() {
	network_addressing && mount_nfs && create_net_scripts && create_uuid && run_vm_kvmgt_cmd
	exit $?
}

# End KVMGT methods

export IMAGE_NAME=""
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
export BOOT_UEFI=""

# main
# parse provided parameters
parse_parameters "$@"

# CREATE KVMGT - exit if script done
[ "$KVMGT" == "--kvmgt" ] && start_vm_kvmgt

# Configure network addressing
network_addressing

if [ "$CARD_ADDR" == "" ]; then
	echo "Unknown card address. Please provide address directly, or provide card and node number to use default ones"
	exit 1
fi
if [ "$NFS_ADDR" == "" ]; then
	echo "Unknown NFS address. Please provide address directly, provide host address or card and node number to calculate default ones"
	exit 1
fi

# Count the most fitting network mask containing VM and card's IP
count_network_mask $CARD_ADDR $DOMU_ADDR
#start_xen
if [ "$GFX_PASSTHROUGH" == "YES" ]; then
	configure_gfx
fi

if [ "$USE_BRIDGING" == "Y" ]; then
	start_bridge
fi

if [ "$USE_CARD_MAC" == "Y" ]; then
	use_card_mac
fi

# mount NFS share
mount_nfs

if [ "$USE_ROUTING" == "Y" ]; then
	# create XML for DHCP server
	create_network_cfg_file
fi

# start KVM preconfiguration
start_kvm

#Start VM
start_vm
echo "VM started"

if [ "$USE_ROUTING" == "Y" ]; then
	echo "Enabling routing"
	# Set MTU
	ifconfig $ROUTE_IF_NIC mtu $ROUTING_MTU
	ifconfig $KVM_IF mtu $ROUTING_MTU
	# display interface (for reporting purposes only)
	ifconfig $ROUTE_IF

	# Enable routing
	echo 1 > /proc/sys/net/ipv4/ip_forward
	echo 1 > /proc/sys/net/ipv4/conf/eth0/proxy_arp
	echo 1 > /proc/sys/net/ipv4/conf/$ROUTE_IF/proxy_arp

	# Add firewall exceptions to enable VNC, RDP and iperf
	if hash firewall-cmd 2>/dev/null; then
		firewall-cmd --zone=public --add-port=5900/tcp --permanent
		firewall-cmd --zone=public --add-port=3389/tcp --permanent
		firewall-cmd --zone=public --add-port=5201/tcp --permanent
		firewall-cmd --reload
	else
		iptables -I INPUT -p tcp -m tcp --dport 5900 -j ACCEPT
		iptables -I INPUT -p tcp -m tcp --dport 3389 -j ACCEPT
		iptables -I INPUT -p tcp -m tcp --dport 5201 -j ACCEPT
		service iptables save
		service iptables restart
	fi
elif [ "$USE_BRIDGING" == "Y" ]; then
	# Add firewall expception to forward all traffic on bridged interface and to accept VNC
	if hash firewall-cmd 2>/dev/null; then
		firewall-cmd --permanent --direct --passthrough ipv4 -I FORWARD -m physdev --physdev-is-bridged -j ACCEPT
		firewall-cmd --zone=public --add-port=5900/tcp --permanent
		firewall-cmd --reload
	else
		iptables -I FORWARD -m physdec --physdev-is-bridged -j ACCEPT
		iptables -I INPUT -p tcp -m tcp --dport 5900 -j ACCEPT
		service iptables save
		service iptables restart
	fi
fi

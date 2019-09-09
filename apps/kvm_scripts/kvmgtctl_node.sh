#!/bin/bash

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

# Params
ROUTING_MTU="65500"
DRIVER="i915-GVTg_V5_8"
QEMU_PATH="/usr/bin/qemu-system-x86_64"
MEMORY="3072"
SMP="2"
VM_NAME="kvmgt_vm"
NET_SCRIPT_PATH="/etc/"
BRIDGE="vcabr0"
MAX_NFS_RETRIES="5"
SHOW_IP=true

function die {
        rc=$?
        test $rc = 0 && rc=99
        echo -e "$@" >&2
        exit $rc
}

function print_help {
	# Keep parameters alphabetically sorted in help and in the 'case' switch which parses them.
	# Prefer single-character options, i.e. do not introduce long options without an apparent reason.
	echo "Help:
	drivers                 show supported kvmgt drivers
	fast_status		show fast actual status - without IP
	force_kill		kill all qemu instances
	info                    show configuration
	-m <vmID>		set VM_ID
	remove_unused           remove unused devices
	set_min_freq <value>    set min frequency value, the value should not be lower than 350 [MHz]
	set_max_freq <value>    set max frequency value, the value should not be higher than 1150 [MHz]
	start <fullImgPath> <mountPoint>
	status                  show actual status: MAC and IP addresses, available cores and memory, etc."
}

parse_parameters () {
	[ $# == 0 ] || [ "$1" == "-h" ] || [ "$1" == "--help" ] && print_help && exit 0

        while [ "$1" != "" ]
        do
                case "$1" in
			drivers)
                                show_drivers || die 'Error: show_drivers failed'
                                shift;;
			fast_status)
                                SHOW_IP=false
                                status || die 'Error: showing status failed'
                                shift;;
			force_kill)
				force_kill_all || die 'Error: force_kill failed'
                                shift;;
                        info)
				info || die 'Error: geting info failed'
                                shift;;
			-m)
				parsed_input=$(echo "$2" | grep -E '[0-9]{1,2}')
				[[ "$parsed_input" != ""  && "$parsed_input" -lt 16 ]] || die 'Error: Wrong VM id! Please set id from a range [0;15]' # 0-15 (0-F hex)
				VM_ID=$2
                                shift; shift;;
			remove_unused)
                                remove_unused_vm || die 'Error: remove_unused_vm failed'
                                shift;;
                        set_min_freq)
				isPositiveInt "$2" || die 'Error: gt_min_freq_mhz is not a number'
				echo "$2" > /sys/class/drm/card0/gt_min_freq_mhz || die 'Error: setting gt_min_freq_mhz failed'
                                shift; shift;;
			set_max_freq)
				isPositiveInt "$2" || die 'Error: gt_max_freq_mhz is not a number'
				echo "$2" > /sys/class/drm/card0/gt_max_freq_mhz || die 'Error: setting gt_max_freq_mhz failed'
                                shift; shift;;
			start)
				start_vm "$2" "$3" "$4" || die 'Error: starting vm failed' # <fullImgPath> <mountPath> <hostIp>
				shift; shift; shift; shift;;
			status)
				status || die 'Error: showing status failed'
                                shift;;
                        *)
                                echo "unknown parameter '$1'"
                                exit 1
                                ;;
                esac
        done
}

function isPositiveInt {
	grep -Ecq '^\.?[0-9]*\.?[0-9]+$' <<< "$1"
}

function force_kill_all {
	pkill qemu && sleep 5 || echo "Can not kill any qemu process"
	remove_unused_vm
}

function info {
	echo "	Min freq: $(cat /sys/class/drm/card0/gt_min_freq_mhz)
	Cur freq: $(cat /sys/class/drm/card0/gt_cur_freq_mhz)
	Max freq: $(cat /sys/class/drm/card0/gt_max_freq_mhz)
	Available instances: $(cat /sys/bus/pci/devices/0000:00:02.0/mdev_supported_types/$DRIVER/available_instances)"
}

function show_drivers {
	ls /sys/bus/pci/devices/0000:00:02.0/mdev_supported_types/
}

function remove_unused_vm {
	[[ $(ls -l /sys/bus/pci/devices/0000:00:02.0/*/remove 2>/dev/null ) ]] && for i in /sys/bus/pci/devices/0000:00:02.0/*/remove; do echo 1 > "$i"; done
	echo -e "\tAvailable instances: $(cat /sys/bus/pci/devices/0000:00:02.0/mdev_supported_types/$DRIVER/available_instances)\n"
}

# use nmap to find all IPs in IP subnet to which $BRIDGE IP belongs
# put result in global var VM_IPS
function scan_vm_ips {
	subnet_with_mask=$(ip route show|awk '$0 ~ /'$BRIDGE'.+ src / { print $1 }')
	VM_IPS="$(nmap -sn -n "$subnet_with_mask")"
}

# find IP for given MAC (as first arg)
function find_ip {
	test -z "$VM_IPS" && scan_vm_ips
	vm_mac="$1"
	grep -i -B2 "$vm_mac" <<< "$VM_IPS" \
		| sed -n 's/.*[^0-9]\([0-9]\+\.[0-9]\+\.[0-9]\+\.[0-9]\+\).*/\1/p' #Ex. output: 10.91.1.1
}

# parse line like below, input is STDIN, output is TAB separated
#18150 /usr/bin/qemu-system-x86_64 -daemonize -m 3072 -smp 2 -M pc -name kvmgt_vm -hdb /mnt/tmp/vca_windows_10_baremetal_MBR_2.1.194.img -bios /usr/bin/bios.bin -enable-kvm -net nic,macaddr=20:1e:67:e0:6c:09 -net tap,script=/etc//qemu-ifup -machine kernel_irqchip=on -vnc :12 -vga qxl -device vfio-pci,sysfsdev=/sys/bus/pci/devices/0000:00:02.0/ecda88aa-1885-11e8-b738-001e67e06c09,rombar=0 -global PIIX4_PM.disable_s3=1 -global PIIX4_PM.disable_s4=1 -cpu host -usb -usbdevice tablet
# shellcheck disable=SC1004
function process_qemu_cmdline {
	MAC="$1"
	IP="$2"
	awk -v mac="$MAC" -v ip="$IP" \
	'{
		OFS = "\t"; # output field separator
		for (i = 3; i <= NF; ++i) { # first two are PID and program name
			dict[$i] = $(i+1)
		}
		print substr(dict["-vnc"],2,2)-10, dict["-name"], dict["-m"], dict["-smp"], \
			mac, ip, dict["-hdb"], dict["-vnc"];
	}'
}

# Create UUID for VM
function create_uuid {
        UUID=$(uuid)
        echo "$UUID"  > "/sys/bus/pci/devices/0000:00:02.0/mdev_supported_types/$DRIVER/create"
}

function run_vm_kvmgt_cmd {
        (set -x; "$QEMU_PATH" -daemonize -m "$MEMORY" -smp "$SMP" -M pc \
                -name "$VM_NAME" -hdb "$NFS_MOUNT"/"$IMG_NAME" \
                -bios /usr/bin/bios.bin -enable-kvm \
                -net nic,macaddr="$MAC_OVERRIDE" -net tap,script="$NET_SCRIPT_PATH"/qemu-ifup \
                -machine kernel_irqchip=on -vnc :"$VM_VNC_PORT" \
                -vga qxl -device vfio-pci,sysfsdev=/sys/bus/pci/devices/0000:00:02.0/"$UUID",rombar=0 \
                -global PIIX4_PM.disable_s3=1 -global PIIX4_PM.disable_s4=1 \
                -cpu host -usb -usbdevice tablet)
}

# Create network scripts for VM
function create_net_scripts {
        echo "#!/bin/sh -x
                if [ -n \"\$1\" ]; then
                        ip link set \"\$1\" nomaster
                        ip link set \"\$1\" down
                        exit 0
                fi
                echo \"Error: no interface specified\"
                exit 1" > $NET_SCRIPT_PATH"/qemu-ifdown"
        echo "#!/bin/sh -x
                if [ -n \"\$1\" ]; then
                        ip tuntap add \"\$1\" mode tap user $(whoami)
                        ip link set \"\$1\" up
                        ip link set mtu $ROUTING_MTU dev \"\$1\"
                        sleep 0.5s
                        ip link set \"\$1\" master $BRIDGE
                        exit 0
                fi
                echo \"Error: no interface specified\"
                exit 1" > $NET_SCRIPT_PATH"/qemu-ifup"
        chmod +x $NET_SCRIPT_PATH"/qemu-ifdown"
        chmod +x $NET_SCRIPT_PATH"/qemu-ifup"
}

function run_vm {
	NODE_MAC=$(ifconfig eth0 | sed -nre 's/.*(HWaddr|ether) (([a-zA-Z0-9]{2}:){5}[a-zA-Z0-9]{2}).*/\2/p')
	MAC_OVERRIDE=$(awk -F: -v vm_id="$VM_ID" \
	'{
		OFS=":";
		msn=sprintf("%x", (index("0123456789abcdef", substr($4, 1, 1))+vm_id)%16); #00:00:00:X0:00:00 - setting X value for each of VM
                $4=msn substr($4, 2, 1);
		print;
	}' <<< "$NODE_MAC")

	VM_VNC_PORT="$((10 + VM_ID))"
	run_vm_kvmgt_cmd
}

# Mount nfs resources, if not mounted yet
function mount_nfs {
        # check if desired nfs share is already mounted
        if [ "$(mount |grep "$NFS_ADDR":"$NFS_PATH")" == "" ]; then
                # check if anything other is mounted at defined mountpoint; unmount it
                [[ $(mount |grep "$NFS_MOUNT") != "" ]] && umount "$NFS_MOUNT"

                # mount desired nfs share at defined mountpoint
                i=0
                until [ $i -ge  $MAX_NFS_RETRIES ]
                do
                        mkdir -p "$NFS_MOUNT"
                        mount -t nfs "$NFS_ADDR":"$NFS_PATH" "$NFS_MOUNT" && break
                        i=$($i+1)
                        sleep 2
                done
                [[ "$i" -ge  $MAX_NFS_RETRIES ]] && die "$MAX_NFS_RETRIES unsuccessful attempts to mount nfs, aborting"
        fi
        echo "nfs: $NFS_ADDR:$NFS_PATH"
}


function start_vm {
	NFS_PATH=$(dirname "$1") && IMG_NAME=$(basename "$1") && NFS_MOUNT="$2" && NFS_ADDR="$3"
	echo "NFS_PATH=$NFS_PATH IMG_NAME=$IMG_NAME NFS_MOUNT=$NFS_MOUNT NFS_ADDR=$NFS_ADDR"
	mount_nfs && create_net_scripts && create_uuid && run_vm
}

function status {
	QEMU_INSTANCES=$(pgrep -a qemu)
	echo -e "Running VM: $(grep -c macaddr <<< "$QEMU_INSTANCES")\nID\tNAME\tMEMORY\tCORES\tMAC\tIP\tIMG_PATH\tVNC"

	OLDIFS="$IFS"
	IFS=$'\n'
	for line in $QEMU_INSTANCES; do
		MAC=$(sed -re 's/.+macaddr=([^ ]+).*/\1/' <<< "$line")
		if [ $SHOW_IP = "true" ]; then
			IP=$(find_ip "$MAC")
		else
			IP="-"
		fi
		process_qemu_cmdline "$MAC" "${IP:-unknown}" <<< "$line"
	done \
		| LC_ALL=C sort # sort by ID
	IFS="$OLDIFS"
}

#Main
parse_parameters "$@"

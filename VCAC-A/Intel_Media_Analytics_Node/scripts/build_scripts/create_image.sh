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
set -euo pipefail

readonly SCRIPT_DIR="$( cd "$(dirname "$0")" && pwd )"	# $0 works for more shells than  ${BASH_SOURCE[0]}
. "${SCRIPT_DIR}/library_image_creation.sh"

ARCHIVE_FILES_OPTION_ARRAY=()
BLOCKIO_DISK_SIZE_GB=50  # 2GB for STD, 3GB for MSS (boot partition + rootfs). Up to 50GB tested.
BOOTSTRAP_FILE=""
readonly CONST_BAK=.bak
readonly CONST_DOMU_DISK_SIZE_MB=3072
readonly CONST_EFI_SYSTEM_PARTITION_SIZE_MB=300
readonly CONST_ROOTFS_PARTITION_SIZE_MB=3072	# keep below 4096 MB if using cpio from dracut; keep below 2.1GB for domU
DESCRIPTION=""
DRACUT_COMPRESSOR=""
GRUB_CFG=""
IMAGE_NAME=""
IMAGE_TYPE=""
KERNEL_VER=""
readonly NOLOOP=""		# non-empty value means running directly from initramfs, not from final rootfs mounted over loop from dracut initramfs
OUTPUT_DIR=""
UNCOMPRESSED_IMAGE=FALSE

show_help() {
	# Keep parameters alphabetically sorted in help and in the 'case' switch which parses them.
	# Prefer single-character options, i.e. do not introduce long options without an apparent reason.
	echo "Usage: $0 [OPTIONS]
Creates a bootable VCA image.
Options:
-a, --archive <path>	The location of the archive with packages to be added to the root filesystem bootstrap. This option can be used multiple times. Archives will be processed in the order in which they appear on the command line. The packages within every archive will be added with their dependencies satisfied. All dependent packages must be included in the same archive, or in one of the archives applied before.
-b, --bootstrap	<path>	The location of the root filesystem bootstrap archive. This archive will be applied first, to create the base root filesystem. This parameter is mandatory.
-c, --compress <name>	Optional name of the external compression engine to be passed to dracut as --compress.
-d, --descr <description>	OS version description to fake the target environment.
-g, --grub-cfg <file>	GRUB configuration file to be embedded in the VCA image.
-h, --help		Show this help screen.
-i, --image-type <type>	Type of image. Supported types:
	vca-disk	- persistent, block I/O bootable
	persistent-bm	- persistent, rootfs over NFS
	volatile-bm	- volatile baremetal
	volatile-kvm	- volatile with KVM
	volatile-dom0	- volatile with Xen
	domu		- volatile domU for Xen
-k, --kernel <version>	VCA kernel version for which the image is created, used to generate a valid grub config and to fake the target environment.
-n, --image-name <name>	File name of the output image.
-o, --out-dir	The destination directory for the VCA image. This parameter is mandatory.
-s, --blockio-disk-size	The size (in GBs) of the disk image for block I/O bootable images. Must be an integer.
-u, --uncompressed	Leave the final image not compressed (only used for blockio images).
"
}
# TODO: E: Getting name for slave of master fd 8 failed! - ptsname (2: No such file or directory)
# TODO: debconf: delaying package configuration, since apt-utils is not installed
parse_parameters(){
	while [ $# -gt 0 ] ; do
		case "$1" in
			-a|--archive)
				ARCHIVE_FILES_OPTION_ARRAY+=("-a ${2:-""}")
				shift; shift;;
			-b|--bootstrap)
				BOOTSTRAP_FILE="${2:-""}"
				shift; shift;;
			-c|--compress)	# this option is unused as of 2017.12
				DRACUT_COMPRESSOR="${2:-""}"
				shift; shift;;
			-d|--descr)
				DESCRIPTION="${2:-""}"
				shift; shift;;
			-g|--grub-cfg)
				GRUB_CFG="${2:-""}"
				shift; shift;;
			-h|--help)
				show_help
				exit 0;;
			-i|--image-type)
				IMAGE_TYPE="${2:-""}"
				shift; shift;;
			-k|--kernel)
				KERNEL_VER="${2:-""}"
				shift; shift;;
			-n|--image-name)
				IMAGE_NAME="${2:-""}"
				shift; shift;;
			-o|--out-dir)
				OUTPUT_DIR="${2:-""}"
				shift; shift;;
			-s|--blockio-disk-size)
				BLOCKIO_DISK_SIZE_GB="${2:-""}"
				shift; shift;;
			-u|--uncompressed)
				UNCOMPRESSED_IMAGE=TRUE
				shift;;
			*)
				show_help && die "Unknown parameter '$1'"
		esac
	done
}

check_parameters () {
	# -a, -b, parameters will be checked by the create_rootfs.sh script
	[ -z "${IMAGE_TYPE}" ] 	&& show_help && die "Image type not given"
	[ -z "${KERNEL_VER}" ]	&& show_help && die "Kernel version not given"
	[ -z "${IMAGE_NAME}" ]	&& show_help && die "Output image name not given"
	[ -z "${GRUB_CFG}" ]	&& show_help && die "GRUB configuration file not given"
	[ -n "${GRUB_CFG}" ] && [ ! -s "${GRUB_CFG}" ] && show_help && die "Configuration file ${GRUB_CFG} does not exist or is empty"
	[ -z "${OUTPUT_DIR}" ]	&& show_help && die "Output directory not given"
	[ -z "${DESCRIPTION}" ]	&& show_help && die "OS version description not given"
	#[ -n "${OUTPUT_DIR}" ] && [ -n "$(find "${OUTPUT_DIR}" -mindepth 1 -maxdepth 1 2>/dev/null )" ]  && die "Output directory ${OUTPUT_DIR} already exists and is not empty"
	return 0
}

create_cooked_partition_file(){
	local _FSTYPE="$1"
	local _PARTITION="$2"
	local _MB_SIZE="$3"

	# Only create the files if they do not already exist as block special, i.e. not as disk (image) partitions
	# fallocate fails on ext3 with "fallocate failed: Operation not supported". See man fallocate. : fallocate -l ${_MB_SIZE}M "${_PARTITION}"
	if [ ! -b "${_PARTITION}" ] ; then
		dd if=/dev/zero bs=1M of="${_PARTITION}" seek="${_MB_SIZE}" count=0 2>/dev/null || die "Could not create partition ${_PARTITION} of size ${_MB_SIZE}"
	fi
	local _FS_OPTIONS=""
	[[ "${_FSTYPE}" == *fat ]] && _FS_OPTIONS="-F 32 -s 2"	# EFI System Partition requires FAT32. The "-s2" is for CentOS 7.3 to avoid: "WARNING: Not enough clusters for a 32 bit FAT!"
	mkfs."${_FSTYPE}" ${_FS_OPTIONS} "${_PARTITION}" <<<y > /dev/null || die "Could not create ${_FSTYPE} filesystem on ${_PARTITION}" # "y" = answer to: "<file> is not a block special device. Proceed anyway? (y,n)"; _FS_OPTIONS can be empty (=neglecting shellcheck error SC2086)
}

mount_and_run(){
	local _OPTS="$1"; shift
	local _DEV="$1"; shift
	local _DIR="$1"; shift
	local _MSG="$1"; shift
	local _RUN=("${@}")		# function/program with parameters, to be run during _DEV is mounted on _DIR

	 sudo mount ${_OPTS} "${_DEV}" "${_DIR}" || die "Could not mount ${_MSG} ${_DIR} on ${_DEV} with ${_OPTS:-no} option(s)" # _OPTS can be empty (=neglecting shellcheck error SC2086)
	 "${_RUN[@]:-$(return 1)}" || die "Command unsuccesfull: ${_RUN[*]:-"NONE GIVEN"}"
	 sudo umount "${_DIR}"  || die "Could not umount ${_MSG} {_DIR} mounted on ${_DEV}"
}

populate_rootfs_partition(){
	local _PARTITION_IMAGE_DIR="$1"

	/bin/rm -fr "${_PARTITION_IMAGE_DIR}/lost+found" # create_rootfs.sh expects an empty dir
	"${SCRIPT_DIR}"/create_rootfs.sh -b "${BOOTSTRAP_FILE}" ${ARCHIVE_FILES_OPTION_ARRAY[@]:-} -k "${KERNEL_VER}" -d "${DESCRIPTION}" -o "${_PARTITION_IMAGE_DIR}" || die "Could not create the root file system" # ARCHIVE_FILES_OPTION_ARRAY can be empty (=neglecting shellcheck error SC2086)
	[ ! -d "${_PARTITION_IMAGE_DIR}/lost+found" ] && mkdir "${_PARTITION_IMAGE_DIR}/lost+found" && chmod 700 "${_PARTITION_IMAGE_DIR}/lost+found" # re-create just for fun; the next fsck would do it anyway

	local LEFTOVER_FILES; LEFTOVER_FILES="$(cd "${_PARTITION_IMAGE_DIR}"; ls root/* tmp/* 2>/dev/null ; find . -maxdepth 2 -name virtual_archive_\* )"
	[ -n "${LEFTOVER_FILES}" ] && warn "The following leftover files unnecessarily remain in the image root filesystem: ${LEFTOVER_FILES}."

	if [[ "${IMAGE_TYPE}" == volatile* ]] ; then
		# zero the free space on the rootfs so it gets compressed better:
		local _CLEANER; _CLEANER="$(mktemp --tmpdir="${_PARTITION_IMAGE_DIR}" cleaner.XXX )"
		dd if=/dev/zero of="${_CLEANER}" bs=512 count=10000000 2>/dev/null || true # cleans up to 5G, which is more than 1.7G expected
		/bin/rm "${_CLEANER}"
		# decrease rootfs partition to 2.5GB:
		local _DEVICE; _DEVICE="$(grep " \+${_PARTITION_IMAGE_DIR} \+" /proc/mounts |  cut -d" " -f1)"
		# The _DEVICE may be /dev/loop if taking information from /proc/mounts, or the true underlying file when taking from 'mount' output
		# dynamic regex to grep for slashes
		[[ "${_DEVICE}" == /dev/loop* ]] && _DEVICE="$( losetup --list | awk '$1~pattern { print $6 }' pattern="${_DEVICE}")"
		sudo umount "${_PARTITION_IMAGE_DIR}"
		sudo e2fsck -y -f "${_DEVICE}" >/dev/null 2>&1
		sudo resize2fs "${_DEVICE}" 2500M
		sudo mount "${_DEVICE}" "${_PARTITION_IMAGE_DIR}"
	fi

	# TODO: is graphical.target still a problem after adding the udev package?
	# change the default target, as there seems to be a problem when booting to
	# the graphical.target target. Linking in /etc/systemd/system does not seems to work
	# ln -s --force multi-user.target "${_PARTITION_IMAGE_DIR}"/lib/systemd/system/default.target
	## enable serial console:
	#local _TTY_NAME=ttyUSB0 # or ttyS0
	#local _SERVICE_DEFINITION_FILE=/etc/systemd/system/serial-getty@"${_TTY_NAME}".service
	#cp /lib/systemd/system/serial-getty@.service "${_PARTITION_IMAGE_DIR}/${_SERVICE_DEFINITION_FILE}"
	#ln -s "${_SERVICE_DEFINITION_FILE}"  "${_PARTITION_IMAGE_DIR}"/etc/systemd/system/getty.target.wants

	local _FSTAB=""
	case "${IMAGE_TYPE}" in
		vca-disk)
			_FSTAB="/dev/vcablk0p2	/	ext4	defaults,noatime	0	1
/dev/vcablk0p1	/boot	vfat	defaults,noatime	0	2"
		;;
		volatile*)
			# When mounting over loop device:
			#_FSTAB="/dev/loop0	/	ext4	defaults,noatime	0	1"
			# When running from the initramfs
			_FSTAB="rootfs	/	ext4	defaults,noatime	0	0"
		;;
		domu)
			_FSTAB="/dev/xvda1	/	ext4	relatime,data=ordered	0	1"
		;;
		persistent-bm)
			# TODO: fill DEVICENAME below
			_FSTAB="/dev/DEVICENAME	/	nfs	defaults,noatime	0	1"
		;;
		*)
			die "Unsupported image type: ${IMAGE_TYPE}"
	esac
	echo "# <device>		<dir>	<type>	<options>		<dump>	<fsck>
${_FSTAB}" > "${_PARTITION_IMAGE_DIR}"/etc/fstab

	if [ "${IMAGE_TYPE}" == persistent-bm ] ; then
		tar -zcf "${OUTPUT_DIR}/${IMAGE_NAME%.*}"-tree.tar.gz -C "${_PARTITION_IMAGE_DIR}" . || die "Could not tar the root filesystem "
	fi
}

# creates the root FS on the indicated PARTITION
create_rootfs(){
	local _PARTITION_IMAGE_FILE="$1"

	local _PARTITION_IMAGE_DIR; _PARTITION_IMAGE_DIR="$(mktemp --tmpdir --directory rootfs_partition_dir.XXX)"

	create_cooked_partition_file ext4 "${_PARTITION_IMAGE_FILE}" ${CONST_ROOTFS_PARTITION_SIZE_MB}
	mount_and_run "" "${_PARTITION_IMAGE_FILE}" "${_PARTITION_IMAGE_DIR}" "root filesystem" \
		populate_rootfs_partition "${_PARTITION_IMAGE_DIR}"
	/bin/rm -r "${_PARTITION_IMAGE_DIR}"
}

# Replaces _DST with _SRC, be it a file or directory
# make backup of _DST in _BACKUP_DIR
# TODO: what about "replacing" a non-existent file?
replace(){
	local _BACKUP_DIR="$1"
	local _SRC="$2"
	local _DST="$3"

	local _DST_PATH; _DST_PATH="$(dirname "${_DST}")"
	if [ -d "${_DST_PATH}/" ] ; then
		# make a backup of _DST if _DST exists
		if [ -e "${_DST}" ] ; then
			local _DST_NAME; _DST_NAME="$( basename -- "${_DST}")"
			# free the name for backing-up _DST_NAME in _BACKUP_DIR
			reserve_name "${_BACKUP_DIR}/${_DST_NAME}" "${CONST_BAK}"
			# Can the file to be "replaced" not exist (not observing this now) ?
			[ -e "${_DST}" ] && mv "${_DST}" "${_BACKUP_DIR}/${_DST_NAME}"
		fi
		# TODO: use sudo or fakeroot?
		cp -rp "${_SRC}" "${_DST}" || die "Could not replace ${_DST} with ${_SRC}"
	else
		die "Could not replace ${_DST} with ${_SRC}, as directory ${_DST_PATH} does not exist"
	fi
}

restore(){
	local _BACKUP_DIR="$1"
	local _DST="$2"

	local _DST_PATH; _DST_PATH="$(dirname "${_DST}")"
	if [ -d "${_DST_PATH}/" ] ; then
		local _DST_NAME; _DST_NAME="$( basename -- "${_DST}")"
		# TODO: use sudo or fakeroot?
		/bin/rm -fr "${_DST}" 2>/dev/null || true
		if [ -e "${_BACKUP_DIR}/${_DST_NAME}" ] ; then
			# TODO: use sudo or fakeroot?
			mv "${_BACKUP_DIR}/${_DST_NAME}" "${_DST}"  || warn "Could not restore ${_DST} from ${_BACKUP_DIR}/${_DST_NAME}"
			release_name "${_BACKUP_DIR}/${_DST_NAME}" "${CONST_BAK}"
		else
			notice "No backup ${_BACKUP_DIR}/${_DST_NAME} exists to restore ${_DST}"
		fi
	else
		warn "Could not restore ${_DST} from ${_BACKUP_DIR}/${_DST_NAME} as directory ${_DST_PATH} does not exist"
	fi
}

# TODO: to avoid the message: [**    ] A start job is running for dev-ttyS0.device (47s / 1min 30s)
# ln -s /lib/systemd/system/serial-getty\@service
#       /etc/systemd/system/getty.target.wants/serial-getty\@ttyUSB0.service
#
#  ln -s /lib/systemd/system/serial-getty@.service /etc/systemd/system/getty.target.wants/
#  ln -s /lib/systemd/system/serial-getty@.service /tmp/a/lib/systemd/system/getty.target.wants/
# ln -s /lib/systemd/system/netctl.service
#       /etc/systemd/system/multi-user.target.wants/netctl.service
# systemctl stop serial-getty at ttyS0.service
# systemctl start serial-getty at ttyS0.service
# [ TIME ] Timed out waiting for device dev-ttyS0.device.
# [DEPEND] Dependency failed for Serial Getty on ttyS0.
# another to consider:
# systemctl get-default # returns graphical.target
# systemctl set-default multi-user.target # Created symlink /etc/systemd/system/default.target,
# 												pointing to /lib/systemd/system/multi-user.target.
# rootfs ma:
#	/tmp/r/lib/systemd/system/default.target -> graphical.target
#	...ale zwraca: systemctl get-default multi-user.target , bo ma tez:
#	/tmp/r/etc/systemd/system/default.target -> /lib/systemd/system/multi-user.target

create_initramfs(){
	local _INITRAMFS_FILE="$1"
	local _ROOTFS_PARTITION_DIR="$2"
	local _ROOTFS_PARTITION_FILE="$3"

	[ -n "${DRACUT_COMPRESSOR}" ] && DRACUT_COMPRESSOR="--compress ${DRACUT_COMPRESSOR}"

	local _BACKUP_DIR; _BACKUP_DIR="$(mktemp --directory --tmpdir backup_dir.XXX)"
	local _VOID_DIR; _VOID_DIR="$(mktemp --directory --tmpdir backup_void_dir.XXX)"
	local _VOID_FILE; _VOID_FILE="$(mktemp --tmpdir backup_void_file.XXX)"
	replace "${_BACKUP_DIR}" "${_VOID_FILE}" /etc/dracut.conf
	replace "${_BACKUP_DIR}" "${_VOID_DIR}" /etc/dracut.conf.d
    replace "${_BACKUP_DIR}" "${_VOID_DIR}" /usr/lib/dracut/dracut.conf.d
	/bin/rm "${_VOID_FILE}"
	/bin/rm -r "${_VOID_DIR}"

	# Dracut installs kernel modules from ${_ROOTFS_PARTITION_DIR}"/lib/modules/"${KERNEL_VER}
	# If the directory does not exists (kernel version is faked) this dirty modification of a global is done, as symlinking will neither resolve the issues of grub nor the of the layout of initrd.
	# TODO: split the VCA kernel version from the faked kernel version (similar, but cleaner than KER_VER and _KER_VER currently in generate_images.sh)
	local _EXISTING_KER_DIR; _EXISTING_KER_DIR="$( find "${_ROOTFS_PARTITION_DIR}"/lib/modules -mindepth 1 -maxdepth 1 -type d )"
	[ "$(wc -l <<< "${_EXISTING_KER_DIR}")" -ne 1 ] && die "Couldn't recreate kernel information, the directory ${_ROOTFS_PARTITION_DIR}/lib/modules does not contain exactly one entry: '${_EXISTING_KER_DIR}'"
	KERNEL_VER="$( basename -- "${_EXISTING_KER_DIR}" )"

	local _DRACUT_STDOUT; _DRACUT_STDOUT="$(mktemp --tmpdir dracut_stdout.XXX )"
	# TODO: test omitting additionally: aufs, bcache, btrfs, dm, lvm, mraid
	# TODO: test omitting additionally for volatiles: resume, rootfs-block, shutdown
	local _DRACUT_COMMON_EXCLUDED_MODULES="biosdevname bootchart btrfs busybox caps cifs crypt dm dmraid fcoe fcoe-uefi iscsi lvm mdraid multipath nbd plymouth" #modsign
	local _DRACUT_ERROR_KEYWORDS="cannot\|could not\|ERROR\|Failed\|field width not sufficient for storing file size" # Not an error, it is about host not the image directory: "Kernel version 4.4.0-1.2.0.10.VCA has no module directory /lib/modules/4.4.0-1.2.0.10.VCA"
	local _DRACUT_STATUS; _DRACUT_STATUS="$( mktemp --tmpdir dracut_status.XXX )"
	case "${IMAGE_TYPE}" in
		domu)
			dracut											\
				--add-drivers "xen-blkfront ext4" 			\
				--omit "${_DRACUT_COMMON_EXCLUDED_MODULES}"	\
				--kmoddir "${_ROOTFS_PARTITION_DIR}"/lib/modules/"${KERNEL_VER}"	\
				--early-microcode ${DRACUT_COMPRESSOR}		\
				--no-hostonly								\
				--force										\
				"${_INITRAMFS_FILE}" 						\
				"${KERNEL_VER}" 2>&1 || { echo $? > "${_DRACUT_STATUS}" ; warn "Dracut failed"; } # cannot exit here before cleaning up
		;;
		persistent-bm)
			# vca.conf on host must be changed to build the dracut image correctly:
			replace "${_BACKUP_DIR}" "${SCRIPT_DIR}"/persistent_files/vca.conf /etc/modprobe.d/vca.conf  || die "Could not replace modprobe config file"
			# TODO: The 40network module for parsing ip=eth0:lbp in grub_reference_persistent.cfg:
			replace "${_BACKUP_DIR}" "${SCRIPT_DIR}"/dracut_module/40network/ifup.sh /usr/lib/dracut/modules.d/40network/ifup.sh  || die "Could not replace ifup.sh in dracut module"
			replace "${_BACKUP_DIR}" "${SCRIPT_DIR}"/dracut_module/40network/parse-ip-opts.sh /usr/lib/dracut/modules.d/40network/parse-ip-opts.sh  || die "Could not replace parse-ip-opts.sh in dracut module"
			replace "${_BACKUP_DIR}" "${SCRIPT_DIR}"/dracut_module/40network/net-lib.sh /usr/lib/dracut/modules.d/40network/net-lib.sh  || die "Could not replace net-lib.sh in dracut module"
			replace "${_BACKUP_DIR}" "${SCRIPT_DIR}"/dracut_module/95nfs/nfs-lib.sh /usr/lib/dracut/modules.d/95nfs/nfs-lib.sh  || die "Could not replace nfs-lib.sh in dracut module"

			# TODO: is --prefix really necessary?
			# --prefix defines dracut's $prefix, e.g. modules.d/99shutdown/module-setup.sh installs $prefix/shutdown in initramfs (=prepends $prefix to the path of all dracut scripts); $prefix is then used to refere to them by other scripts so it can really be anything, including "/" or ""
			# TODO: removed from "--add-drivers": virtio_net virtio_console as they are only needed for Xen. They are probably also  included in 90qemu module as per https://bugzilla.redhat.com/show_bug.cgi?id=1097999
			# TODO: re-added from "--add-drivers": loop ext4 as CentOS continues to install after adding kernel+modules to build host
			dracut																	\
				--add-drivers 'nfs nfsv3 nfsv4 virtio_net virtio_console loop ext4 plx87xx plx87xx_dma vop vop_bus vca_csa' \
				--add "nfs"																\
				--omit "${_DRACUT_COMMON_EXCLUDED_MODULES}"								\
				--include "$(pwd)"/persistent_files/blacklist /etc/modprobe.d/blacklist	\
				--include "$(pwd)"/persistent_files/vca.conf /etc/modprobe.d/vca.conf	\
				--include "$(pwd)"/persistent_files/91-netup.rules /etc/udev/rules.d/91-netup.rules	\
				--include "$(pwd)"/persistent_files/vca_up.sh /sbin/vca_up.sh			\
				--install 'hostname awk cut grep xargs logger sysctl tail'						\
				--prefix /vca															\
				--kmoddir "${_ROOTFS_PARTITION_DIR}"/lib/modules/"${KERNEL_VER}"		\
				--early-microcode ${DRACUT_COMPRESSOR}									\
				--no-hostonly															\
				--force 																\
				"${_INITRAMFS_FILE}"													\
				"${KERNEL_VER}" 2>&1 || { echo $? > "${_DRACUT_STATUS}" ; warn "Dracut failed"; } # cannot exit here before cleaning up

			restore "${_BACKUP_DIR}" /usr/lib/dracut/modules.d/40network/ifup.sh || die "Could not restore ifup.sh in dracut module from ${_BACKUP_DIR}"
			restore "${_BACKUP_DIR}" /usr/lib/dracut/modules.d/40network/parse-ip-opts.sh || die "Could not restore parse-ip-opts.sh in dracut module from ${_BACKUP_DIR}"
			restore "${_BACKUP_DIR}" /usr/lib/dracut/modules.d/40network/net-lib.sh || die "Could not restore net-lib.sh in dracut module from ${_BACKUP_DIR}"
			restore "${_BACKUP_DIR}" /etc/modprobe.d/vca.conf || die "Could not restore modprobe config file from ${_BACKUP_DIR}"
			restore "${_BACKUP_DIR}" /usr/lib/dracut/modules.d/95nfs/nfs-lib.sh || die "Could not restore nfs-lib.sh in dracut module from ${_BACKUP_DIR}"
		;;
		volatile-dom0)
			if [ -n "${NOLOOP}" ] ; then
				( cd "${_ROOTFS_PARTITION_DIR}"; find . | cpio --create --format='newc' ) | gzip > "${_INITRAMFS_FILE}" || die "Could not create initrd"
			else
				# TODO: What about replacing when no /usr/lib/dracut/modules.d/50mountloopdev exists?
				replace "${_BACKUP_DIR}" "${SCRIPT_DIR}"/dracut_module/50mountloopdev /usr/lib/dracut/modules.d/50mountloopdev || die "Could not replace dracut module"

				#TODO: virtio_net virtio_console seem to be used by KVM, not Xen I am trying to enable.
				dracut																	\
					--add-drivers 'virtio_net virtio_console loop ext4 pl2303 plx87xx plx87xx_dma vop vop_bus vca_csa' \
					--omit "${_DRACUT_COMMON_EXCLUDED_MODULES}"								\
					--include "${_ROOTFS_PARTITION_FILE}" /root/root_partition.img			\
					--install 'hostname awk cut grep xargs logger sysctl tail'				\
					--prefix /vca															\
					--kmoddir "${_ROOTFS_PARTITION_DIR}"/lib/modules/"${KERNEL_VER}"		\
					--early-microcode ${DRACUT_COMPRESSOR}									\
					--no-hostonly															\
					--force 																\
					"${_INITRAMFS_FILE}"													\
					"${KERNEL_VER}" 2>&1 || { echo $? > "${_DRACUT_STATUS}" ; warn "Dracut failed"; } # cannot exit here before cleaning up

				restore "${_BACKUP_DIR}" /usr/lib/dracut/modules.d/50mountloopdev || die "Could not restore dracut module from ${_BACKUP_DIR}"
			fi
		;;
		volatile*)
			if [ -n "${NOLOOP}" ] ; then
				( cd "${_ROOTFS_PARTITION_DIR}"; find . | cpio --create --format='newc' ) | gzip > "${_INITRAMFS_FILE}" || die "Could not create initrd"
			else
				# TODO: What about replacing when no /usr/lib/dracut/modules.d/50mountloopdev exists?
				replace "${_BACKUP_DIR}" "${SCRIPT_DIR}"/dracut_module/50mountloopdev /usr/lib/dracut/modules.d/50mountloopdev || die "Could not replace dracut module"

				# The destination paths below are in the initramfs, not in the rootfs of the booted OS
				dracut											\
					--omit-drivers "plx87xx plx87xx_dma"		\
					--add-drivers "loop ext4 pl2303" 			\
					--omit "${_DRACUT_COMMON_EXCLUDED_MODULES}"	\
					--add mountloopdev 							\
					--include "${_ROOTFS_PARTITION_FILE}" /root/root_partition.img		\
					--kmoddir "${_ROOTFS_PARTITION_DIR}"/lib/modules/"${KERNEL_VER}"	\
					--early-microcode ${DRACUT_COMPRESSOR}		\
					--no-hostonly								\
					--force										\
					"${_INITRAMFS_FILE}" 						\
					"${KERNEL_VER}" 2>&1 || { echo $? > "${_DRACUT_STATUS}" ; warn "Dracut failed"; } # cannot exit here before cleaning up

				restore "${_BACKUP_DIR}" /usr/lib/dracut/modules.d/50mountloopdev || die "Could not restore dracut module from ${_BACKUP_DIR}"
			fi
		;;
		vca-disk)
			# vca.conf on host must be changed to build the dracut image correctly:
			replace "${_BACKUP_DIR}" "${SCRIPT_DIR}"/persistent_files/vca.conf /etc/modprobe.d/vca.conf  || die "Could not replace modprobe config file"

			# TODO: is --prefix really necessary?
			# --prefix defines dracut's $prefix, e.g. modules.d/99shutdown/module-setup.sh installs $prefix/shutdown in initramfs
			# TODO: removed from "--add-drivers": virtio_net virtio_console as they are only needed for Xen. They are probably also  included in 90qemu module as per https://bugzilla.redhat.com/show_bug.cgi?id=1097999
			# TODO: re-added from "--add-drivers": loop ext4 as CentOS continues to install after adding kernel+modules to build host
			dracut																	\
			--add-drivers "plx87xx plx87xx_dma vop vop_bus vca_csa" \
			--omit "${_DRACUT_COMMON_EXCLUDED_MODULES}"								\
			--include "$(pwd)"/persistent_files/blacklist /etc/modprobe.d/blacklist	\
			--include "$(pwd)"/persistent_files/vca.conf /etc/modprobe.d/vca.conf	\
			--include "$(pwd)"/persistent_files/91-netup.rules /etc/udev/rules.d/91-netup.rules	\
			--include "$(pwd)"/persistent_files/vca_up.sh /sbin/vca_up.sh			\
			--install 'hostname awk cut grep xargs logger tail'						\
			--prefix /vca										\
			--kmoddir "${_ROOTFS_PARTITION_DIR}"/lib/modules/"${KERNEL_VER}"		\
			--early-microcode ${DRACUT_COMPRESSOR}									\
			--no-hostonly															\
			--force 																\
			"${_INITRAMFS_FILE}"													\
			"${KERNEL_VER}" 2>&1 || { echo $? > "${_DRACUT_STATUS}" ; warn "Dracut failed"; } # cannot exit here before cleaning up

			restore "${_BACKUP_DIR}" /etc/modprobe.d/vca.conf || die "Could not restore modprobe config file from ${_BACKUP_DIR}"
		;;
		*)
			die "Error while generating initramfs. Image type not supported"
		;;
	esac  | tee "${_DRACUT_STDOUT}"

	restore "${_BACKUP_DIR}" /etc/dracut.conf
	restore "${_BACKUP_DIR}" /usr/lib/dracut/dracut.conf.d
	restore "${_BACKUP_DIR}" /etc/dracut.conf.d
	isEmptyDir "${_BACKUP_DIR}" || die "Backup directory ${_BACKUP_DIR} is not empty: $(ls "${_BACKUP_DIR}"). Previous restore(s) failed?"
	/bin/rmdir "${_BACKUP_DIR}"

	local _DRACUT_ERROR_FILE; _DRACUT_ERROR_FILE="$(mktemp --tmpdir dracut_errors.XXX )"
	grep -i "${_DRACUT_ERROR_KEYWORDS}" "${_DRACUT_STDOUT}" > "${_DRACUT_ERROR_FILE}" && warn "Dracut failed with errors: $(cat "${_DRACUT_ERROR_FILE}")" && die "Dracut failed with the above errors"
	_DRACUT_STATUS="$( cat "${_DRACUT_STATUS}" ; /bin/rm "${_DRACUT_STATUS}" )"
	[ -n "${_DRACUT_STATUS}" ] && die "Dracut returned non-zero exit status (${_DRACUT_STATUS})"
	/bin/rm "${_DRACUT_STDOUT}"
	/bin/rm "${_DRACUT_ERROR_FILE}"
}

# populate a mounted boot directory:
populate_boot_directory(){
	local _BOOT_DIR="$1"
	local _ROOTFS_PARTITION_DIR="$2"
	local _INITRAMFS_FILE="$3"

	[ ! "${_ROOTFS_PARTITION_DIR}/boot" -ef "${_BOOT_DIR}" ] && cp "${_ROOTFS_PARTITION_DIR}"/boot/vmlinuz* "${_BOOT_DIR}"
	[ "${IMAGE_TYPE}" == volatile-dom0 ] && cp "${_ROOTFS_PARTITION_DIR}"/boot/xen*.gz "${_BOOT_DIR}"
	cp "${_INITRAMFS_FILE}" "${_BOOT_DIR}"/vca_initramfs.img
}

# create and populate the boot partition in _BOOT_PARTITION_FILE
# make the partition bootable
create_boot_partition(){
	local _BOOT_PARTITION_FILE="$1"
	local _ROOTFS_PARTITION_DIR="$2"
	local _INITRAMFS_FILE="$3"

	local _FS_PARTITION="${_INITRAMFS_FILE}"
	[ "${IMAGE_TYPE}" == domu ] && _FS_PARTITION="$(mount | grep -F " ${_ROOTFS_PARTITION_DIR} " | awk '{ print $1 }')"

	local _FS_SIZE_MB=""
	if [ -b "${_FS_PARTITION}" ] ; then
		# if this is a block device /dev/loopX[pY] then get the size of the underlying regular file
		losetup --set-capacity "${_FS_PARTITION}"
		_FS_SIZE_MB="$( echo "$(blockdev --getsize64 "${_FS_PARTITION}")"/1024/1024 | bc )"
	else
		_FS_SIZE_MB="$(ls "${_FS_PARTITION}" -l --block-size=1M | awk '{ print $5 }')"
	fi
	local _RESERVED_SPACE_MB=50	# in MBs
	[[ "${IMAGE_TYPE}" == vca-disk || "${IMAGE_TYPE}" == persistent-bm ]] && _RESERVED_SPACE_MB=20
	_FS_SIZE_MB=$((_FS_SIZE_MB + _RESERVED_SPACE_MB))
	# The domU uses a single common partition as root and boot, which already exists at this time
	#if [[ "${IMAGE_TYPE}" != domu && "${IMAGE_TYPE}" != volatile-dom0 ]] ; then
	if [ "${IMAGE_TYPE}" != domu ] ; then
		# Ensure the minimum size of the UEFI System Partition:
		[ "${_FS_SIZE_MB}" -lt 100 ] && _FS_SIZE_MB=100
		create_cooked_partition_file fat "${_BOOT_PARTITION_FILE}" "${_FS_SIZE_MB}" || die "Could not create initramfs partition file ${_BOOT_PARTITION_FILE} of size ${_FS_SIZE_MB} MBs"
	fi

	# copy files from the root partition to the boot partition
	if [ "${IMAGE_TYPE}" == domu ] ; then
		populate_boot_directory "${_ROOTFS_PARTITION_DIR}"/boot "${_ROOTFS_PARTITION_DIR}" "${_INITRAMFS_FILE}"
	else
		local _BOOT_PARTITION_DIR; _BOOT_PARTITION_DIR="$(mktemp --tmpdir --directory boot_partition_dir.XXX)"
		mount_and_run "" "${_BOOT_PARTITION_FILE}" "${_BOOT_PARTITION_DIR}" "boot directory" \
			populate_boot_directory "${_BOOT_PARTITION_DIR}" "${_ROOTFS_PARTITION_DIR}" "${_INITRAMFS_FILE}"
		/bin/rm -r "${_BOOT_PARTITION_DIR}"
	fi
	# mount for making bootable
	sudo mount -o remount -o rw mount "${_ROOTFS_PARTITION_DIR}"
	mkdir -p "${_ROOTFS_PARTITION_DIR}/boot"
	mount "${_BOOT_PARTITION_FILE}" "${_ROOTFS_PARTITION_DIR}/boot"
	make_partition_bootable "${_ROOTFS_PARTITION_DIR}"
	umount "${_ROOTFS_PARTITION_DIR}/boot"
}

# boot partition already mouted under ${_ROOTFS_DIR}/boot
# or just using the rootfs partition
make_partition_bootable(){
	local _ROOTFS_DIR="$1"

	# The /etc/grub.cfg is copied to be used during boot phase 1 as (memdisk)/boot/grub/grub.cfg
	# The [/boot]/grub.cfg created in the next step is used in boot phase 2
	local _GRUBCFG=""
	case "${IMAGE_TYPE}" in
		persistent-bm | volatile*)
			_GRUBCFG="set root=(hd0)
configfile (hd0)/grub.cfg"
		;;
		vca-disk)
			_GRUBCFG="insmod part_gpt
set root=(hd0,gpt1)
configfile (hd0,gpt1)/grub.cfg"
		;;
		domu)
			# nothing to be done here
		;;
		*)
			die "Error while configuring grub.cfg. Unsupported image type: ${IMAGE_TYPE}"
	esac
	echo -e "${_GRUBCFG}" > "${_ROOTFS_DIR}"/etc/grub.cfg

	if [ "${IMAGE_TYPE}" == domu ] ; then
		local _ROOTFS_DISK; _ROOTFS_DISK="$(grep -F " ${_ROOTFS_DIR} " /proc/mounts | awk '{ print $1 }' |  cut -dp -f-2 )" # using fgrep, as awk regex is cumbersome with slashes in the path. The cut cuts off the partition part of the path
		do_chroot "${_ROOTFS_DIR}" "grub2-install ${_ROOTFS_DISK}" || die "Could not install the grub bootloader to ${_ROOTFS_DISK}"
		# domu on CentOS uses .../grub2
		sed -e s/VCA_KERNEL_VERSION/"${KERNEL_VER}"/g "${GRUB_CFG}" > "${_ROOTFS_DIR}"/boot/grub2/grub.cfg || die "Could not set kernel name ${KERNEL_VER} in ${_ROOTFS_DIR}/boot/grub2/grub.cfg"
	else
		mkdir -p "${_ROOTFS_DIR}"/boot/EFI/BOOT
		# during boot, the standalone application consisting of *.mod files is in  (memdisk)/boot/grub/x86_64-efi/
		do_chroot "${_ROOTFS_DIR}" /bin/bash <<EOF || die "Could not install the grub bootloader"

			# Make a standalone UEFI application which has all the modules embeddded in a memdisk within the uefi application, thus removing the need for having a separate directory populated with all the GRUB2 uefi modules and other related files. The graft point syntax below means: file will be saved as (memdisk)/${GRUB_CFG_DIR=}/grub.cfg, with the content taken from /etc/grub.cfg
			# Ubutu 16.04 and 16.10 have grub-mkstandalone:
			which grub-mkstandalone > /dev/null 2>&1 && GRUB_MKSTANDALONE=grub-mkstandalone && GRUB_CFG_DIR=/boot/grub
			# CentOS 7.3 and 7.4 have grub2-mkstandalone and /boot/grub/grub.cfg:
			which grub2-mkstandalone > /dev/null 2>&1 && GRUB_MKSTANDALONE=grub2-mkstandalone && GRUB_CFG_DIR=/boot/grub
			\${GRUB_MKSTANDALONE} -d /usr/lib/grub/x86_64-efi/ -O x86_64-efi --fonts="unicode" -o /boot/EFI/BOOT/BOOTX64.EFI "\${GRUB_CFG_DIR}"/grub.cfg=/etc/grub.cfg
EOF
		sed -e s/VCA_KERNEL_VERSION/"${KERNEL_VER}"/g "${GRUB_CFG}" > "${_ROOTFS_DIR}"/boot/grub.cfg \
				|| die "Could not set kernel name ${KERNEL_VER} in ${_ROOTFS_DIR}/boot/grub.cfg"
		# awk '/ {8}linux/ { printf "\t%s %s",$1,"${KERNEL_VER}" ; for(i=3 ; i<=NF; i++) printf " %s", $i ; printf "\n" }' grub.cfg

		echo 'fs0:\EFI\BOOT\BOOTX64.EFI' > "${_ROOTFS_DIR}"/boot/startup.nsh \
				|| die "Could not update the UEFI shell script"
	fi
}

make_GPT_partition(){
	local _DISK_IMAGE="$1"
	local _NUMBER="$2"
	local _START="$3"	# Units are sectors. Empty means selecting the start sector by default at the earliest free part of the disk
	local _SIZE="$4"	# Units are sectors if no unit symbol (K,M,G,T,P) appended. Empty means selecting the size by default till the end of the current block of free space.
	local _TYPE="$5" 	# 1=EFI. Empty means Linux Filesystem.

	[ -n "${_SIZE}" ] && _SIZE="+${_SIZE}"	# only non-empty _SIZE should have the "+" prepended
	echo "
n
${_NUMBER}
${_START}
${_SIZE}
w
" | fdisk "${_DISK_IMAGE}" > /dev/null ||die "Could not create partition number ${_NUMBER} on ${_DISK_IMAGE}"
	# Only set partition type if non-default. When more than one partition already exists, its index must be used:
	if [ -n "${_TYPE}" ] ; then
		if [ "${_NUMBER}" -gt 1 ] ; then
			echo "
t
${_NUMBER}
${_TYPE}
w
"
		else
			echo "
t
${_TYPE}
w
"
		fi | fdisk "${_DISK_IMAGE}" > /dev/null ||die "Could not set partition ${_NUMBER} type to ${_TYPE} on ${_DISK_IMAGE}"
	fi
}

# creating any number of GPT partitions
create_GPT_disk_image_with_partitions(){
	local _DISK_IMAGE="$1" ; shift
	local _DISK_SIZE_GB="$1" ; shift

	# fallocate -l "${_DISK_SIZE_GB}"G "${_DISK_IMAGE}"
	dd if=/dev/zero of="${_DISK_IMAGE}" bs=1G seek="${_DISK_SIZE_GB}" count=0 2>/dev/null || die "Could not allocate space for ${_DISK_IMAGE} with size ${_DISK_SIZE_GB} GB"
	# Create an empty GTP partition table:
	echo "
g
w" | fdisk "${_DISK_IMAGE}" > /dev/null ||die "Could not create an empty GPT partition table on ${_DISK_IMAGE} with size ${_DISK_SIZE_GB} GB"

	# Create partitions:
	local _NUMBER=1	# start with partition index 1
	while [ $# -gt 0 ] ; do
		local _START="$1" ; shift	# Unitsa are sectors. Empty means selecting the start sector by default at the earliest free part of the disk
		local _SIZE="$1" ; shift		# Units are sectors if no unit symbol (K,M,G,T,P) appended. Empty means selecting the size by default till the end of the current block of free space.
		local _TYPE="$1" ; shift		# 1=EFI. Empty means Linux Filesystem.

		make_GPT_partition "${_DISK_IMAGE}" "${_NUMBER}" "${_START}" "${_SIZE}" "${_TYPE}"
		_NUMBER=$((_NUMBER + 1))
	done
}

# TODO: does domU really need MBR?
# creating one MBR primary partition
create_MBR_disk_image_with_partition(){
	local _DISK_IMAGE="$1"
	local _DISK_SIZE_MB="$2"

	# fallocate -l "${_DISK_SIZE_MB}"M "${_DISK_IMAGE}"
	dd if=/dev/zero of="${_DISK_IMAGE}" bs=1M seek="${_DISK_SIZE_MB}" count=0 2>/dev/null || die "Could not allocate space for ${_DISK_IMAGE} with size ${_DISK_SIZE_MB} MB"
	# Create a single MBR primary partition:
	local _NEW_PARTITION=n
	local _PRIMARY=p
	local _PART_NUMBER=1
	local _START_SECT=""
	local _LAST_SECT=""
	local _WRITE_PARTITION_TABLE=w
	echo "
${_NEW_PARTITION}
${_PRIMARY}
${_PART_NUMBER}
${_START_SECT}
${_LAST_SECT}
${_WRITE_PARTITION_TABLE}" | fdisk "${_DISK_IMAGE}" > /dev/null ||die "Could not create an MBR partition table on ${_DISK_IMAGE} with size ${_DISK_SIZE_MB} MB"
}

create_image(){
	parse_parameters "$@"
	check_parameters

	mkdir -p "${OUTPUT_DIR}"
	local DISK_LOOP
	local DISK_IMAGE
	local BOOT_PARTITION_FILE=""
	local ROOTFS_PARTITION_FILE=""
	local -r CONST_EFI_SYSTEM_PARTITION_TYPE=1
	case "${IMAGE_TYPE}" in
		vca-disk)
			# support for blockio assumes two separate GPT partitions on one disk
			DISK_IMAGE="${OUTPUT_DIR}/${IMAGE_NAME}"
			create_GPT_disk_image_with_partitions "${DISK_IMAGE}" ${BLOCKIO_DISK_SIZE_GB} \
				"" ${CONST_EFI_SYSTEM_PARTITION_SIZE_MB}M ${CONST_EFI_SYSTEM_PARTITION_TYPE} \
				"" "" ""	# BLOCKIO_DISK_SIZE_GB can be empty (=neglecting shellcheck error SC2086)
			DISK_LOOP="$(sudo losetup --find --show --partscan "${DISK_IMAGE}")" || die "Could not set up a loop device for ${DISK_IMAGE}"

			BOOT_PARTITION_FILE="${DISK_LOOP}p1"
			ROOTFS_PARTITION_FILE="${DISK_LOOP}p2"
		;;
		domu)
			# support for blockio one GPT partition on disk
			DISK_IMAGE="${OUTPUT_DIR}/${IMAGE_NAME}"
			create_MBR_disk_image_with_partition "${DISK_IMAGE}" ${CONST_DOMU_DISK_SIZE_MB}

			DISK_LOOP="$(sudo losetup --find --show --partscan "${DISK_IMAGE}")" || die "Could not set up a loop device for ${DISK_IMAGE}"

			BOOT_PARTITION_FILE="${DISK_LOOP}p1"
			ROOTFS_PARTITION_FILE="${DISK_LOOP}p1"
		;;
		persistent-bm | volatile*)
			BOOT_PARTITION_FILE="${OUTPUT_DIR}/${IMAGE_NAME}"
			ROOTFS_PARTITION_FILE="$(mktemp --tmpdir rootfs_partition_file.XXX)"
		;;
		*)
			die "Error preparing partitions. Unsupported image type: ${IMAGE_TYPE}"
	esac

	create_rootfs "${ROOTFS_PARTITION_FILE}" || die "Could not create the root filesystem on ${ROOTFS_PARTITION_FILE}"
	local ROOTFS_PARTITION_DIR; ROOTFS_PARTITION_DIR="$(mktemp --tmpdir --directory rootfs_partition_dir.XXX)"
	# if [[ -n "${NOLOOP}" && "${IMAGE_TYPE}" == volatile* && "${IMAGE_TYPE}" != volatile-dom0 ]] ; then
	if [[ -n "${NOLOOP}" && "${IMAGE_TYPE}" == volatile* ]] ; then
		mount "${ROOTFS_PARTITION_FILE}" "${ROOTFS_PARTITION_DIR}"
		ln -s /lib/systemd/systemd "${ROOTFS_PARTITION_DIR}"/init
		umount "${ROOTFS_PARTITION_DIR}"
	fi

	INITRAMFS_FILE="$(mktemp --tmpdir vca_initramfs.img.XXX)"
	mount_and_run "-o rw" "${ROOTFS_PARTITION_FILE}" "${ROOTFS_PARTITION_DIR}" "root filesystem" \
		create_initramfs "${INITRAMFS_FILE}" "${ROOTFS_PARTITION_DIR}" "${ROOTFS_PARTITION_FILE}" || die "Could not create the initrd ${INITRAMFS_FILE} based on root filesystem ${ROOTFS_PARTITION_DIR} and root filsystem image ${ROOTFS_PARTITION_FILE}"

	mount_and_run "" "${ROOTFS_PARTITION_FILE}" "${ROOTFS_PARTITION_DIR}" "root filesystem" \
		create_boot_partition "${BOOT_PARTITION_FILE}" "${ROOTFS_PARTITION_DIR}" "${INITRAMFS_FILE}" || die "Could not create boot partition ${BOOT_PARTITION_FILE}"
	##copy rootfs image to the boot partition next to initrd:
	# mount "${BOOT_PARTITION_FILE}" "${ROOTFS_PARTITION_DIR}"
	# ls "${ROOTFS_PARTITION_DIR}"
	# cp "${ROOTFS_PARTITION_FILE}" "${ROOTFS_PARTITION_DIR}"
	# ls "${ROOTFS_PARTITION_DIR}"
	# umount "${ROOTFS_PARTITION_DIR}"

	/bin/rm "${INITRAMFS_FILE}"
	[[ "${IMAGE_TYPE}" == volatile* ]] && /bin/rm "${ROOTFS_PARTITION_FILE}"
	/bin/rm -r "${ROOTFS_PARTITION_DIR}"

	if [[ "${IMAGE_TYPE}" == vca-disk || "${IMAGE_TYPE}" == domu ]] ; then
		sudo losetup -d "${DISK_LOOP}"	# be kind and release the loop device; anyone can get another one anyway, anytime
		if [ "${UNCOMPRESSED_IMAGE}" == FALSE ] ; then
			gzip --force "${DISK_IMAGE}" || die "Could not compress file ${DISK_IMAGE}"
		fi
	fi
	return 0
}

stderr "Called as: $0 $*"
create_image "$@" && stderr "Finished: $0 $*"

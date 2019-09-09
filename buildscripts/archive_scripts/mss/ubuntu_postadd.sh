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
set -eu

STEP="$1"			; shift
PHASE="$1"			; shift
ARCHIVE_DIR="$1"	; shift # relative to CHROOT_DIR
SCRIPTS_DIR="$1"	; shift
CHROOT_DIR="$1"		; shift

. "${SCRIPTS_DIR}/library_image_creation.sh"

MSS_INSTALL_DIR="${ARCHIVE_DIR}/${_CONST_CUSTOM_DIR}"
CURR_KER_VER="$(cd "${CHROOT_DIR}"/lib/modules ; ls)" # there should only be one subdirectory in .../modules
CURR_OS_VER=$(lsb_release -rs)

# If needed, get the MSS verion from a file like: MssVersionFile_2017_16.5.2_PV3_<otherSymbols>_UBUNTU.quickbuild | awk -F_ '{ printf("%s_%s_%s",$4, $5, $6)}'). Such file has to be added to every MSS_INSTALL_DIR
case "${CURR_KER_VER}" in
	4.13.13*)	# MSS 2018R1
		FAKED_KER_VER="4.13.0-36-generic"
		FAKED_OS_VER="16.04.4"
	;;
	4.13.*)	# MSS 2017R3
		FAKED_KER_VER="4.10.0"
		FAKED_OS_VER="16.0.3"
	;;
	4.4.*)	# MSS 2017R3
		FAKED_KER_VER="4.4.0"
		FAKED_OS_VER="16.04"
	;;
	*)	# do not fake anything; unbelievable
		FAKED_KER_VER="${CURR_KER_VER}"
		FAKED_OS_VER="${CURR_OS_VER}"
	;;
esac
# enabing currently needed kernel using 4.4.0 sources in MSS; needed for MSS 2017R3; not necessary for 2018R1:
PARENT_DIR="/${MSS_INSTALL_DIR}"/Generic/lib/modules
[[ "${CURR_KER_VER}" == 4.4.* || "${CURR_KER_VER}" == 4.13.* ]] && ln -s "${PARENT_DIR}"/4.4.0-vpg20160125-gcc4.7.2/ "${PARENT_DIR}/${CURR_KER_VER}"-vpg20160125-gcc4.7.2 2>/dev/null || true

# link installed modules to mimic the modules of the faked kernel; needed for 2018R1:
[ "${CURR_KER_VER}" != "${FAKED_KER_VER}" ] && ln -s /lib/modules/"${CURR_KER_VER}" "${CHROOT_DIR}"/lib/modules/"${FAKED_KER_VER}" 2>/dev/null || true
ORIGINAL_UNAME="$(getOriginalOfFakedCommand "${CHROOT_DIR}"/bin/uname)"
[ "$( realpath "${CHROOT_DIR}" )" != "/" ] && ln -s /lib/modules/"${CURR_KER_VER}" "${CHROOT_DIR}"/lib/modules/"$( "${ORIGINAL_UNAME}" -r )" 2>/dev/null || true	# the currently booted kernel info leaks to the build process in chroot, despite faking uname and depmod in chroot. Produces "libkmod: ERROR ../libkmod/libkmod.c:586 kmod_search_moddep: could not open moddep file '/lib/modules/<host-kernel-version>/modules.dep.bin'"

# /usr/sbin/grub-mkconfig wants to create /boot/grub/grub.cfg.new in CHROOT:
mkdir -p "${CHROOT_DIR}"/boot/grub/

fakeDepmod "${CHROOT_DIR}" "" "${FAKED_KER_VER}"
fakeUname "${CHROOT_DIR}" "${FAKED_KER_VER}"
fakeLsb_release "${CHROOT_DIR}" UBUNTU "${FAKED_OS_VER}"

# disable the initrd update script "${CHROOT_DIR}"/backports-i915-driver/scripts/update-initramfs.sh, calling 'update-grub'. This script is called from:
#	CHROOT/backports-i915-driver/Makefile.real
# which is called from:
#	CHROOT/var/lib/dpkg/info/backports-i915-driver.postinst
# which is called (by means of  dpkg -i kmd_for_ubuntu.deb) from:
#	CHROOT/tmp/mss_mount_point.DUoWKh/mss/install.sh
fakeAndRegisterCommand "${CHROOT_DIR}"/usr/sbin/update-grub <<< "exit 0" >/dev/null

stderr "Configuring MSS in ${CHROOT_DIR}"
do_chroot "${CHROOT_DIR}" /bin/bash << EOF || die "Could not configure operating environment in ${CHROOT_DIR}"
	set -eu

case "${CURR_KER_VER}" in
	4.14*)	# MSS 2018R2
		cd "/${MSS_INSTALL_DIR}"
		./install_media.sh

		cd intel-opencl-16.9-00158
		mkdir /usr/local/lib64
		cp usr/local/lib64/* /usr/local/lib64/
		cp etc/ld.so.conf.d/libintelopencl.conf /etc/ld.so.conf.d/
		cp -r etc/OpenCL /etc/
	;;
	*)	# do not fake anything; unbelievable
		cd "/${MSS_INSTALL_DIR}"
		export OS=UBUNTU	# referenced in makefiles
		./install.sh <<< "y" >/dev/null	# "yes" to install firmware

		cp -rf "${ARCHIVE_DIR}/${_CONST_CUSTOM_DIR}/vainfo" /usr/bin/
	;;
esac
EOF

# cleanup after UKMD build:
/bin/rm -fr "${CHROOT_DIR}"/backports-i915-driver

removeIfSymlink "${CHROOT_DIR}"/lib/modules/"${FAKED_KER_VER}"
removeIfSymlink "${CHROOT_DIR}"/lib/modules/"$( "${ORIGINAL_UNAME}" -r )"

restoreFakedRegisteredCommands LASTONLY	# unfake update-grub
restoreFakedRegisteredCommands LASTONLY	# restore uname
restoreFakedRegisteredCommands LASTONLY	# restore lsb_release
unFakeDepmod "${CHROOT_DIR}" ""

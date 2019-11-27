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
CURR_OS_VER="$(chroot "${CHROOT_DIR}" /usr/bin/lsb_release -rs)"

# If needed, get the MSS verion from a file like: MssVersionFile_2017_16.5.2_PV3_<otherSymbols>_CENTOS.quickbuild | awk -F_ '{ printf("%s_%s_%s",$4, $5, $6)}'). Such file has to be added to every MSS_INSTALL_DIR
case "${CURR_KER_VER}" in
	3.10.0-693*)
		FAKED_KER_VER="3.10.0-693.el7.x86_64"
		FAKED_OS_VER="7.4.1708"
	;;
	4.4.*)	# the below is with the assumption that the kernel has been previously patched
		FAKED_KER_VER="3.10.0-229.el7.x86_64"
		FAKED_OS_VER="7.1.1503"
	;;
	*)	# do not fake anything; unbelievable
		FAKED_KER_VER="${CURR_KER_VER}"
		FAKED_OS_VER="${CURR_OS_VER}"
	;;
esac

# Only needed when this script is not run on node; this is to placate dracut, also needed for 2018R1:
ln -s /lib/modules/"${CURR_KER_VER}" "${CHROOT_DIR}"/lib/modules/"${FAKED_KER_VER}" || true

fakeDepmod "${CHROOT_DIR}" /usr "${FAKED_KER_VER}"
fakeAndRegisterCommand "${CHROOT_DIR}"/usr/bin/yum <<< "exit 0" >/dev/null
fakeUname "${CHROOT_DIR}" "${FAKED_KER_VER}"
fakeLsb_release "${CHROOT_DIR}" CENTOS "${FAKED_OS_VER}"

stderr "Configuring MSS in ${CHROOT_DIR}"
do_chroot "${CHROOT_DIR}" /bin/bash << EOF || die "Could not configure operating environment in ${CHROOT_DIR}"
	set -eu

	cd "/${MSS_INSTALL_DIR}"

	case ${FAKED_KER_VER} in
		3.10.0-229.el7.x86_64)
			cd Generic
			./install_media.sh <<< y > /dev/null
		;;
		3.10.0-693.el7.x86_64)	# CentOS 7.4, MSS2018R1

			ln -s /usr/src/kernels/"${CURR_KER_VER}" /usr/src/kernels/"${FAKED_KER_VER}"

			rpmbuild --rebuild ukmd-kmod-??.?-?????.el7.centos.src.rpm > /dev/null 2>&1 # ukmd-kmod-16.8-69021.el7.centos.src.rpm
			cp /root/rpmbuild/RPMS/x86_64/kmod-ukmd-??.?-?????.el7.centos.x86_64.rpm . # kmod-ukmd-16.8-69021.el7.centos.x86_64.rpm

			tar xzf install_scripts_centos_??.?-?????.tar.gz	# install_scripts_centos_16.8-69021.tar.gz
			./install_sdk_CentOS.sh <<< "1" # to select the option to use mss-specific repository configuration

			/bin/rm /usr/src/kernels/"${FAKED_KER_VER}"
		;;
		*)
			export OS=CENTOS	# referenced in makefiles
			./install.sh <<< "y" >/dev/null	# "yes" to "install firmwares for SKL?"
		;;
	esac
EOF

# cleanup after UKMD rpmbuild:
/bin/rm -fr "${CHROOT_DIR}"/root/rpmbuild 2>/dev/null || true

removeIfSymlink "${CHROOT_DIR}"/lib/modules/"${FAKED_KER_VER}"

restoreFakedRegisteredCommands LASTONLY	# restore lsb_release
restoreFakedRegisteredCommands LASTONLY	# restore uname
restoreFakedRegisteredCommands LASTONLY	# restore yum
unFakeDepmod "${CHROOT_DIR}" /usr

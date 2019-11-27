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
#CURR_OS_VER="$(chroot "${CHROOT_DIR}" /usr/bin/lsb_release -rs)"

# If needed, get the MSS verion from a file like: MssVersionFile_2017_16.5.2_PV3_<otherSymbols>_CENTOS.quickbuild | awk -F_ '{ printf("%s_%s_%s",$4, $5, $6)}'). Such file has to be added to every MSS_INSTALL_DIR


# Only needed when this script is not run on node; this is to placate dracut, also needed for 2018R1:
#ln -s /lib/modules/"${CURR_KER_VER}" "${CHROOT_DIR}"/lib/modules/"${FAKED_KER_VER}" || true

#fakeDepmod "${CHROOT_DIR}" /usr "${CURR_KER_VER}"
#fakeAndRegisterCommand "${CHROOT_DIR}"/usr/bin/yum <<< "exit 0" >/dev/null
#fakeUname / "${CURR_KER_VER}"
#fakeLsb_release "${CHROOT_DIR}" CENTOS "${FAKED_OS_VER}"

stderr "Configuring MSS in ${CHROOT_DIR}"
do_chroot "${CHROOT_DIR}" /bin/bash <<EOF || die "Could not configure operating environment in ${CHROOT_DIR}"
	set -u

	cd "/${MSS_INSTALL_DIR}"
	./install_sdk_CentOS.sh <<< "1" # to select the option to use mss-specific repository configuration
	rpm -i ukmd-kmod-16.8-69021.el7.centos.src.rpm
	cd /root/rpmbuild/SOURCES/
	tar -xf kmod-ukmd-16.9.tar.gz
	cd kmod-ukmd-16.9
	rm -f Module_centos7.4.symvers
	cp /usr/src/kernels/"${CURR_KER_VER}"/Module.symvers .
	mv Module.symvers Module_centos7.4.symvers
	make
	make install
	cp ../skl_dmc_ver1_27.bin /lib/firmware/i915/
	depmod -a "${CURR_KER_VER}"
	#mkinitrd --force /boot/vca_initramfs.img `uname -r`

EOF

# cleanup after UKMD rpmbuild:
/bin/rm -fr "${CHROOT_DIR}"/root/rpmbuild 2>/dev/null || true

#removeIfSymlink "${CHROOT_DIR}"/lib/modules/"${FAKED_KER_VER}"

#restoreFakedRegisteredCommands LASTONLY	# restore lsb_release
#restoreFakedRegisteredCommands LASTONLY	# restore uname
#restoreFakedRegisteredCommands LASTONLY	# restore yum
#unFakeDepmod "${CHROOT_DIR}" /usr

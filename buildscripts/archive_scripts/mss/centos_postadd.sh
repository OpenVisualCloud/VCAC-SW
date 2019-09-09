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
	# this step is a common for all activities with super-archives
	cd "/${MSS_INSTALL_DIR}"
	
	# depends on kernel version, different steps may be required
	case "${CURR_KER_VER}" in
		3.10.0*)
			rpm -Uvh libva-* intel-*
			rpm -ivh kmod-ukmd-16.9-00189.el7.centos.x86_64.rpm
			
			unlink /lib/modules/"${CURR_KER_VER}"/weak-updates/ukmd/drm.ko
			unlink /lib/modules/"${CURR_KER_VER}"/weak-updates/ukmd/drm_ukmd_compat.ko
			unlink /lib/modules/"${CURR_KER_VER}"/weak-updates/ukmd/drm_ukmd_kms_helper.ko
			unlink /lib/modules/"${CURR_KER_VER}"/weak-updates/ukmd/drm_ukmd.ko
			unlink /lib/modules/"${CURR_KER_VER}"/weak-updates/ukmd/i915.ko
			
			cp /lib/modules/3.10.0-693.17.1.el7.x86_64/extra/ukmd/* /lib/modules/"${CURR_KER_VER}"/weak-updates/ukmd/
			
			#cd /lib/modules/"${CURR_KER_VER}"/weak-updates/ukmd/
			#ln -s /lib/modules/"${CURR_KER_VER}"/extra/ukmd/drm.ko drm.ko
			#ln -s /lib/modules/"${CURR_KER_VER}"/extra/ukmd/drm_ukmd_compat.ko drm_ukmd_compat.ko
			#ln -s /lib/modules/"${CURR_KER_VER}"/extra/ukmd/drm_ukmd_kms_helper.ko drm_ukmd_kms_helper.ko
			#ln -s /lib/modules/"${CURR_KER_VER}"/extra/ukmd/drm_ukmd.ko drm_ukmd.ko
			#ln -s /lib/modules/"${CURR_KER_VER}"/extra/ukmd/i915.ko i915.ko
			
			depmod -a "${CURR_KER_VER}"
			
			rm -fr /lib/modules/3.10.0-693.17.1.el7.x86_64
		;;
		4.14.20*)
			# below installation script has been changed (install_media.sh) separately in provided "MSS_INSTALL_DIR" directory
			# changes I made:
			# 		extended if-else to use VCA kernel (originally, only one can pass script validation) as well
			# 		all "yes" questions will automatically selected to not stop the script and wait for input
			# this method should work for next MSS releases as well
			./install_media.sh
			rpm -i intel-opencl-16.9-*.x86_64.rpm
		;;
		*)
			die "Unsupported kernel (${CURR_KER_VER})"
	esac
EOF

# cleanup after UKMD rpmbuild:
/bin/rm -fr "${CHROOT_DIR}"/root/rpmbuild 2>/dev/null || true

#removeIfSymlink "${CHROOT_DIR}"/lib/modules/"${FAKED_KER_VER}"

#restoreFakedRegisteredCommands LASTONLY	# restore lsb_release
#restoreFakedRegisteredCommands LASTONLY	# restore uname
#restoreFakedRegisteredCommands LASTONLY	# restore yum
#unFakeDepmod "${CHROOT_DIR}" /usr

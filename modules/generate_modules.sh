#!/bin/bash -x
#
# Intel VCA Software Stack (VCASS)
#
# Copyright(c) 2017-2019 Intel Corporation.
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

set -euEo pipefail

function print_help {
	echo 'Synopsis:'
	echo 'OS={CENTOS|DEBIAN|UBUNTU} PKG_VER=<pkg version> [WORKSPACE=<intermediate and output dir>] [ VER_KERNEL=<kernel ver (without VCA subversion)> | LINUX_HEADERS_OR_KERNEL_DEVEL_PKG=<path of the src pkg>] ../ValleyVistaBuildScripts/quickbuild/generate_modules.sh'
	echo '	where:'
	echo '		LINUX_HEADERS_OR_KERNEL_DEVEL_PKG is the path of kernel-devel*VCA*.rpm (CentOS) or linux-headers*vca*.deb (Ubuntu).'
	echo '			The path is resolved from ValleyVistaModules/'
	echo 'Examples:'
	echo 'OS=UBUNTU PKG_VER=2.1.1 VER_KERNEL=4.4.0 ../ValleyVistaBuildScripts/quickbuild/generate_modules.sh'
	echo 'OS=UBUNTU PKG_VER=2.1.1 LINUX_HEADERS_OR_KERNEL_DEVEL_PKG=/tmp/output/rpmbuild/RPMS/x86_64/kernel-devel-4.16.18_3223_1.0.0.0.VCA-6.x86_64.rpm ../ValleyVistaBuildScripts/quickbuild/generate_modules.sh'
	echo 'OS=CENTOS PKG_VER=2.1.1 VER_KERNEL=4.13.0 ../ValleyVistaBuildScripts/quickbuild/generate_modules.sh'
	echo 'OS=CENTOS PKG_VER=2.1.1 LINUX_HEADERS_OR_KERNEL_DEVEL_PKG=../Kernel_v3.10.0-693.11.6.el7.2.2.16.VCA/CentOS_7.*/kernel-devel-*.rpm ../ValleyVistaBuildScripts/quickbuild/generate_modules.sh'
}

function die {
	rc=$?
	test $rc = 0 && rc=1
	echo -e "$@" >&2
	exit $rc
}

function verifyInputData {
	[[ "${OS}" != CENTOS && "${OS}" != UBUNTU && "${OS}" != DEBIAN ]] && die "Unsuported OS: ${OS}"
	[ -z "${PKG_VER}" ] && die "VCA package version not given"
	[[ -z "${VER_KERNEL:-}" && -z "${LINUX_HEADERS_OR_KERNEL_DEVEL_PKG:-}" ]] && die "Either location of kernel-devel/linux-headers package or kernel version (for finding kernel-devel/linux-headers package in hardcoded locations) must be given"
	true
}

function findMostRecentKernelDevel {
	local SEARCH_DIR="$1"
	local PATH1="$2"
	local PATH2="${3:-""}"

	# find the latest kernel-devel or linux-headers distribution package
	# (could be more than one on dev machine or due to QB issue)
	# printf format is "timestamp filepath"
	# declaring KERNEL_HDR_PKG separately from assigning to avoid masking return values:
	local KERNEL_HDR_PKG
	KERNEL_HDR_PKG="$(find "${SEARCH_DIR}" \( -path "${PATH1}" -o -path "${PATH2}" \) -printf '%T@ %p\n' \
		| sort -rn | cut -d' ' -f2-
	)"
	local NUM; NUM="$(wc -l <<< "${KERNEL_HDR_PKG}")"
	if [ 1 -ne "${NUM}" ] ; then
		echo -e "Exactly one kernel header package expected, got ${NUM} items:\n${KERNEL_HDR_PKG}" >&2
		KERNEL_HDR_PKG="$(head -1 <<< "${KERNEL_HDR_PKG}")"
		echo "Continuing with: '${KERNEL_HDR_PKG}'"  >&2
	fi
	echo "${KERNEL_HDR_PKG}"
}

function generate_modules_ubuntu {
	# extract kernel header pkg:
	dpkg -x "${LINUX_HEADERS_OR_KERNEL_DEVEL_PKG}" "${KERNEL_DEVEL}"
	local DIR_NAME; DIR_NAME="$( ls "${KERNEL_DEVEL}"/usr/src )"
	local KER_VER="${DIR_NAME#linux-headers-}"
	local KERNEL_SRC; KERNEL_SRC="$( ls -d "${KERNEL_DEVEL}"/usr/src/* )"

	# generate tar.gz with full source
	local _SRC_ARCHIVE_NAME=vcass-modules_"${KER_VER}_${PKG_VER}".tar.gz
	tar --exclude './.*' -zcf "${OUT_DIR}/${_SRC_ARCHIVE_NAME}" . || die 'Could not make source archive ${OUT_DIR}/${_SRC_ARCHIVE_NAME}'

	# actual build
	sudo mkdir -p "/lib/modules/${KER_VER}"	# otherwise make below needs root privileges

	make -j "$(nproc)" OS=UBUNTU VCASS_BUILDNO="${PKG_VER}" VCA_CARD_ARCH=l1om KERNEL_VERSION="${KER_VER}" KERNEL_SRC="${KERNEL_SRC}" \
		|| die 'Error executing make'
	# fake the installation and create a Debian (-D) software distribution package instead:
	checkinstall -D --nodoc --install=no --fstrans=yes --default \
		--deldesc=yes --backup=no \
		--pkgname="vcass-modules-${KER_VER}" \
		--pkgversion="$PKG_VER" \
		--pakdir="${OUT_DIR}" \
		--requires="linux-image-${KER_VER}" \
		make install VCA_CARD_ARCH=l1om KERNEL_VERSION="${KER_VER}" KERNEL_SRC="${KERNEL_SRC}" KERNELRELEASE="$(ls ${KERNEL_DEVEL}/lib/modules)" \
			|| die 'Make install failed'
	# transfer any left Debian files to the directory expected by QB:
	mv *.deb *.changes *.tar.xz *.dsc *.tar.gz "${OUT_DIR}" || true
}

function generate_modules_centos {
	# extract kernel headers pkg:
	rpm2cpio "${LINUX_HEADERS_OR_KERNEL_DEVEL_PKG}" | ( cd "${KERNEL_DEVEL}" ; cpio -idm )
	local KER_VER; KER_VER="$( ls "${KERNEL_DEVEL}"/usr/src/kernels )"
	local KERNEL_SRC; KERNEL_SRC="$( ls -d "${KERNEL_DEVEL}"/usr/src/kernels/* )"

	make -j "$(nproc)" -f make_rpm.mk \
		HOME="${OUT_DIR}" \
		MIC_CARD_ARCH=l1om \
		KERNEL_SRC="${KERNEL_SRC}" \
		KERNEL_VERSION="${KER_VER}" \
		RPMVERSION="${PKG_VER}" \
		KERNELRELEASE="${KER_VER}" \
		|| die 'Make rpm failed'
}

echo 'Start building modules'
echo "OS: ${OS:=$(grep -o -e UBUNTU -e DEBIAN -m1 < /etc/os-release || echo CENTOS)}"
echo "PKG_VER: ${PKG_VER:=0.0.0}"
[ -n "${VER_KERNEL:-}" ] && echo "VER_KERNEL: ${VER_KERNEL}"
verifyInputData

: "${WORKSPACE="$(realpath ..)"}"	# quickbuild starts in ValleyVistaModules
: "${RPM_DIR=${WORKSPACE}}"
KERNEL_DEVEL="${WORKSPACE}/kernel-headers-extracted"
/bin/rm -fr "${KERNEL_DEVEL}"
mkdir -p "${KERNEL_DEVEL}"
OUT_DIR="${WORKSPACE}/output"
mkdir -p "${OUT_DIR}"
if [ -z "${LINUX_HEADERS_OR_KERNEL_DEVEL_PKG:-}" ] ; then
	case $OS in
		"UBUNTU"|"DEBIAN")
			PATH1="*/linux-headers-${VER_KERNEL}*_1.0_amd64.deb"
			PATH2="*/Kernel_v${VER_KERNEL}/Ubuntu_??.??*/linux-headers*.deb"
		;;
		"CENTOS")
			PATH1="*/rpmbuild/RPMS/x86_64/kernel-devel-*.rpm"
			PATH2="*/Kernel_v${VER_KERNEL}/CentOS_7.*/kernel-devel-*.rpm"
		;;
	esac
	LINUX_HEADERS_OR_KERNEL_DEVEL_PKG="$(findMostRecentKernelDevel "${RPM_DIR}" "${PATH1}" "${PATH2}" )"
else
	if [[ "$OS" = CENTOS && "${LINUX_HEADERS_OR_KERNEL_DEVEL_PKG##*/}" = kernel-headers-* ]]; then
		die "Possibly wrong LINUX_HEADERS_OR_KERNEL_DEVEL_PKG (=${LINUX_HEADERS_OR_KERNEL_DEVEL_PKG}), most probably you want to use kernel-devel-*.rpm"
	fi
fi
[ -f "${LINUX_HEADERS_OR_KERNEL_DEVEL_PKG}" ] || die "Kernel headers package file: '${LINUX_HEADERS_OR_KERNEL_DEVEL_PKG}' does not exist"
echo "LINUX_HEADERS_OR_KERNEL_DEVEL_PKG: ${LINUX_HEADERS_OR_KERNEL_DEVEL_PKG}"
case $OS in
	"UBUNTU"|"DEBIAN")
		generate_modules_ubuntu
	;;
	"CENTOS")
		generate_modules_centos
	;;
	*)
		print_help
		die 'Unsupported OS type'
	;;
esac

echo "Build has ended with status: $?"

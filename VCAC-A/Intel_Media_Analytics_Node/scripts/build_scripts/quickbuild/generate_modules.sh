#!/bin/bash -x
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

set -euEo pipefail

function print_help {
	echo 'Help:'
	echo 'Supported OS: UBUNTU, CENTOS'
	echo 'Ex. OS=UBUNTU PKG_VER=2.1.1 KERNEL_NAME=4.4.0-1.2.1.1.vca KERNEL_PATH=/lib/modules/4.4.0-1.2.1.1.vca/build ./generate_modules.sh'
	echo 'Ex. OS=CENTOS PKG_VER=2.1.1 VER_KERNEL=3.10.0-693.11.6.el7.2.2.16.VCA  ./quickbuild/generate_modules.sh'
	echo 'Ex. OS=CENTOS PKG_VER=2.1.1  DEVEL_RPM=../Kernel_v3.10.0-693.11.6.el7.2.2.16.VCA/CentOS_7.*/kernel-devel-*.rpm  ./quickbuild/generate_modules.sh'
}

function die {
	rc=$?
	test $rc = 0 && rc=1
	echo -e "$@" >&2
	exit $rc
}

function generate_modules_ubuntu {
	# generate tar.gz with full source
	local _SRC_ARCHIVE_NAME=vca_modules_"${KERNEL_NAME}_${PKG_VER}"_src.tar.gz
	tar --exclude './.*' -zcf "../${_SRC_ARCHIVE_NAME}" . || die 'Could not make sources archive'
	mv "../${_SRC_ARCHIVE_NAME}" .

	# actual build
        OS=UBUNTU make VCASS_BUILDNO="${PKG_VER}" VCA_CARD_ARCH=l1om KERNEL_VERSION="$KERNEL_NAME" KERNEL_SRC="$KERNEL_PATH" || die 'Error executing make'
        sudo checkinstall -D --nodoc --install=no --default \
		--deldesc=yes --backup=no \
		--pkgname='vcass-modules' \
		--pkgversion="$PKG_VER" \
		make install \
		VCA_CARD_ARCH=l1om \
		KERNEL_VERSION="$KERNEL_NAME" \
		KERNEL_SRC="$KERNEL_PATH" \
		|| die 'Make install failed'
}

function generate_modules_centos {
	cd ..
	WORKSPACE_DIR=$(pwd)
	cd "$WORKSPACE_DIR"/ValleyVistaKernel
	make modules_prepare || die 'Make modules_prepare failed'

	cd "$WORKSPACE_DIR"
	/bin/rm -fr kernel-devel || true
	mkdir kernel-devel
	cd kernel-devel
	DEVEL_RPM="${DEVEL_RPM:-"${WORKSPACE_DIR}/Kernel_v${VER_KERNEL}"/CentOS_7.*/kernel-devel-*.rpm}"
	test -f ${DEVEL_RPM} || die 'Invalid path to kernel-devel RPM'
	rpm2cpio ${DEVEL_RPM} | cpio -idmv

	VERSION=$(ls usr/src/kernels)
	cd "$WORKSPACE_DIR"/ValleyVistaModules
	OUT_DIR="$(pwd)/../output"
	mkdir -p "${OUT_DIR}"
	make -j "$(nproc)" -f make_rpm.mk \
		HOME="${OUT_DIR}" \
		MIC_CARD_ARCH=l1om \
		KERNEL_SRC="$WORKSPACE_DIR"/kernel-devel/usr/src/kernels/"$VERSION" \
		KERNEL_VERSION="$VERSION" \
		RPMVERSION="$PKG_VER" \
		|| die 'Make rpm failed'
}

echo 'Start building modules'
echo "OS: ${OS:=UBUNTU}"
echo "PKG_VER: ${PKG_VER:=0.0.0}"

case $OS in
"UBUNTU"|"DEBIAN")
	echo "KERNEL_NAME: $KERNEL_NAME"
	echo "KERNEL_PATH: $KERNEL_PATH"
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

echo 'Build has ended with status: SUCCESSFUL'

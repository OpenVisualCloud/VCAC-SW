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

function print_usage {
	echo 'Example usage:'
	echo 'OS=UBUNTU KER_VER=2.0.0 ./generate_kernel.sh'
	echo 'OS=CENTOS KER_VER=3.10.0-693 ../ValleyVistaBuildScripts/quickbuild/generate_kernel.sh'
	echo 'PKG_VER=2.3.4 OS=CENTOS KER_VER=3.10.0-693 ../ValleyVistaBuildScripts/quickbuild/generate_kernel.sh'
	echo 'Supported OS: UBUNTU, DEBIAN, CENTOS'
}

function die {
	rc=$?
	test $rc = 0 && rc=99
	echo -e "$@" >&2
	exit $rc
}

function generate_k310_centos {
	local _SRPM_NAME="$1"
	local _PKG_VER="$2"
	test -d .git || die 'did you checkout git repo? .git directory not found!'
	KERNEL_BASE_COMMIT=$(get_kernel_base)
	KERNEL_BASE_COMMIT="42e4f05" 

	cd .. # go to toplevel dir, quickbuild starts in ValleyVistaKernel
	WORKSPACE="$(pwd)"
	: "${SRPM_DIR:=/nfs/igk/disks/igk_valley_vista001/toolchain}"

	cp "$SRPM_DIR/${_SRPM_NAME}" "$WORKSPACE"
	OUT_DIR="$WORKSPACE/output"
	mkdir -p "$OUT_DIR"

	# set HOME to force working dir of rpmbuild
	HOME="$OUT_DIR" \
	ValleyVistaTools/KernelBuildScript/vca_kernel_build.sh \
		-g "$WORKSPACE"/ValleyVistaKernel \
		-c "$KERNEL_BASE_COMMIT" \
		-s "$WORKSPACE/${_SRPM_NAME}" \
		-v "${_PKG_VER}" \
		|| die 'vca_kernel_build.sh failed'
}





# this will just use Makefile provided with kernel and build following packages:
# kernel, kernel-devel, kernel-headers, kernel.src.rpm
#
# for simplicity (for us and perhaps also for client) patches are provided via zip
function generate_k3_10_106_centos {
	test -d .git || die 'did you checkout git repo? .git directory not found!'
	KERNEL_BASE_COMMIT=$(get_kernel_base)

	# quickbuild starts in ValleyVistaKernel
	WORKSPACE="$(pwd)"
	OUT_DIR="$WORKSPACE/../output"
	mkdir -p "$OUT_DIR"

	git format-patch "$KERNEL_BASE_COMMIT" || die 'could not format patches'
	zip "$OUT_DIR/vca_patches.zip" ./*\.patch || die 'could not make zip'

	# set HOME to force working dir of rpmbuild
	HOME="$OUT_DIR" \
	make -j "$(nproc)" \
		KERNELRELEASE="$KER_VER-1.$PKG_VER.VCA" \
		RPMVERSION="$PKG_VER" \
		LOCALVERSION='' \
		rpm-pkg \
		|| die 'make rpm-pkg failed'
}

function generate_k4_centos {
	cd .. # go to toplevel dir, quickbuild starts in ValleyVistaKernel
	WORKSPACE="$(pwd)"
	OUT_DIR="$WORKSPACE/output"
	mkdir -p "$OUT_DIR"
	cd "$WORKSPACE"/ValleyVistaKernel
	make -j "$(nproc)" rpm \
		HOME="$OUT_DIR" \
		KERNELRELEASE="$KER_VER-1.$PKG_VER.VCA" \
		LOCALVERSION= \
		RPMVERSION="$PKG_VER" \
		INSTALL_MOD_STRIP=1 \
		|| die 'Make rpm failed'
}

# according to https://wiki.ith.intel.com/pages/viewpage.action?pageId=823271743
# we mark vanilla kernel in repo (eg. with commit containing "Base Kernel",
# but it is not always the case)
# all descendant commits are treated as patches
# and applied over base source rpm by ValleyVistaTools/KernelBuildScript/vca_kernel_build.sh
function get_kernel_base {
	# base_sha1 should be commit marked as base kernel, so
	# it is the one with "Base Kernel" *if there is such commit*
	# otherwise it is last commit with word kernel (or with special string for debian)
	base_sha1=$(git log --oneline \
		| awk '/[Kk]ernel|Source code of Debian Linux 3\.16/ {print $1}
			/Apply (ubuntu )?patch 4\./ {print $1; exit 0}
			/Base Kernel/ {exit 0}' \
		| tail -n1)
	test -n "$base_sha1" \
		|| die "base kernel not found, current branch: $(git rev-parse --abbrev-ref HEAD)"
	echo "$base_sha1"
}


echo "Start building a kernel"
test -n "$KER_VER" || { print_help; die 'Kernel version is not set'; }
echo "OS: ${OS:=UBUNTU}"
echo "KER_VER: $KER_VER"
echo "PKG_VER: ${PKG_VER:=0.0.0}"

case "$OS" in
UBUNTU|DEBIAN)
	# generate tar.gz with full source
	_SRC_ARCHIVE_NAME=vca_kernel_"${KER_VER}_${PKG_VER}"_src.tar.gz
	tar --exclude './.*' -zcf "../${_SRC_ARCHIVE_NAME}" . || die 'Could not make sources archive'

	# actual build
	make -j "$(nproc)" \
		KERNELRELEASE="$KER_VER-1.$PKG_VER.vca" \
		KDEB_PKGVERSION=1.0 \
		PKGVERSION="$PKG_VER" \
		deb-pkg OS="$OS" \
		INSTALL_MOD_STRIP=1 \
		LOCALVERSION='' \
		|| die 'Error while executing make cmd'

# generate archive with our patches for kernel
	test -d .git || die 'did you checkout git repo? .git directory not found!'
	KERNEL_BASE_COMMIT=$(get_kernel_base)
	git format-patch "$KERNEL_BASE_COMMIT" || die 'could not format patches'
	tar -zcf "../${_SRC_ARCHIVE_NAME/vca/vca_patches}" ./*\.patch || die 'Could not make patches archive'
;;

CENTOS)
	case "$KER_VER" in
	3.10.0)
		generate_k310_centos kernel-3.10.0-514.26.2.el7.src.rpm "${PKG_VER}"
	;;
	3.10.0-693)
		generate_k310_centos kernel-3.10.0-693.17.1.el7.src.rpm "${PKG_VER}.VCA"
	;;
	3.10.0-862)
		generate_k310_centos kernel-3.10.0-862.3.2.el7.src.rpm "${PKG_VER}"
	;;
	3.10.106)
		generate_k3_10_106_centos
	;;
	4.4*)
		generate_k4_centos
	;;
	*)
		die 'Unsupported kernel version'
	;;
	esac
;;
*)
	print_usage
	die 'Unsupported OS type'
;;
esac

echo 'Build has ended with status: SUCCESSFUL'

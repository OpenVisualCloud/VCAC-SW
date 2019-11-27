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

set -eEo pipefail

function print_help {
	echo 'Help:'
	echo 'Ex. OS=UBUNTU PKG_VER=2.0.0 ./generate_apps.sh'
	echo 'Supported OS: UBUNTU, CENTOS'
}

function die {
	rc=$?
	test $rc = 0 && rc=1
	echo -e "$@" >&2
	exit $rc
}

function gen_apps_ubuntu {
	# generate tar.gz with full source
	local _SRC_ARCHIVE_NAME=vca_apps_"${PKG_VER}"_src.tar.gz
	tar --exclude './.*' -zcf "../${_SRC_ARCHIVE_NAME}" . || die 'Could not make sources archive'
	mv "../${_SRC_ARCHIVE_NAME}" .

	# actual build
	cmake -DOS="${OS}" -DPKG_VER="${PKG_VER}" -DDESTDIR="" || die "Error executing cmake"
	cpack || die "Error making deb package"

	echo "Build has ended with status: SUCCESSFUL"
}

function gen_apps_centos {
	local OUT_DIR="$(pwd)/../output"
	mkdir -p "${OUT_DIR}"
	make -f make_rpm.mk		\
		OS="${OS}"				\
		PKG_VER="${PKG_VER}"	\
		HOME="${OUT_DIR}"		\
		MODULES_SRC=`pwd`/../ValleyVistaModules \
			|| die "Error executing make"
	echo "Build has ended with status: SUCCESSFUL"
}

# main:
if [ -z "$PKG_VER" ] ; then
	PKG_VER="0.0.0"
	echo "Set default version: "$PKG_VER
fi

if [ -z "$OS" ] ; then
        OS="CENTOS"
        echo "Set default OS:"$OS
fi

echo "Generate vca apps"
echo "PKG_VER:"$PKG_VER
echo "OS:"$OS

case $OS in
"UBUNTU"|"DEBIAN")
	gen_apps_ubuntu
    ;;
"CENTOS")
	gen_apps_centos
    ;;
*)
        print_help
	die 'Unsupported OS type'
    ;;
esac

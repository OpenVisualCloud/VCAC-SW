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
	echo 'Ex. OS=UBUNTU PKG_VER=2.0.0 ./generate_qemu.sh'
	echo 'Supported OS: UBUNTU'
}

function die {
	rc=$?
	test $rc = 0 && rc=99
	echo -e "$@" >&2
	exit $rc
}

function gen_qemu_ubuntu {
	PROCS=$(( $(nproc) * 2 ))
	./configure --prefix=/usr \
		--enable-kvm --disable-xen --enable-debug-info \
		--enable-debug --enable-sdl --enable-vhost-net \
		--enable-spice --disable-debug-tcg --target-list=x86_64-softmmu \
			|| die "Error when configuring qemu" \
	make -j "$PROCS" || die 'Error when making qemu'
	cd roms/seabios
	make -j "$PROCS" || die 'Error when making qemu bios'
	cd -
	sudo checkinstall -D --nodoc --install=no --default \
		--pkgname='vca-qemu' \
		--pkgversion="${PKG_VER#VcaQemu-}" \
		make -j "$PROCS" install \
		|| die 'Make install failed'
}

# main:
echo "Generate VcaQemu"
echo "OS: ${OS:=UBUNTU}"
echo "PKG_VER: ${PKG_VER:=0.0.0}"

case $OS in
UBUNTU)
	gen_qemu_ubuntu
	echo 'Build has ended with status: SUCCESSFUL'
;;
*)
	print_help
	die 'Unsupported OS type'
;;
esac

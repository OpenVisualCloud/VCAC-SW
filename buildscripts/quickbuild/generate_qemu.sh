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

set -eEou pipefail

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
	mkdir -p usr/bin
	cp roms/seabios/out/bios.bin usr/bin
	sudo checkinstall -D --nodoc --install=no --default \
		--include usr/bin/bios.bin \
		--pkgname="vca-qemu-${QEMU_VER}" \
		--pkgversion="${PKG_VER#VcaQemu-}" \
		make -j "$PROCS" install \
		|| die 'Make install failed'
		mv *.deb ..
}

function create_rpmbuild {
    if [ -z $1 ]; then
        return 1
    fi
    mkdir -p $1/BUILD >> /dev/null 2> /dev/null
    mkdir -p $1/BUILDROOT >> /dev/null 2> /dev/null
    mkdir -p $1/BUILT >> /dev/null 2> /dev/null
    mkdir -p $1/RPMS >> /dev/null 2> /dev/null
    mkdir -p $1/SOURCES >> /dev/null 2> /dev/null
    mkdir -p $1/SPECS >> /dev/null 2> /dev/null
    mkdir -p $1/SRPM >> /dev/null 2> /dev/null
    mkdir -p $1/SRPMS >> /dev/null 2> /dev/null
}

function create_tarball {
    dirname=vca-qemu-$1-${PKG_VER}
    mkdir ../$dirname
    cp -r * ../$dirname
    tar -czf vca-qemu-$1.tar.gz ../$dirname
    rm -rf ../$dirname
}

function gen_qemu_centos {
    RPMBUILD=$(pwd)"/../../output/rpmbuild"
    create_rpmbuild $RPMBUILD
    rm -rf $RPMBUILD/BUILD/vca-qemu*
    create_tarball $1
    mv *tar.gz $RPMBUILD/SOURCES

    rpmbuild -ba --define "_unpackaged_files_terminate_build 0" --define "_topdir $RPMBUILD" --define "_version ${PKG_VER}" make.spec \
    || die 'Create rpm failed'
    mv $RPMBUILD/RPMS/*/vca-qemu*.rpm ..
    mv $RPMBUILD/SRPMS/vca-qemu*.rpm ..
}

# main:
echo "Generate VcaQemu"
echo "OS: ${OS:=UBUNTU}"
echo "PKG_VER: ${PKG_VER:=0.0.0}"

for QEMU_VER in $(ls | grep -P "^v[0-9]+.[0-9]+[.0-9]*$")
do
    if [ -d $QEMU_VER ]
    then
        cd $QEMU_VER
        case $OS in
            UBUNTU)
                gen_qemu_ubuntu
                ;;
            CENTOS)
                gen_qemu_centos $QEMU_VER
                ;;
            *)
                print_help
                die 'Unsupported OS type'
                ;;
        esac
        echo "Build $QEMU_VER has ended with status: SUCCESSFUL"
        cd ..
    fi
done

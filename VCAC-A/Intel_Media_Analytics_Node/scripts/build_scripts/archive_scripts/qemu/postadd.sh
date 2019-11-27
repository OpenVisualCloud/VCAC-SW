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

echo "Configuring QEMU in ${CHROOT_DIR}" >&2

# the list of packeges must be quoted, so expansion of wildards first occurs in chroot:
do_dpkg "${CHROOT_DIR}" /"${ARCHIVE_DIR}/${_CONST_CUSTOM_DIR}/*.deb"

do_chroot "${CHROOT_DIR}" /bin/bash << EOF || die "Could not configure operating environment in ${CHROOT_DIR}"
		set -eu

		# TODO: Replace this risky replacement with creating a systemd service or at least a separate initd rc script
		# sed to replace the (hopefully) final 'exit 0' in /etc/rc.local with our code:
		sed -i -r -e 's/^exit 0$/brctl addbr vcabr0 \&\& \
			brctl addif vcabr0 eth0 \&\& \
			ip link set vcabr0 state up \&\& \
			dhclient -r eth0 \&\& \
			sleep 5 \&\& \
			ip addr flush dev eth0 \&\& \
			dhclient vcabr0 \&\& \
			ifconfig vcabr0 mtu 9000 \|\| echo "Error during adding bridge"/' \
			/etc/rc.local
		chmod +x /etc/rc.local

		# copying the Qemu Bios
		cp -f "/${ARCHIVE_DIR}/${_CONST_CUSTOM_DIR}"/bios.bin /usr/bin/bios.bin
EOF

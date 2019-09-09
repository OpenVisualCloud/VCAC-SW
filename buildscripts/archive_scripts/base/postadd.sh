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

CHROOT_FIRMWARE_DIR="${CHROOT_DIR}/lib/firmware/i915/"
mkdir -p "${CHROOT_FIRMWARE_DIR}"
cp "${SCRIPTS_DIR}/additional_binaries/skl_dmc_ver1_27.bin" "${CHROOT_FIRMWARE_DIR}"

echo "Configuring operating environment in ${CHROOT_DIR}" >&2
do_chroot "${CHROOT_DIR}" /bin/bash << EOF || die "Could not configure operating environment in ${CHROOT_DIR}"
	set -e

	if [ -d /etc/modprobe.d/ ] ; then
		echo blacklist e1000e > /etc/modprobe.d/blacklist-e1000e.conf
	else
		echo "Skipping modprobe configuration as /etc/modprobe.d/ does not exist (modprobe not installed?)" >&2
		exit 1
	fi
	if [ -f /etc/ssh/ssh_config ] ; then
		sed -i -r 's/^#(\s*PasswordAuthentication)/\1/' /etc/ssh/ssh_config
		sed -i -r 's/^#?(\s*PermitRootLogin)\s+\S+/\1 yes/' /etc/ssh/sshd_config
	else
		echo "Skipping SSHD configuration as /etc/ssh/ssh_config does not exist (SSHD not installed?)" >&2
		exit 2
	fi
EOF

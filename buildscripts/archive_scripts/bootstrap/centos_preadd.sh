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

DESCRIPTION="$(get_from_archive_info_file "${CHROOT_DIR}/${ARCHIVE_DIR}" "Target system distribution" )"

echo "Configuring operating environment in ${CHROOT_DIR}" >&2
do_chroot "${CHROOT_DIR}" /bin/bash << 'EOF' || die "Could not configure operating environment in ${CHROOT_DIR}"
	# enable tmpfs for /tmp
	systemctl enable tmp.mount

	# work around for poor key import UI in PackageKit
	rm -f /var/lib/rpm/__db*
	releasever=$(rpm -q --qf '%{version}\n' --whatprovides system-release)
	# basearch=$(uname -i)
	rpm --import /etc/pki/rpm-gpg/RPM-GPG-KEY-CentOS-$releasever #-$basearch
	# Note that running rpm recreates the rpm db files which aren't needed or wanted
	rm -f /var/lib/rpm/__db*

	# go ahead and pre-make the man -k cache (https://bugzilla.redhat.com/show_bug.cgi?id=455968)
	PATH="/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin" /usr/bin/mandb > /dev/null 2>&1

	# save a little bit of space at least...
	rm -f /boot/initramfs*
	# make sure there aren't core files lying around
	rm -f /core*

	# rebuild schema cache with any overrides we installed
	glib-compile-schemas /usr/share/glib-2.0/schemas

	echo "localhost" > /etc/hostname

	echo "Setup VCA card dependencies"
	/sbin/vca_setup.sh card

	#echo "Create symlink for console on USB"
	#ln -s /usr/lib/systemd/system/serial-getty\@service /etc/systemd/system/getty.target.wants/serial-getty\@ttyUSB0.service
	# Commented out the above as it causes:
	# Job dev-ttyUSB0.device/start timed out.
	# Timed out waiting for device dev-ttyUSB0.device.
	# Dependency failed for Serial Getty on ttyUSB0.
	# Job serial-getty@ttyUSB0.service/start failed with result 'dependency'.
	# Job dev-ttyUSB0.device/start failed with result 'timeout'.

	echo "Blacklisting e1000e module"
	echo "blacklist e1000e" >> /etc/modprobe.d/blacklist-e1000e.conf

	echo "Set NFS read and write sizes"
	echo "
rsize=32k
wsize=32k" >> /etc/nfsmount.conf

EOF

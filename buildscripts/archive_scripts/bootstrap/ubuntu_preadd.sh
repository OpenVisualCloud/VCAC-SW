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

DESCRIPTION="$(get_from_archive_info_file "${CHROOT_DIR}/${ARCHIVE_DIR}" "Target system version" )"

echo "Configuring operating environment in ${CHROOT_DIR}" >&2
CODENAME="$(get_codename "${DESCRIPTION}")"
IS_POINT_RELEASE=FALSE
isUbuntuPointRelease "${DESCRIPTION}" && IS_POINT_RELEASE=TRUE

do_chroot "${CHROOT_DIR}" /bin/bash << EOF || die "Could not configure operating environment in ${CHROOT_DIR}"
	set -e
	echo "localhost" > /etc/hostname
	echo -e "vista1\nvista1" | passwd 2> /dev/null
	echo "\
#------------------------------------------------------------------------------#
#                            PUBLIC UBUNTU REPOS                               #
#------------------------------------------------------------------------------#
###### Ubuntu Main Repos
deb     http://archive.ubuntu.com/ubuntu/ ${CODENAME} main restricted universe
deb-src http://archive.ubuntu.com/ubuntu/ ${CODENAME} main restricted universe

###### Important Security Updates (CODENAME-security).
# Patches for security vulnerabilities in Ubuntu packages. They are managed by the Ubuntu Security Team and are designed to change the behavior of the package as little as possible -- in fact, the minimum required to resolve the security problem. As a result, they tend to be very low-risk to apply and all users are urged to apply security updates.
deb     http://archive.ubuntu.com/ubuntu/ ${CODENAME}-security main restricted universe
deb-src http://archive.ubuntu.com/ubuntu/ ${CODENAME}-security main restricted universe
" > /etc/apt/sources.list

# if this is a point release to an LTS release:
if [ "${IS_POINT_RELEASE}" == TRUE ] ; then
	echo "\
###### Recommended Updates (CODENAME-updates).
# Updates for serious bugs in Ubuntu packaging that do not affect the security of the system.
deb     http://archive.ubuntu.com/ubuntu/ ${CODENAME}-updates  main restricted universe
deb-src http://archive.ubuntu.com/ubuntu/ ${CODENAME}-updates  main restricted universe
" >> /etc/apt/sources.list
fi
EOF

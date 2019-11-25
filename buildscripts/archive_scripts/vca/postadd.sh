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
set -euE

STEP="$1"			; shift
PHASE="$1"			; shift
ARCHIVE_DIR="$1"	; shift # relative to CHROOT_DIR
SCRIPTS_DIR="$1"	; shift
CHROOT_DIR="$1"		; shift

. "${SCRIPTS_DIR}/library_image_creation.sh"

echo "Configuring operating environment in ${CHROOT_DIR}" >&2
echo "#!/bin/bash
#
# Intel VCA Software Stack (VCASS)
#
# Copyright(c) 2015-2017 Intel Corporation.
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
# the file called \"COPYING\".
#
# Intel VCA Scripts.
#

export HOST_PORT='8086:2950'
export CARD_PORT='8086:2953'
export DMA_PORT='8086:2952'
export VCA0_DMA_BDF='01:00.3'
export VCA1_DMA_BDF='01:00.4'
export VCA_DRIVER_NAME='plx87xx'
export VCA0_CARD_IP='172.31.1.1'
export VCA0_HOST_IP='172.31.1.254'
export VCA1_CARD_IP='172.31.2.1'
export VCA1_HOST_IP='172.31.2.254'
export VCA_NET_MASK='255.255.255.0'
export VCA_ETH_RULE='/lib/udev/rules.d/97-eth_up.rules'
export VCA_MODPROBE_DEP_FILE='/etc/modprobe.d/vca.conf'
export VCA_ETH_SCRIPT='/sbin/vca_eth_cfg.sh'" > "${CHROOT_DIR}"/etc/vca_config

# the list of packeges must be quoted, so expansion of wildards first occurs in chroot:
do_dpkg "${CHROOT_DIR}" /"${ARCHIVE_DIR}/${_CONST_CUSTOM_DIR}/*.deb"

do_chroot "${CHROOT_DIR}" /bin/bash << EOF || die "Could not configure operating environment in ${CHROOT_DIR}"
		set -eu

		depmod -a "\$(ls /lib/modules/)" # this requires the kmod package to be installed
		/sbin/vca_setup.sh card			 # this requires /sbin/modprobe, /etc/modprobe.d/vca.conf, /etc/udev/rules.d/97-eth_up.rules
		cp /sbin/vca_* /usr/sbin/

		sed -i 's/#Storage=auto/Storage=persistent/g' /etc/systemd/journald.conf
EOF

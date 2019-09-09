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

VCA_CONFIG_DIR="/etc/vca_config.d"
VCA_UPDTE_CONFIG_SCRIPT="/usr/lib/vca/make_config.py"

VCA_OLD_CONF="vca_config.old_default.xml"
VCA_USER_CONF="vca_config.old_user.xml"
VCA_NEW_CONF="vca_config.new_default.xml"

function print_help() {
	$VCA_UPDTE_CONFIG_SCRIPT help
	exit 1
}


while [[ $# > 0 ]]
do
key="$1"
if [[ $key == "-h" || $key == "--help" || -z $2 ]]; then
	print_help
fi
case $key in
	-m|--mode)
	UPDATE_MODE="$2"
	shift
	;;
	*)
	print_help
	;;
esac
shift
done
if [ -z "$UPDATE_MODE" ]; then
	UPDATE_MODE=full-auto
fi

python $VCA_UPDTE_CONFIG_SCRIPT $VCA_CONFIG_DIR/$VCA_OLD_CONF $VCA_CONFIG_DIR/$VCA_USER_CONF $VCA_CONFIG_DIR/$VCA_NEW_CONF $UPDATE_MODE

EXIT_CODE=$?

if [ $EXIT_CODE == 0 ]; then
	echo "Update done"
	exit 0
fi

echo "Setting vcactl default configuration."
vcactl config-default

exit $EXIT_CODE

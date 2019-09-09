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
export EXESCRIPTDIR="/usr/lib/vca" # to avoid leading '/' in tar archive
export EXESCRIPT="kvmgtctl_node.sh"
export VCACTRL="vcactl"
export CMD=""

# Parse parameters passed to the script. All parameters used by kvmsetup.sh must
# be defined here, even if are not used by this script

function print_help {
	# Keep parameters alphabetically sorted in help and in the 'case' switch which parses them.
	# Prefer single-character options, i.e. do not introduce long options without an apparent reason.
	echo -e "Help:
        -c <value>	set card id
        -m <value>	set vm id
        -n <value>	set node id
        -x
		driver		show available drivers
		force_kill	kill all qemu instances
                info		show info about frequency, available vm's
                remove_unused	tremove unused devices
                set_min_freq <value>
                set_max_freq <value>
		start <fullImgPath> <mountPoint>
                status		show running vm with mac,ip ToDo:name,CPU,memory"
}

parse_parameters () {
	[ $# == 0 ] || [ "$1" == "-h" ] || [ "$1" == "--help" ] && print_help && exit 0

	while [ "$1" != "" ]
	do
		case "$1" in
                        -c)
                                export CARD_ID=$2
                                shift; shift;;
			-m)
				export CMD="$CMD -m $2"
				shift; shift;;
			-n)
				export NODE_ID=$2
				shift; shift;;
			-x)
                                case "$2" in
					set_min_freq|set_max_freq)
						export CMD="$CMD $2 $3"
						shift; shift; shift;;
					start)
						BRIDGE_NAME=$(sed -re 's/.+bridge-interface: ([^ ]+).*/\1/' <<< $("$VCACTRL" config-show "$CARD_ID" "$NODE_ID" bridge-interface))
						NFS_ADDR=$(sed -re 's/.+inet (addr:|)([^ ]+).*/\2/' <<< $(ifconfig "$BRIDGE_NAME"))
						export CMD="$CMD $2 $3 $4 $NFS_ADDR"
						shift; shift; shift; shift;;
					*)
						export CMD="$CMD $2"
						shift; shift;;
				esac
				;;
			*)
				echo "unknown parameter '$1'"
				exit 1
				;;
		esac
	done
}

parse_parameters "$@"
CARD_ADDR=$("$VCACTRL" network ip "$CARD_ID" "$NODE_ID")

# Send script to card. Pass all parameters passed to this script and extend it with some calculated overrides
(cd ${EXESCRIPTDIR} ;\
	tar cf - $EXESCRIPT ) \
	| ssh root@"$CARD_ADDR" 'export D=`mktemp -d`;
	tar xf - -C $D;
	chmod +x $D/'$EXESCRIPT';
	$D/'"$EXESCRIPT $CMD"

#!/bin/bash
#
# Intel VCA Software Stack (VCASS)
#
# Copyright(c) 2019 Intel Corporation.
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

set -euEo pipefail
stderr(){
	echo "*** $*" >&2
}
die(){
	EXIT_CODE=$?
	EXIT_CODE=$((EXIT_CODE == 0 ? 99 : EXIT_CODE))
	stderr "FATAL ${FUNCNAME[1]}(): $*"
	exit ${EXIT_CODE}
}

help() {
stderr 'This script is intended to be called from generate_images.sh to perform initial
cleanup before actual build process in QB integration enviroment.
It removes <tmp-dir> content, unmounts things mounted in <tmp-dir> and removes <directory-to-remove>.
Parameters:
--dir-to-rm <directory-to-remove>, could be specified multiple times to remove more directories
--clear-tmp-dir <tmp-dir>, umount all things mounted inside <tmp-dir>, then remove content of <tmp-dir>'
exit 1
}

unset DIRS_TO_RM TMP_DIR
DIRS_TO_RM=()

parse_parameters(){
	while [ $# -gt 1 ] ; do
		case "${1}" in
		--dir-to-rm)     DIRS_TO_RM+=("${2}");;
		--clear-tmp-dir) TMP_DIR="${2}";;
		*) help;;
		esac
		shift 2
	done
	[ -n "${TMP_DIR-}" ] || [ ${#DIRS_TO_RM[@]} -gt 0 ] || help
}

clear_tmp_dir(){
	local _TMP_DIR; _TO_UMOUNT
	_TMP_DIR="$1"
	_TO_UMOUNT=($(awk '{ print $2 }' /proc/mounts | grep "^${_TMP_DIR}" | awk -F/ '{ printf "%d*%s\n" , NF, $0 }' | sort -rn | awk -F'*' '{ print $2 }'))
	[ ${#_TO_UMOUNT[@]} -gt 0 ] && {
		stderr "unmounting: ${_TO_UMOUNT[*]}"
		umount "${_TO_UMOUNT[@]}" 2>/dev/null || true
	}
	# clear everything in _TMP_DIR except krb5cc*:
	[ -n "${_TMP_DIR}" ] && {
		stderr "removing ${_TMP_DIR}"
		find "${_TMP_DIR}" -mindepth 1 -maxdepth 1 ! -name krb5cc\* -exec /bin/rm -fr {} 2>/dev/null \;
	}
}

main(){
	[ ${#DIRS_TO_RM[@]} -gt 0 ] && {
		stderr "removing ${DIRS_TO_RM[*]}"
		rm -rf "${DIRS_TO_RM[@]}"
	}

	[ -n "${TMP_DIR-}" ] && clear_tmp_dir "${TMP_DIR}"

	sudo losetup -D
}

parse_parameters "$@"
stderr "Called as: $0 $*"
main
stderr "Finished: $0 $*"

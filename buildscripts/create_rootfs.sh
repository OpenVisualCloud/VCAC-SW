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

readonly SCRIPT_DIR="$( cd "$(dirname "$0")" && pwd )"	# $0 works for more shells than  ${BASH_SOURCE[0]}
. "${SCRIPT_DIR}/library_image_creation.sh"

ARCHIVE_FILES_ARRAY=()	# array of archives with software packages to be added to the bootstrap; to be set from the parameters
BOOTSTRAP_FILE=""	# the archive containing the bootstrap
DESCRIPTION=""		# OS description
# TODO: what should happen if an archive (e.g. with VCA software) added in the middle of other archives installs a kernel with different version?
KER_VER=""			# kernel version
OUTPUT_DIR=""		# the directory housing the final environment; to be set from the parameters

show_help() {
	# Keep parameters alphabetically sorted in help and in the 'case' switch which parses them.
	# Prefer single-character options, i.e. do not introduce long options without an apparent reason.
	echo "Usage: $0 [OPTIONS]
Creates a root file system utilizing regular VCA software archives and/or a bootstrap VCA archive.
This script requires root privileges and creates an operating environment that contains complete file and direcotry tree of an operating system.
The scripts main input is:
- The location of (possibly multiple) archives created with create_archive.sh. The archives can contain additional software packages to be added to the bootstrap, including the VCA modules package and the VCA kernel packages. The archives can also contain a custom file/directory with other data necessary for installation, and the pre- and post-installation scripts. A specific example of the custom directory is the bootstrap, i.e. a minimal operating environment, sufficient to add further software packages and to prepare the necessary configuration. The files from the other archive(s) are aplied to the unarchived bootstrap to form the final operating environment.
- The destination directory for the final operating environment.

Options:
-a, --archive <path>	The location of the archive to be added to the bootstrap. This option can be used multiple times. Archives are processed in the order in which they appear on the command line. To allow to satisfy the dependencies for packages within every archive, all dependent packages must be included in the same archive, or in one of the archives applied before.
-b, --bootstrap	<path>	The location of the bootstrap archive. This archive will be aplied first. This parameter is mandatory.
-d, --descr <description>	The OS version description, to fake the target environment.
-k, --kernel <version>	The VCA kernel version, to fake the target environment.
-h, --help	Show this help screen.
-o, --out-dir <path>	The destination directory for the final operating environment. This parameter is mandatory.
"
}

parse_parameters(){
	while [ $# -gt 0 ] ; do
		case "$1" in
			-a|--archive)
				ARCHIVE_FILES_ARRAY+=("${2:-""}")
				shift; shift;;
			-b|--bootstrap-dir)
				BOOTSTRAP_FILE="${2:-""}"
				shift; shift;;
			-d|--description)
				DESCRIPTION="${2:-""}"
				shift; shift;;
			-k|--kernel)
				KER_VER="${2:-""}"
				shift; shift;;
			-h|--help)
				show_help
				exit 0;;
			-o|--out-dir)
				OUTPUT_DIR="${2:-""}"
				shift; shift;;
			*)
				show_help && die "Unknown parameter '$1'"
		esac
	done
}

check_parameters () {
	[ -z "${BOOTSTRAP_FILE}" ] && show_help && die "No bootstrap file given"
	[ -n "${BOOTSTRAP_FILE}" ] && [ ! -f "${BOOTSTRAP_FILE}" ] && show_help && die "No bootstrap file ${BOOTSTRAP_FILE}"
	local _ARCHIVE
	[ ${#ARCHIVE_FILES_ARRAY[@]} -ne 0 ] && for _ARCHIVE in "${ARCHIVE_FILES_ARRAY[@]}" ; do
		[ ! -f "${_ARCHIVE}" ] && show_help && die "Archive file ${_ARCHIVE} not found"
	done

	[ -z "${OUTPUT_DIR}" ] && show_help && die "Indicating the output directory is mandatory"
	[ -n "${OUTPUT_DIR}" ] && [ -d "${OUTPUT_DIR}" ] && ! isEmptyDir "${OUTPUT_DIR}" && show_help && die "Output directory ${OUTPUT_DIR} already exists and is not empty"
	return 0
}

add_archive(){
	local _ARCHIVE="$1"
	local _ARCHIVE_TYPE="$2"	# value BOOTSTRAP, or other (e.g. NotBOOTSTRAP)

	local _MKTEMP_DIR=""
	if [ "${_ARCHIVE_TYPE}" == BOOTSTRAP ] ; then
		stderr "*** Extracting bootstrap archive ${_ARCHIVE} into ${OUTPUT_DIR}"
		_MKTEMP_DIR="${OUTPUT_DIR}"	# Make directory in the root of the chroot, as no directory tree exists below yet
	else
		stderr "*** Applying archive ${_ARCHIVE} to the environment in ${OUTPUT_DIR}"
		_MKTEMP_DIR="${OUTPUT_DIR}"/tmp	# Make directory in the directory /tmp in chroot
	fi
	[ ! -e "${_MKTEMP_DIR}" ] && { mkdir "${_MKTEMP_DIR}" || die "Direcotry '${_MKTEMP_DIR}' does not exist and could not be created" ; }
	local _ARCH_SHORT; _ARCH_SHORT="$( basename -- "${_ARCHIVE}" )"
	local _UNTAR_DIR; _UNTAR_DIR="$( mktemp --directory --tmpdir="${_MKTEMP_DIR}" "archive_${_ARCH_SHORT}".XXX )"
	_UNTAR_DIR=/"$( realpath --relative-to="${OUTPUT_DIR}" "${_UNTAR_DIR}" )"

	extract_archive "${_ARCHIVE}" "${OUTPUT_DIR}/${_UNTAR_DIR}" || die "Could not extract archive ${_ARCHIVE} to ${OUTPUT_DIR}/${_UNTAR_DIR}"

	# Moving the bootstrap rootfs out of archive in _UNTAR_DIR and make the _UNTAR_DIR one of rootfs' subdirectories:
	if [ "${_ARCHIVE_TYPE}" == BOOTSTRAP ] ; then
		# swap ${_UNTAR_DIR} and ${_CONST_CUSTOM_DIR}=rootfs:
		swapParentAndChildDir "${OUTPUT_DIR}/${_UNTAR_DIR}" "${_CONST_CUSTOM_DIR}"
		# move ${_CONST_CUSTOM_DIR}=rootfs up to ${OUTPUT_DIR}:
		replaceParentWithChildDirContent "${OUTPUT_DIR}" "${_CONST_CUSTOM_DIR}"
		# Move the _UNTAR_DIR to _CONST_CUSTOM_DIR/tmp/ and update the variable _UNTAR_DIR accordingly:
		mv "${OUTPUT_DIR}/${_UNTAR_DIR}" "${OUTPUT_DIR}/tmp/${_UNTAR_DIR}"
		_UNTAR_DIR="/tmp/${_UNTAR_DIR}"	# relative to OUTPUT_DIR
	fi

	# Fake the environment:
	local _KERNEL_INF; _KERNEL_INF="$( get_from_archive_info_file "${OUTPUT_DIR}/${_UNTAR_DIR}" "Target system kernel" )"
	if [ -n "${KER_VER}" ] ; then
		[ "${_KERNEL_INF}" != "${KER_VER}" ] && notice "Target kernel version mismatch between the archive (${_KERNEL_INF}), and the current command-line (${KER_VER}). Continuing with '${KER_VER}'"
		_KERNEL_INF="${KER_VER}"
	fi
	fakeUname "${OUTPUT_DIR}" "${_KERNEL_INF}"
	local _DIST_INF; _DIST_INF="$( get_from_archive_info_file "${OUTPUT_DIR}/${_UNTAR_DIR}" "Target system distribution" )"
	local _VER_INF; _VER_INF="$( get_from_archive_info_file "${OUTPUT_DIR}/${_UNTAR_DIR}" "Target system version" )"
	if [ -n "${DESCRIPTION}" ] ; then
		local _DIST_CMD; _DIST_CMD="$( cut -d" " -f1 <<< "${DESCRIPTION}" )"
		[ "${_DIST_INF}" != "${_DIST_CMD}" ] && notice "Target distribution name mismatch between the archive (${_DIST_INF}), and the current command-line (${_DIST_CMD}). Continuing with '${_DIST_CMD}'"
		_DIST_INF="${_DIST_CMD}"
		local _VER_CMD; _VER_CMD="$( cut -d" " -f2 <<< "${DESCRIPTION}" )"
		[ "${_VER_INF}" != "${_VER_CMD}" ] && notice "Target distribution version mismatch between the archive (${_VER_INF}), and the current command-line (${_VER_CMD}). Continuing with '${_VER_CMD}'"
		_VER_INF="${_VER_CMD}"
	fi
	[ -x "${OUTPUT_DIR}"/usr/bin/lsb_release ] && fakeLsb_release "${OUTPUT_DIR}" "${_DIST_INF}" "${_VER_INF}"
	# The PRE/POST scripts should be run even if there are no packages to be added:
	run PRE TARGET "${_UNTAR_DIR}" "${SCRIPT_DIR}" "${OUTPUT_DIR}"

	# existence of apt-get in a correclty build archive indicates that some packages are to be added
	if [ -d "${OUTPUT_DIR}/${_UNTAR_DIR}/var/lib/apt" ] ; then
		# prepare apt-get package dependency database:
		/bin/rm -fr "${OUTPUT_DIR}/var/lib/apt"
		mv "${OUTPUT_DIR}/${_UNTAR_DIR}/var/lib/apt" "${OUTPUT_DIR}/var/lib/apt"

		# disable apt-get contacting any other package databases than the local one
		local _DISABLED_APT_SRC_LIST; _DISABLED_APT_SRC_LIST="$(mktemp --tmpdir="${OUTPUT_DIR}/etc/apt/" sources.list.XXX)"
		mv "${OUTPUT_DIR}/etc/apt/sources.list" "${_DISABLED_APT_SRC_LIST}"

		# add packages:
		# *.deb must be given with path to avoid treating as regex
		local _HARMLESS="^Get:.*${_UNTAR_DIR}\|^Preparing to unpack \|^Unpacking \|^Processing triggers for \|^Setting up \|^Selecting previously unselected package\|^(Reading database ... \|^E: Getting name for slave of master fd "

		pushd "${OUTPUT_DIR}"		> /dev/null	# to get relative package paths below
		add_packages "${OUTPUT_DIR}" \
				"${_HARMLESS}"	\
				NOUPDATE	\
				CLEAN	\
				"${_UNTAR_DIR}/archives/*"
		popd				> /dev/null

		mv "${_DISABLED_APT_SRC_LIST}" "${OUTPUT_DIR}/etc/apt/sources.list"
	fi

	# The PRE/POST scripts should be run even if there are no packages to be added:
	run POST TARGET "${_UNTAR_DIR}" "${SCRIPT_DIR}" "${OUTPUT_DIR}"
	restoreFakedRegisteredCommands

	/bin/rm -fr "${OUTPUT_DIR:?}/${_UNTAR_DIR:?}"
	return 0
}

create_rootfs() {
	parse_parameters "$@"
	check_parameters

	mkdir -p "${OUTPUT_DIR}"
	add_archive "${BOOTSTRAP_FILE}" BOOTSTRAP

	local _ARCHIVE=""
	[ ${#ARCHIVE_FILES_ARRAY[@]} -ne 0 ] && for _ARCHIVE in "${ARCHIVE_FILES_ARRAY[@]}" ; do
		add_archive "${_ARCHIVE}" NotBOOTSTRAP
	done
	return 0
}

stderr "Called as: $0 $*"
create_rootfs "$@" && stderr "Finished: $0 $*"

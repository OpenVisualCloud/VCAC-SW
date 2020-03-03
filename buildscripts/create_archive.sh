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

readonly SCRIPT_DIR="$( cd "$(dirname "$0")" && pwd )"	# $0 works for more shells than  ${BASH_SOURCE[0]}
. "${SCRIPT_DIR}/library_image_creation.sh"

ARCHIVE_FILE=""
AUTHOR=""
BOOTSTRAP=FALSE
CHROOT_DIR=""
CUSTOM_FILE_DIR=""
DESCRIPTION=""
KERNEL_VER=""
KS_FILE=""
PKG_LIST=()
PREADD_SCRIPT=""
POSTADD_SCRIPT=""
_REPO_VCA=""
_REPO_EXTRAS=""
_REPO_OS=""

# TODO: --post and --post should probably also get the following params: DESCR OS, KERNEL
show_help(){
	# Keep parameters alphabetically sorted in help and in the 'case' switch which parses them.
	# Prefer single-character options, i.e. do not introduce long options without an apparent reason.
	echo "Usage: $0 [OTHER OPTIONS] [-p <package_list>]
Creates a regular software archive or a bootstrap archive. The packages for the software archive can come from a local set of ${_CONST_PKG_EXT} package files, or from named packages that will be automatially downloaded from one of the official public repositories.
Creation of the regular software archive involves adding the packages into a temporary chroot environment containing a fully functional package management tools (eg. apt-get and dpkg), which mimics the target environment. This ensures that any dependencies or installation errors are caught at the stage of software archive creation.
Every time when creating an archive, all archives to be added later in the process of creating the root file system must be re-created if they already exist. This is to ensure that they have package dependency information not earlier than the packages in this archive.
Options:
-a <path>	The pathname of a regular or booststrap archive file to be created. A regular archive usually contains downloaded package files, package management database (e.g. apt and dpkg databases), and information on essential (not: all dependant) packages for which this archive has been created. A bootstrap archive contains an original Ubuntu bootstrap downloaded from a public repository for the OS version description given with '-d <descr>' or a boostrap CentOS OS created with live-cd tools.
-b		Create a boostrap archive (a regular archive is created by default).
-c <author>	The string indentifying the author of this archive. This parameter is mandatory.
-d <descr>	OS version description. Used to select the right rootfs creation method (for bootstrap archives) and to fake commands if the kernel version in the target environment differs (for regular archives). Example description strings are: \"CENTOS 7.4\", \"UBUNTU 16.04\", \"UBUNTU 16.04.3\", \"UBUNTU 16.10\" but it is worthless do distinguish between Ubuntu point releases for LTS (Long Term Support) versions. Each point release is merely a snapshot of updated packages in the LTS version at that time - the public repositories do not foresee to install other releases than 'base LTS' (e.g. 16.04) or 'most recent point release' (currently: 16.04.4 for the 16.04).
-e <path>	The path to a repository containing optional software package repository, with pakages to be added to the CentOS root filesystem (neglected when creating Ubuntu archives).
-f <path>	The pathname of the kickstart file used for creation of CentOS root filesystem (neglected when creating Ubuntu archives).
-g <path>	The pathname to the custom file or directory tree to be included into the archive. This file/directory will be included into the archive without any other processing during creation nor during extraction, except by the <pre-add_script> or <post-add_script>, if provided.
-h		Displays this help page.
-k <version>	The VCA kernel version, to fake the target environment.
-o <path>	The path to the OS repository containing OS official distribution software packages to be added to the CentOS root filesystem (neglected when creating Ubuntu archives).
-p <package_list>	List of essential packages for which this archive is created. Dependant packages do not need to be specified, as they will be automatically determined, downloaded, and installed into the chroot environment. This must always be the last option specified.
--post <post-add_script>
--pre <pre-add_script>	The name of the shell script to be run after/before adding packages from this archive. The scritp will be renamed to ${_CONST_ADD_SCRIPTS["POST"]}/${_CONST_ADD_SCRIPTS["PRE"]} in the archive and will be interpreted by /bin/bash with the following command-line parameters:
	* A string indicating wheter the current execution environment is the build environment (BUILD) or  the target environment (TARGET).
	* A string indicating whether the script is called as a pre-add (PRE) or a post-add (POST) script.
	* The <root_dir>-based path to the unpacked archive.
	* The fully-qualified path to this build script's directory (dirname $0), containing shell script libraries with potentially usable functions.
	* The fully qualified path to the root of the rootfs filesystem (<root_dir>) being created at the time the packages from the archive are added.
The --post and --pre parameters are optional.
-r <root_dir>	The root directory of the chroot environment containing fully functional package management tools. The archive will be created only from new packages selected and installed in this environment, so it should already contain all packages which are required, but which are NOT to be included in the resulting archive. In case of creating the bootstrap archive, this drectory is used as a temporary bootstrap directory, if given.
-v <path>		The path to the VCA build repository containing VCA software packages to be added to the CentOS root filesystem (neglected when creating Ubuntu archives).

Options '-d' and '-p' are mutually exclusive.
Options '-d' and '-g' are mutually exclusive.

Option '-p' must be the last option, if used.
"
}

parse_parameters(){
	while [ $# -gt 0 ] ; do
		case "$1" in
			-a)
				ARCHIVE_FILE="${2:-""}"
				shift; shift;;
			-b)
				BOOTSTRAP=TRUE
				shift;;
			-c)
				AUTHOR="${2:-""}"
				shift; shift;;
			-d)
				DESCRIPTION="${2:-""}"
				shift; shift;;
			-e)
				_REPO_EXTRAS="${2:-""}"
				shift; shift;;
			-f)
				KS_FILE="${2:-""}"
				shift; shift;;
			-g)
				CUSTOM_FILE_DIR="${2:-""}"
				shift; shift;;
			-k)
				KERNEL_VER="${2:-""}"
				shift; shift;;
			-h)
				show_help
				exit 0;;
			-o)
				_REPO_OS="${2:-""}"
				shift; shift;;
			-p)
				shift || die "Argument for -p missing!"
				PKG_LIST=("${@}")
				return 0;;	# this must be the last parameter
			--post)
				POSTADD_SCRIPT="${2:-""}"
				shift; shift;;
			--pre)
				PREADD_SCRIPT="${2:-""}"
				shift; shift;;
			-r)
				CHROOT_DIR="${2:-""}"
				shift; shift;;
			-v)
				_REPO_VCA="${2:-""}"
				shift; shift;;
			*)
				show_help && die "Unknown parameter '$1'"
		esac
	done
}

# output to stdout the currently installed kernel version, or an empty string:
get_kernel_ver(){
	local _CHROOT_DIR="$1"
	ls "${CHROOT_DIR}"/lib/modules 2>/dev/null
}

check_parameters () {
	[ -z "${ARCHIVE_FILE}" ] && show_help && die "Name of the archive file to be created is not given "
	[[ "${BOOTSTRAP}" == TRUE && -z "${DESCRIPTION}" ]] && show_help && die "Give the OS version description for a bootstrap archive"
	[ -z "${AUTHOR}" ]	&& show_help && die "Author string not given ( '-p' must be the last option, if used)"
	[ "${#PKG_LIST[@]}" == 0 ] && [ "${BOOTSTRAP}" == FALSE ] && [ -z "${CUSTOM_FILE_DIR}" ] && show_help && die "No bootstrap requested. No list of packages to be included into the archive given. No custom files to be included into the archive given. ( '-p' must be the last option, if used)"
	[ "${BOOTSTRAP}" == TRUE ] && [ -n "${CUSTOM_FILE_DIR}" ] && show_help && die "Custom file/dir can not be added to a bootstrap archive"

	[ -n "${ARCHIVE_FILE}" ] && [ -s "${ARCHIVE_FILE}" ] && show_help && die "Archive file ${ARCHIVE_FILE} already exists"
	[ "${BOOTSTRAP}" == FALSE ] && [ -z "${CHROOT_DIR}" ] && show_help && die "Root directory of the operating environment must be given for creating regular archives"
	[ "${BOOTSTRAP}" == FALSE ] && [ -n "${CHROOT_DIR}" ]	&& [ ! -d "${CHROOT_DIR}" ]	&& show_help && die "Root directory of the operating environment ${CHROOT_DIR} does not exist"
	[ -n "${CHROOT_DIR}" ]	&& [ "${BOOTSTRAP}" == TRUE ] && ! isEmptyDir "${CHROOT_DIR}" && show_help && die "Root directory ${CHROOT_DIR} for the bootstrap archive is not empty"
	[ -n "${CUSTOM_FILE_DIR}" ]	&& [ ! -e "${CUSTOM_FILE_DIR}" ] && show_help && die "Custom file/directory ${CUSTOM_FILE_DIR} does not exist"
	[ -n "${PREADD_SCRIPT}"  ]	&& [ ! -f "${PREADD_SCRIPT}"  ]	&& show_help && die "Pre-add script ${PREADD_SCRIPT} does not exist"
	[ -n "${POSTADD_SCRIPT}" ]	&& [ ! -f "${POSTADD_SCRIPT}" ]	&& show_help && die "Post-add script ${POSTADD_SCRIPT} does not exist"

	if [[ "${BOOTSTRAP}" == TRUE && "${DESCRIPTION}" == CENTOS* ]] ; then
		[ -z "${KS_FILE}" ]		&& show_help && die "Kickstart file not given for ${DESCRIPTION}"
		[ -z "${_REPO_OS}" ]	&& show_help && die "OS package repository path not given for ${DESCRIPTION}"
		[ -z "${_REPO_VCA}" ]	&& show_help && die "VCA package repository path not given for ${DESCRIPTION}"
	fi
	if [ "${BOOTSTRAP}" == FALSE ] ; then
		# Is kernel already installed?
		local _INSTALLED_KERNEL_VER; _INSTALLED_KERNEL_VER="$(get_kernel_ver "${CHROOT_DIR}")"
		# decide on kernel version:
		if [ -n "${_INSTALLED_KERNEL_VER}" ] ; then
			[ -n "${KERNEL_VER}" ] && [ "${KERNEL_VER}" != "${_INSTALLED_KERNEL_VER}" ] && notice "Target kernel mismatch between the environment in ${CHROOT_DIR} (${_INSTALLED_KERNEL_VER}), and the current command-line (${KERNEL_VER}). Continuing with '${KERNEL_VER}'"
			[ -z "${KERNEL_VER}" ] && KERNEL_VER="${_INSTALLED_KERNEL_VER}"
		else
			[ -z "${KERNEL_VER}" ] && show_help && die "Target kernel version not given"
		fi
	else
		[ -z "${KERNEL_VER}" ] && show_help && die "Target kernel version not given"
	fi

	return 0
}

# Downloads packages and refreshes apt-get database and dpkg database
get_packages(){
	local _CHROOT_DIR="$1"
	shift
	local _PKG_LIST=("${@}")

	local _HARMLESS="^Get:\|^Hit:\|^Preparing to unpack \|^Unpacking \|^Processing triggers for \|^Setting up \|^Selecting previously unselected package\|^(Reading database ... \|^E: Getting name for slave of master fd \|^W: Can't drop privileges for downloading \|^W: Download is performed unsandboxed as root as file "
	# to avoid the message: "debconf: delaying package configuration, since apt-utils is not installed"
	add_packages "${_CHROOT_DIR}" \
				"${_HARMLESS}"	\
				UPDATE	\
				NOCLEAN	\
				apt-utils

	# to fill the apt-get cache with curretly downloaded *.deb package files:
	add_packages "${_CHROOT_DIR}" \
				"${_HARMLESS}"	\
				UPDATE	\
				CLEAN	\
				"${_PKG_LIST[@]}"
}

create_UBUNTU_bootstrap(){
	local _CHROOT_DIR="$1"

	local _UBUNTU_VERSION="${DESCRIPTION#* }"	#get a DESCRIPTION substring, from the end until space
	local _CODENAME="$(get_codename "${_UBUNTU_VERSION}")" ; [ -z "${_CODENAME}" ] && die "Could not get Ubuntu code name for description '${DESCRIPTION}'"	# declaring with assignment to mask return value (=neglecting shellcheck error SC2155)
	# debootstrap variants: --variant=minbase|buildd|fakechroot|scratchbox or just the Debian base when called without --variant=
	local _VARIANT="minbase"
	# Consider getting the newest debootstrap from http://archive.ubuntu.com/ubuntu/pool/main/d/debootstrap/?C=M;O=D  + ar  debootstrap_1.0.*_all.deb + tar xf data.tar.gz.
	# Calling debootstrap requires root privileges. Consider fakeroot fakeroot /polystrap: https://unix.stackexchange.com/a/214830
	${_CONST_SUDO} debootstrap --arch=amd64 --variant "${_VARIANT}" "${_CODENAME}" "${_CHROOT_DIR}" http://archive.ubuntu.com/ubuntu/ || die "Could not create Ubuntu bootstrap archive"
}

# returns path to the _IMG_CREATOR_ROOTFS_IMG image on which _CHROOT_DIR is mounted
# _IMG_CREATOR_ROOTFS_IMG is to be umounted & deleted by the caller when _CHROOT_DIR no longer needed
create_CENTOS_bootstrap(){
	local _CHROOT_DIR="$1"
	local _KS_FILE="$2"
	local _REPO_OS="$3"
	local _REPO_VCA="$4"
	local _REPO_EXTRAS="$5"

	# modify a copy of the original _KS_FILE:
	local _KS_TMP_FILE; _KS_TMP_FILE="$(mktemp --tmpdir ks_file.XXX)"
	cp -f "${_KS_FILE}" "${_KS_TMP_FILE}" || die "Could not copy ${_KS_FILE} to ${_KS_TMP_FILE}"
	sed -i "s|VCA_OS_REPO|${_REPO_OS}|g"			"${_KS_TMP_FILE}"
	sed -i "s|VCA_BUILD_REPO|${_REPO_VCA}|g"		"${_KS_TMP_FILE}"
	sed -i "s|VCA_EXTRAS_REPO|${_REPO_EXTRAS}|g"	"${_KS_TMP_FILE}"

	# Create rootfs image
	local _IMG_CREATOR_TMP_DIR; _IMG_CREATOR_TMP_DIR="$(mktemp --directory --tmpdir imgage_creator_tmp_dir.XXX)"
	local _IMG_CREATOR_ROOTFS_IMG; _IMG_CREATOR_ROOTFS_IMG="$(mktemp --tmpdir image_creator_rootfs_image.XXX)"

	# the image-creator only accepts _IMG_CREATOR_ROOTFS_IMG without path, and creates it in its current working directory:
	(
		cd "$(dirname -- "${_IMG_CREATOR_ROOTFS_IMG}")"
		image-creator --config "${_KS_TMP_FILE}" --name "$(basename -- "${_IMG_CREATOR_ROOTFS_IMG}")" --tmpdir "${_IMG_CREATOR_TMP_DIR}" --skip-compression --skip-minimize || die "Failed to create image with image-creator" # not necessary to compress or minimize this temporary image
	) || die "Image creator failed - correct the KickStart file (${_KS_FILE})?"
	# the image-creator unnecessarily appends a hardcoded ".img" to the name of _IMG_CREATOR_ROOTFS_IMG
	_IMG_CREATOR_ROOTFS_IMG="${_IMG_CREATOR_ROOTFS_IMG}".img
	# Transfer filesystem content from the rootfs image; it is unavoidable for later change of the structure of the archive directory tree; also valuable for the _CHROOT_DIR as the by-product of archive creation (to avoid the tight size of _IMG_CREATOR_ROOTFS_IMG):
	local _TEMP_ROOTFS_MOUNT_POINT; _TEMP_ROOTFS_MOUNT_POINT="$( mktemp --tmpdir --directory tmp_rootfs_mount_point.XXX )"
	mount "${_IMG_CREATOR_ROOTFS_IMG}" "${_TEMP_ROOTFS_MOUNT_POINT}"
	tar cf - -C "${_TEMP_ROOTFS_MOUNT_POINT}" . | tar xf - -C "${_CHROOT_DIR}"
	umount "${_IMG_CREATOR_ROOTFS_IMG}"
	/bin/rmdir "${_TEMP_ROOTFS_MOUNT_POINT}"

	# cleanup:
	/bin/rm "${_KS_TMP_FILE}"
	# according to stdout/stderr mesages from the above call to image-creator, _IMG_CREATOR_TMP_DIR is already unmounted here. But trying to remove the ${_IMG_CREATOR_TMP_DIR}/*/install_root/sys dir induces many error messages. Supressing this, as _IMG_CREATOR_TMP_DIR is anyway re-created with random name at every run:
	/bin/rm -rf "${_IMG_CREATOR_TMP_DIR}" > /dev/null 2>&1
	/bin/rm "${_IMG_CREATOR_ROOTFS_IMG}"
}

create_archive(){
	parse_parameters "$@"
	check_parameters

	local _TMP_ROOT_FS=tmp_root_fs	# The name must be unique within a (not-yet-existing...) freshly-installed rootfs to allow moving the bootstrap archive direcory up (later)
	local _CHROOT_DIR_PARENT=""
	if [ "${BOOTSTRAP}" == TRUE ] ; then	# start with bootstrap if its download requested
		# Only create CHROOT_DIR directory if not yet given. This creation only makes sense for Ubuntu (CentOS is centered on image), allowing the populated CHROOT_DIR rootfs directory to be the by-product of archive creation:
		local _CHROOT_DIR_DEFINED="${CHROOT_DIR}" # non-empty means CHROOT_DIR was defined
		_CHROOT_DIR_PARENT="${CHROOT_DIR:-$(mktemp --directory --tmpdir root_dir.XXX)}"
		${_CONST_SUDO} chmod a+rwx "${_CHROOT_DIR_PARENT}" #in case CHROOT_DIR is not user-writable
		CHROOT_DIR="${_CHROOT_DIR_PARENT}/${_TMP_ROOT_FS}"	# to enable moving the current CHROOT_DIR relative to _VIRTUAL_ARCHIVE for bootstrap archives, even if the original CHROOT_DIR is mounted on a device.
		( umask 0; mkdir -p "${CHROOT_DIR}" )

		fakeUname /  "${KERNEL_VER}"

		local _IMG_CREATOR_ROOTFS_IMG=""
		local _STATUS=0
		case "${DESCRIPTION}" in
			UBUNTU*)
				create_UBUNTU_bootstrap "${CHROOT_DIR}"
				;;
			CENTOS*)
				create_CENTOS_bootstrap "${CHROOT_DIR}" "${KS_FILE}" "${_REPO_OS}/BaseOS" "${_REPO_VCA}" "${_REPO_EXTRAS}"
				;;
			*)
				# cannot break here as environment has to be restored first
				warn "Unknown description: ${DESCRIPTION}"
				false
		esac  || _STATUS=$?

		restoreFakedRegisteredCommands

		[ "${_STATUS}" -ne 0 ] && die "Could not download or initialize the bootstrap. Check the distribution description: ${DESCRIPTION}."
	fi

	# From now on a non-empty ${CHROOT_DIR} exists for both bootstrap (just created above) and regular archives (passed as parameter)
	local _PKG_APT_DIR; _PKG_APT_DIR="${CHROOT_DIR}"/var/cache/apt/archives	# Ubuntu directory of packages downloaded by apt_get (in the subdirectory archives)
	local _PKG_DEB_DIR; _PKG_DEB_DIR="$(mktemp --directory --tmpdir="${CHROOT_DIR}"/tmp  package_file_dir.XXX )"	# directory of package files provided by the user (in the subdirectory archives)
	# bind-mounting (due to package files potentially dispersed over multiple directories) or hard-linkng (due to potential different filesystems) is not correct. Need to transfer package files (if any) to the directory _PKG_DEB_DIR accessible in ${CHROOT_DIR}; changing their names on the package list accordingly:
	local _NEW_PKG_LIST=""
	local _PKG
	[ "${#PKG_LIST[@]}" -ne 0 ] && for _PKG in "${PKG_LIST[@]}" ; do
		# TODO: this will accept names of non-existing files (even when given with paths) as package names. This is a place to check for existence of such files, caring about unexpanded wildards. The distinction between package name and file name shoud be done on the basis of path preceding the name. A path must contain a slash '/'
		if [ -f "${_PKG}" ] ; then
			PKG="$(readlink -f "${PKG}" )"	# canonicalize
			local _PKG_FILENAME; _PKG_FILENAME="$(basename -- "${_PKG}")"
			local _PKG_PATH; _PKG_PATH="$(dirname "${_PKG}")"
			mkdir -p "${_PKG_DEB_DIR}/${_PKG_PATH}"
			cp "${_PKG}" "${_PKG_DEB_DIR}/${_PKG_PATH}/${_PKG_FILENAME}"
			_NEW_PKG_LIST="${_NEW_PKG_LIST} ${_PKG_DEB_DIR}/${_PKG_PATH}/${_PKG_FILENAME}"
		else
			_NEW_PKG_LIST="${_NEW_PKG_LIST} ${_PKG}"
		fi
	done

	# At this point packages/dependencies are not downloaded yet, in bootstrap archives CUSTOM_FILE_DIR is empty; no package dir _PKG_SRC_DIR exists, the location of _APT_DB_FILENAME not known yet; only local package files may be available. This accounts for difference between BUILD and TAGET (PRE/POSTADD should check if it runs in BUILD or TARGET): e.g. tweaking the package files is imaginable during application of the SA, but not during its creation.

	# prepare a virtual structure named relative to CHROOT_DIR, identical to the archive format, for running the PREADD/POSTADD scripts:
	local _ARCH_SHORT; _ARCH_SHORT="$( basename -- "${ARCHIVE_FILE}" )"
	local _VIRTUAL_ARCHIVE; _VIRTUAL_ARCHIVE="$(${_CONST_SUDO} mktemp --directory --tmpdir="${CHROOT_DIR}" virtual_archive_"${_ARCH_SHORT}".XXX )"|| die "Could not create the directory for virtual archive in ${CHROOT_DIR}"
	${_CONST_SUDO} chmod a+rwx "${_VIRTUAL_ARCHIVE}"
	_VIRTUAL_ARCHIVE="$( basename -- "${_VIRTUAL_ARCHIVE}" )"
	# CUSTOM_FILE_DIR is empty for bootstrap archives:
	build_virtual_archive "${CHROOT_DIR}/${_VIRTUAL_ARCHIVE}" "" "" "" "${CUSTOM_FILE_DIR}" "${PREADD_SCRIPT}" "${POSTADD_SCRIPT}"

	if [ -n "${DESCRIPTION}" ] ; then
		create_info_file "${CHROOT_DIR}/${_VIRTUAL_ARCHIVE}" ${DESCRIPTION} "${KERNEL_VER}" "${AUTHOR}" "${PKG_LIST[@]:-}" # DESCRIPTION provides TWO parameters
	else
		if [ ! -x "${CHROOT_DIR}"/usr/bin/lsb_release ] ; then
			warn "Distribution information not available in the chroot environment '${CHROOT_DIR}' and not given on the command line. Assuming: ANY distribution name and ANY version"
			create_info_file "${CHROOT_DIR}/${_VIRTUAL_ARCHIVE}" ANY ANY "${KERNEL_VER}" "${AUTHOR}" "${PKG_LIST[@]:-}"
		else
			local _DIST="$( chroot "${CHROOT_DIR}" /usr/bin/lsb_release -is )"; [ -z "${_DIST}" ] && die "Could not get distribution name in chroot '${CHROOT_DIR}'"	# declaring with assignment to mask return value (=neglecting shellcheck error SC2155)
			_DIST="${_DIST^^}"	# uppercase
			local _VER="$( chroot "${CHROOT_DIR}" /usr/bin/lsb_release -rs )"; [ -z "${_VER}" ] && die "Could not get distribution version in chroot '${CHROOT_DIR}'"	# declaring with assignment to mask return value (=neglecting shellcheck error SC2155)
			create_info_file "${CHROOT_DIR}/${_VIRTUAL_ARCHIVE}" "${_DIST}" "${_VER}" "${KERNEL_VER}" "${AUTHOR}" "${PKG_LIST[@]:-}"
		fi
	fi

	# The PRE/POST scripts should be run even if there are no packages to be added:
	fakeUname "${CHROOT_DIR}" "${KERNEL_VER}"
	[[ -n "${DESCRIPTION}" && -x "${CHROOT_DIR}"/usr/bin/lsb_release ]] && fakeLsb_release "${CHROOT_DIR}" ${DESCRIPTION}	# DESCRIPTION provides TWO parameters to fakeLsb_release
	run PRE BUILD "${_VIRTUAL_ARCHIVE}" "${SCRIPT_DIR}" "${CHROOT_DIR}"

	if [[ "${BOOTSTRAP}" == TRUE && "${DESCRIPTION}" == UBUNTU* ]] ; then
		# After repositories got configured by the preadd script, continue with bootstrap package upgrade:
		# do a blind upgrade to the newest Ubuntu distribution if this is not the base (e.g. trying to upgrade to 16.04.3 can actually result in 16.04.400 if such version has been most recently released)
		isUbuntuPointRelease "${DESCRIPTION}" && get_packages "${CHROOT_DIR}" DIST_UPGRADE
	fi

	local _PKG_SRC_DIR=""	# if left empty, no packages to be incorporated into the archive
	if [ -n "${_NEW_PKG_LIST}" ] ; then
		# Package files in a local directory (e.g. *.deb files) can require dependencies to be downloaded from an Internet repository. So always calling get_packages in the hope that the package management tool (e.g.apt-get) will resolve this
		get_packages "${CHROOT_DIR}" "${_NEW_PKG_LIST}"
		# now uniting packages from potential two places: _PKG_DEB_DIR and - possible fresher - _PKG_APT_DIR
		# This is sub-optimal: e.g. if any package from the originally provided *.deb files has ben overriden by a newer downloaded one, both versions will be stored in the archive
		_PKG_SRC_DIR="$(mktemp --directory --tmpdir  package_source_dir.XXX )" # the directory being the source of packages for assemble_archive:
		# one of these may be empty:
		isEmptyDir "${_PKG_DEB_DIR}" || cp "${_PKG_DEB_DIR}"/* "${_PKG_SRC_DIR}"
		/bin/rm -fr "${_PKG_APT_DIR}"/partial 2> /dev/null || true	# only on Ubuntu; to avoid the copy to complain about a directory in the source
		isEmptyDir "${_PKG_APT_DIR}" || cp "${_PKG_APT_DIR}"/* "${_PKG_SRC_DIR}"

		# Update the virtual archive with package information:
		[ -n "${_NEW_PKG_LIST}" ] && build_virtual_archive "${CHROOT_DIR}/${_VIRTUAL_ARCHIVE}" "${_PKG_SRC_DIR}" "${CHROOT_DIR}" "var/lib/apt" "" "" ""
	fi
	/bin/rmdir "${_PKG_DEB_DIR}" || die "Something went wrong"

	# The PRE/POST scripts should be run even if there are no packages to be added:
	run POST BUILD "${_VIRTUAL_ARCHIVE}" "${SCRIPT_DIR}" "${CHROOT_DIR}"
	restoreFakedRegisteredCommands

	# After PREADD and POSTADD scripts have been run, CHROOT_DIR must me temporarily moved under _VIRTUAL_ARCHIVE, so the structure of virtual archive can be updated:
	if [ "${BOOTSTRAP}" == TRUE ] ; then
		# if this is a bootstrap archive: move _VIRTUAL_ARCHIVE out of $CHROOT_DIR in the filesystem hierarchy to make it the parent of CHROOT_DIR:
		swapParentAndChildDir "${CHROOT_DIR}" "${_VIRTUAL_ARCHIVE}" # temporarily moves 'basename CHROOT_DIR' to _CHROOT_DIR_PARENT; now CHROOT_DIR is rooted down where _VIRTUAL_ARCHIVE used to be; e.g. .../root_dir.XXX/${_TMP_ROOT_FS}/${_VIRTUAL_ARCHIVE} gets transformed to .../root_dir.XXX/${_VIRTUAL_ARCHIVE}/${_TMP_ROOT_FS}

		# add CHROOT_DIR as _CUSTOM_DIR to the archive:
		local _TMP_VIRTUAL_ARCHIVE; _TMP_VIRTUAL_ARCHIVE="$(dirname -- "${CHROOT_DIR}")/${_VIRTUAL_ARCHIVE}"
		build_virtual_archive "${_TMP_VIRTUAL_ARCHIVE}" "" "" "" "${_TMP_VIRTUAL_ARCHIVE}/${_TMP_ROOT_FS}" "" ""
		assemble_archive "${_TMP_VIRTUAL_ARCHIVE}" "${ARCHIVE_FILE}" || die "Could not assemble archive '${ARCHIVE_FILE}' from '${_TMP_VIRTUAL_ARCHIVE}'"
		# Revert directory structure so all paths work below (e.g. when cleaning up):
		swapParentAndChildDir "${_TMP_VIRTUAL_ARCHIVE}"  "${_TMP_ROOT_FS}"
	else
		assemble_archive "${CHROOT_DIR}/${_VIRTUAL_ARCHIVE}" "${ARCHIVE_FILE}" || die "Could not assemble archive ${ARCHIVE_FILE} from ${CHROOT_DIR}/${_VIRTUAL_ARCHIVE}"
	fi
	[ -n "${_PKG_SRC_DIR}" ] && { /bin/rm -fr "${_PKG_SRC_DIR}" ||  die "Something went wrong" ; }

	destroy_virtual_archive "${CHROOT_DIR}/${_VIRTUAL_ARCHIVE}"
	if [ "${BOOTSTRAP}" == TRUE ] ; then
		# ensure Ubuntu archive creation by-product in the form of a properly rooted CHROOT:
		replaceParentWithChildDirContent "${_CHROOT_DIR_PARENT}" "${_TMP_ROOT_FS}"
		# only expecting empty (=undefined) initial CHROOT_DIR when creating Ubuntu bootstrap archives:
		[ -z "${_CHROOT_DIR_DEFINED}" ] && { /bin/rm -r "${_CHROOT_DIR_PARENT}" || die "Could not remove rootfs in ${_CHROOT_DIR_PARENT}" ; }
	fi
	return 0
}

stderr "Called as: $0 $*"
create_archive "$@"  && stderr "Finished: $0 $*"

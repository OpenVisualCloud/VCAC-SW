#!/bin/bash
# library of shell functions; to be sourced
set -eu

# Library's constants and internal variables. The variables not to be modified out of the library:
declare -gr -A _CONST_ADD_SCRIPTS=( ["PRE"]="preadd.sh" ["POST"]="postadd.sh" )	# Bash v.4 supports dictionaries. Mapping of installation steps to script file name:
readonly _CONST_APT_DB_FILENAME=apt.tar.gz
readonly _CONST_ARCHIVE_INFO_FILE_NAME=arch_info.txt
readonly _CONST_CUSTOM_DIR=custom
declare -gr -A _CONST_DISTROS=( ["16.04"]="xenial" ["16.04.3"]="xenial" ["16.04.4"]="xenial" ["16.10"]="yakkety" ["18.04"]="bionic")	# Bash v.4 supports dictionaries. Mapping of description to distribution name:
readonly _CONST_DEBUG_LEVEL=1	# 0 means no debug; positive numbers increase verbosity
readonly _CONST_FAKE_EXT=ORG	# can not contains characters special to regex (used in sed)
_FAKED_COMMANDS_REGISTER=() 	# an array of faked commands' original and faked names; to be reverted to their original state
readonly _CONST_PKGDB_LINK=pkgdb_link
readonly _CONST_PKGDB_ROOT=pkgdb_root_link
readonly _CONST_PKG_DIR=archives
readonly _CONST_PKG_EXT=deb			# the file name extension for the software packages
# TODO: running as non-root not fullly tested
# sudo needs only be installed when not running as root:
[ "$( id -u )" == 0 ] && readonly  _CONST_SUDO="" || readonly _CONST_SUDO="sudo -E"

stderr(){
	echo "*** $*" >&2
}
notice(){
	stderr "NOTICE: $*"
}
warn(){
	stderr "WARNING: $*"
}
die(){
	local EXIT_CODE=$(( $? == 0 ? 99 : $? ))
	stderr "ERROR: $*"
	exit ${EXIT_CODE}
}
debug(){
	local _LEVEL=1
	[ $# -gt 1 ] && { _LEVEL="$1" ; shift ;}
	[ "${_LEVEL}" -le "${_CONST_DEBUG_LEVEL}" ] && stderr "DEBUG[${_LEVEL}]: $*"
	return 0
}

# Prepares a chroot environemt wich mounted /dev, /sys and proc,
# which it tries to unmount cleanly in case of failure (or success).
# Executes the command given as parameter or commands from stdin in the chrooted environment.
# Returns the exit code of chroot.
# To get chroot fail at any error, pass your commands with 'set -e'
# Typical call:
#	chroot /tmp/dir /bin/bash << <a HERE-document with commands to be executed, if more than one>
do_chroot() {
	local _DIR="$1" ; shift
	local _CMD=("${@}")
	local _MSG="chroot to ${_DIR} with command: ${_CMD[*]}"

	debug 3 "Preparing ${_MSG}"
	# fakeroot needs libraries
	# see Linux namespaces in https://boxbase.org/entries/2015/oct/5/linux-namespaces/ , http://man7.org/linux/man-pages/man1/unshare.1.html
	# Accepting "/" as chroot directory allows to reuse dependant scripts by running them on nodes
	if [ "$( realpath "${_DIR}" )" != "/"  ] ; then
		# read-only mounting should be enough for many cases
		${_CONST_SUDO} mount -o ro -t proc none "${_DIR}/proc"	|| die "Could not mount ${_DIR}/proc"
		${_CONST_SUDO} mount -o ro -t sysfs /sys "${_DIR}/sys" || die "Could not mount ${_DIR}/sys"
		# /dev must be RW, as Ubuntu needs write access during dist-upgrade, probably running 'MAKEDEV generic' as 88 devices are created. OTOH CentOS needs /dev mounted as it only populates a handful of devices: fd  full  kmsg  null  ptmx  pts  random  shm  stderr  stdin  stdout  tty  urandom  zero and we do not include MAKEDEV package to run 'MAKEDEV generic'. All in one: mounting rw:
		${_CONST_SUDO} mount --bind /dev "${_DIR}/dev"	|| die "Could not mount ${_DIR}/dev"	# bind just causes re-mounting, not re-creation of the filesystem
		${_CONST_SUDO} mount -o newinstance -t devpts devpts "${_DIR}/dev/pts" || die "Could not mount ${_DIR}/dev/pts" # to replace with another one the dpkg error: E: Can not write log (Is /dev/pts mounted?) - posix_openpt (2: No such file or directory)
		${_CONST_SUDO} mount --bind "${_DIR}/dev/ptmx" "${_DIR}/dev/pts/ptmx"	|| die "Could not mount ${_DIR}/dev/pts/ptmx"
		# TODO: try mounting in this way: https://github.com/spotify/linux/blob/master/Documentation/filesystems/devpts.txt
	fi

	local _STATUS=0
	# if using sudo, -E is necessary if e.g. environmental variables for proxies are to be passed
	if [ ${#_CMD[@]} -eq 0 ] ; then
		# use "" or "${_CONST_SUDO} -E" :
		${_CONST_SUDO:+${_CONST_SUDO} -E} chroot "${_DIR}"
	else
		# use "" or "${_CONST_SUDO} -E" :
		${_CONST_SUDO:+${_CONST_SUDO} -E} chroot "${_DIR}" "${_CMD[@]}"
	fi || _STATUS=$?	# a trick to save exit code in case this is executed from a script with "-e";

	# a dare attempt to kill any processes occupying the chroot.
	# there should be none, and if the out-of-chroot systemd or rsyslogd ocupy it, they are unkillable anyway
	# local _IS_BUSY="$(fuser -m "${_DIR}/proc" 2>/dev/null )"
	# if [ -n "${_IS_BUSY}" ] ; then
		# echo "The chroot ${_DIR} is used by the following processes. Killing before trying to unmount:" >&2
		# ps -p $(fuser -m "${_DIR}/proc" 2>/dev/null) | awk '{ printf "\t(%5d) %s\n", $1, $5 }' >&2
		# exit 1
		# # killing systemd does not hurt Ubuntu:
		# fuser -m "${_DIR}/proc" 2>/dev/null | xargs kill -9
	# fi
	# Accepting "/" as chroot directory allows to reuse dependant scripts by running them on nodes
	if [ "$( realpath "${_DIR}" )" != "/"  ] ; then
		${_CONST_SUDO} umount -lf "${_DIR}/dev/pts/ptmx"	|| die "Could not umount ${_DIR}/dev/pts/ptmx"
		${_CONST_SUDO} umount -lf "${_DIR}/dev/pts"|| die "Could not umount ${_DIR}/dev/pts"
		${_CONST_SUDO} umount -lf "${_DIR}/dev"	|| die "Could not umount ${_DIR}/dev"
		${_CONST_SUDO} umount -lf "${_DIR}/sys"	|| die "Could not umount ${_DIR}/sys"
		${_CONST_SUDO} umount -lf "${_DIR}/proc"	|| die "Could not umount ${_DIR}/proc"
	fi

	debug 3 "End of ${_MSG}"
	return ${_STATUS}	# late return of chroot exit status
}

create_info_file(){
	[ $# -lt 6 ] && die "Insufficient number ($#) of parameters to create_info_file (this should never happen)"
	local _ARCHIVE_INFO_DIR="$1"; shift
	local _TARGET_OS="$1";		shift
	local _TARGET_OS_VER="$1";	shift
	local _KERNEL_VER="$1";		shift
	local _AUTHOR="$1";			shift
	local _PKG_LIST=("${@}")

	# add the info file to the final archive. It is assumed to be run in chrooted environment, so to collect information on the TARGET environment:
	local _BUILD_DISTRO="$(cat /etc/*release 2>/dev/null | grep "DISTRIB_DESCRIPTION\|PRETTY_NAME")" # declaring with assignment to mask return value (=neglecting shellcheck error SC2155)
	command -v lsb_release > /dev/null && _BUILD_DISTRO="$(lsb_release -a 2>/dev/null)"

	echo "This software archive can be a bootstrap or a regular archive.
A bootstrap archive	contains already installed software and can additionally contain package files. \
A regular archive contains software to be installed in the form of package files and/or other files. \
During application of the archive, the software packages and other files contained in the archive get \
installed in the target environment.
Packages usually have dependencies on another packages. Such dependant packages (if any) were \
automatically selected and the corresponding package files have been included in this archive. \
These dependant packages are guaranteed to match the needs of the essential packages at the time \
of selection, and to fit with the packages already pre-existing in the build chroot environment.
The package management tool (e.g. apt-get, yum) db included in the archive with the packages is \
up-to-date to the point to reflect the dependencies for all the packages included in this archive, \
and for the packages already pre-existing in the build chroot environment.
When adding any future packages in the production environment, the package management tool db \
should be updated first (e.g. apt-get update) as future packages may depend on newer version of \
the packages that are in this archive.

Essential packages: $(echo "${_PKG_LIST[*]:-NONE}" | tr '\n' ' ')
Creation date: $(date 2>/dev/null)
Author: ${_AUTHOR}
Build system: $(uname -a 2>/dev/null)
Build system distribution: $(echo "${_BUILD_DISTRO}" | tr '\n' ',')
Target system kernel: ${_KERNEL_VER}
Target system distribution: ${_TARGET_OS}
Target system version: ${_TARGET_OS_VER}" \
		> "${_ARCHIVE_INFO_DIR}/${_CONST_ARCHIVE_INFO_FILE_NAME}"
}

get_from_archive_info_file(){
	local _ARCHIVE_INFO_FILE_DIR="$1"
	local _LABEL="$2"

	_LABEL="${_LABEL}: "	# this part will be deleted by sed below
	# print the rest of the line starting right after ^${_LABEL}
	# sed: replace ${_LABEL} with an empty string, and print (p) the result
	grep "^${_LABEL}" "${_ARCHIVE_INFO_FILE_DIR}/${_CONST_ARCHIVE_INFO_FILE_NAME}" |	sed -n -e "s/^${_LABEL}//p"
	return 0
}

# The file/directory structure of an archive is as follows:
# 	Max 2 directories:
#		/${_CUSTOM_DIR}/ 		# max one entry; this entry may contain bootstrap or something else
#		/${_PACKAGE_DIR}/*.[deb|rpm]	# 0 or more *.deb  files or *.rpm files
#	/${_APT_DB_FILENAME} can exist only if /${_PACKAGE_DIR}/ exist
#	Max 2 named script files:
#		${_ADD_SCRIPTS["PRE"]}		# the PREADD script name
#		${_ADD_SCRIPTS["POST"]}		# the POSTADD script name
#	Always an ${_ARCHIVE_INFO_FILE_NAME}
# TODO: where to verify (non)existing elements of the virtual archive before creating the archive? This function should only add the named elements to the archive, as _VIRTUAL_ARCHIVE_DIR can contain spurious elements (e.g. the original rootfs when creating the bootstrap archive)
assemble_archive (){
	local _VIRTUAL_ARCHIVE_DIR="$1"
	local _ARCHIVE_FILE="$2"

	touch "${_ARCHIVE_FILE}"	# to add files/dirs later with 'tar rf'
	if [ -d "${_VIRTUAL_ARCHIVE_DIR}/${_CONST_PKG_DIR}" ] ; then
		# add the *._CONST_PKG_EXT files to the final archive:
		tar rf "${_ARCHIVE_FILE}" -C "${_VIRTUAL_ARCHIVE_DIR}" "${_CONST_PKG_DIR}" --exclude=lock --exclude=partial
		# add the package management tool (e.g. apt-get) db to the final archive:
		local _DB_TAR_TMP_DIR; _DB_TAR_TMP_DIR="$(mktemp --directory )"	# uses directory ${TMPDIR:-/tmp} (i.e. /tmp is the value by default)
		local _LINK
		for _LINK in "${_VIRTUAL_ARCHIVE_DIR}/${_CONST_PKGDB_LINK}".??? ; do
			local _LNAME; _LNAME="$( basename -- "${_LINK}")"
			local _LEXT; _LEXT="${_LNAME##*.}"
			# var/lib/dpkg dpkg db gets compressed approx 6x, but seems unnecessary
			# apt-get db gets compressed approx 6x
			local _PKGDB_TAR="${_LEXT}.${_CONST_APT_DB_FILENAME}"
			tar czf "${_DB_TAR_TMP_DIR}/${_PKGDB_TAR}" -C "$( readlink "${_VIRTUAL_ARCHIVE_DIR}/${_CONST_PKGDB_ROOT}" )" "$( readlink "${_LINK}" )"
			tar rf "${_ARCHIVE_FILE}" -C "${_DB_TAR_TMP_DIR}" "${_PKGDB_TAR}"
		done
		rm -fr "${_DB_TAR_TMP_DIR}"
	fi

	# add the _CUSTOM_FILE_DIR to the ${_CONST_CUSTOM_DIR} in the final archive:
	[ -e "${_VIRTUAL_ARCHIVE_DIR}/${_CONST_CUSTOM_DIR}" ] && tar rf "${_ARCHIVE_FILE}" -C "${_VIRTUAL_ARCHIVE_DIR}" "${_CONST_CUSTOM_DIR}"

	# add the PREADD/POSTADD scripts to the final archive only if they exist:
	[ -f "${_VIRTUAL_ARCHIVE_DIR}/${_CONST_ADD_SCRIPTS["PRE"]}" ] &&
		tar rf "${_ARCHIVE_FILE}" -C "${_VIRTUAL_ARCHIVE_DIR}" "${_CONST_ADD_SCRIPTS["PRE"]}"
	[ -f "${_VIRTUAL_ARCHIVE_DIR}/${_CONST_ADD_SCRIPTS["POST"]}" ] &&
		tar rf "${_ARCHIVE_FILE}" -C "${_VIRTUAL_ARCHIVE_DIR}" "${_CONST_ADD_SCRIPTS["POST"]}"

	tar rf "${_ARCHIVE_FILE}" -C "${_VIRTUAL_ARCHIVE_DIR}" "${_CONST_ARCHIVE_INFO_FILE_NAME}"
}

# extract & check the file/directory structure
# return non-zero if verification unsuccesfull, zero otherwise
# The file/directory structure of an archive is as follows:
# 	Max 2 directories:
#		/${_CONST_CUSTOM_DIR}/* 		# max one entry; this entry may contain bootstrap or something else
#		/"${_CONST_PKG_DIR}"/*.deb	# more than 0 *.deb files
#	An ${_CONST_APT_DB_FILENAME} if /"${_CONST_PKG_DIR}"/ exist
#	Max 2 named script files:
#		${_CONST_ADD_SCRIPTS["PRE"]}		# the PREADD script name
#		${_CONST_ADD_SCRIPTS["POST"]}		# the POSTADD script name
#	Always an ${_CONST_ARCHIVE_INFO_FILE_NAME}
extract_archive () {
	local _TAR="$1"
	local _DST="$2"

	tar xf "${_TAR}" -C "${_DST}" || die "Could not untar ${_TAR} to ${_DST}"

	# check ${_CONST_CUSTOM_DIR} and "${_CONST_PKG_DIR}"
	local _UNEXPECTED_DIRS="$(find "${_DST}" -mindepth 1 -maxdepth 1 -type d ! -name ${_CONST_CUSTOM_DIR} ! -name "${_CONST_PKG_DIR}" 2>/dev/null )" # declaring with assignment to mask return value (=neglecting shellcheck error SC2155)
	[ -n "${_UNEXPECTED_DIRS}" ] && warn "Unexpected directory entries in archive ${_TAR}: ${_UNEXPECTED_DIRS}" && return 1

	# check the ${_CONST_CUSTOM_DIR}/
	[ -d "${_DST}/${_CONST_CUSTOM_DIR}" ] && [ "$(count_entries_in_dir "${_DST}/${_CONST_CUSTOM_DIR}" "*")" -lt 1 ] && notice "No custom file/directory entry in ${_TAR}"

	# check the "${_CONST_PKG_DIR}"/
	local _DB_FILE
	local -i _DB_COUNT=0
	local _DB_EMPTY=TRUE	# will be modified to FALSE if at least one _DB_FILE has non-zero length
	for _DB_FILE in "${_DST}"/???."${_CONST_APT_DB_FILENAME}"; do
		if [ -f "${_DB_FILE}" ] ; then
			_DB_COUNT=$(( _DB_COUNT + 1 ))
			[ -s "${_DB_FILE}" ] && _DB_EMPTY=FALSE
		fi
	done
	[ -d "${_DST}/${_CONST_PKG_DIR}" ] && [[ "${_DB_COUNT}" -eq 0 || "${_DB_EMPTY}" == TRUE ]] && notice "Package directory lacks non-empty pkg database(s) ${_CONST_APT_DB_FILENAME} in archive ${_TAR}"  &&  return 0
	[ ! -d "${_DST}/${_CONST_PKG_DIR}" ] && [ -s "${_DST}/${_CONST_APT_DB_FILENAME}" ] && warn "An apt-get database ${_CONST_APT_DB_FILENAME} without the archive directory in archive ${_TAR}" && return 1
	if [ -d "${_DST}/${_CONST_PKG_DIR}" ] ; then
		local -i _PKG_ENTRIES; _PKG_ENTRIES="$(count_entries_in_dir "${_DST}/${_CONST_PKG_DIR}" "*.${_CONST_PKG_EXT}")"
		[ "${_PKG_ENTRIES}" -lt 1 ] && warn "Directory ${_DST}/${_CONST_PKG_DIR} exists but no package files in archive ${_TAR}" && return 1
		local -i _OTHER_ENTRIES; _OTHER_ENTRIES="$(count_entries_in_dir "${_DST}/${_CONST_PKG_DIR}" "*")"
		(( _OTHER_ENTRIES = _OTHER_ENTRIES - _PKG_ENTRIES ))
		[ "${_OTHER_ENTRIES}" -gt 0 ] && warn "Non-package files in the directory ${_DST}/${_CONST_PKG_DIR} of the archive ${_TAR}: $(find "${_DST}/${_CONST_PKG_DIR}" -maxdepth 0 ! -name \*."${_CONST_PKG_EXT}" )" && return 1
	fi

	# Count the number of regular files, excluding the named ones
	local _UNEXPECTED_FILES="$(find "${_DST}" -mindepth 1 -maxdepth 1 -type f ! -name "${_CONST_ADD_SCRIPTS["PRE"]}" ! -name "${_CONST_ADD_SCRIPTS["POST"]}" ! -name "???.${_CONST_APT_DB_FILENAME}" ! -name "${_CONST_ARCHIVE_INFO_FILE_NAME}" )" # declaring with assignment to mask return value (=neglecting shellcheck error SC2155)
	[ -n "${_UNEXPECTED_FILES}" ] && warn "Unexpected files in archive ${_TAR}: ${_UNEXPECTED_FILES}" && return 1

	[ ! -s "${_DST}/${_CONST_ARCHIVE_INFO_FILE_NAME}" ] && warn "Missing or empty ${_DST}/${_CONST_ARCHIVE_INFO_FILE_NAME} file in ${_TAR}" && return 1

	# extract and check the pkg db
	for _DB_FILE in "${_DST}"/???."${_CONST_APT_DB_FILENAME}"; do
		[ -f "${_DB_FILE}" ] && tar xzf "${_DB_FILE}" -C "${_DST}"
	done
	if [ "${_DB_COUNT}" -gt 0 ] ; then
		[ ! -d  "${_DST}/var/lib/apt" ] && warn "Missing apt-get db under ${_DST}" && return 1
		# expecting 3 subdirectories like: lists/  mirrors/  periodic/
		local _NUM_OF_DIRS; _NUM_OF_DIRS=$(find "${_DST}/var/lib/apt/" -type d -maxdepth 1 -mindepth 1 2>/dev/null | wc -l  )
		[[ ${_NUM_OF_DIRS} -lt 3  || ${_NUM_OF_DIRS} -gt 6 ]] && warn "Incorrect apt-get db structure under ${_DST} (${_NUM_OF_DIRS} subdirectories)" && return 1
		# As per https://askubuntu.com/a/28375 only *Release and *Packages files are needed; but I have observed:
		# -name \*_Packages -o -name \*_InRelease -or -name \*_Release:
		# -name \*_Packages -o -name \*_InRelease -or -name \*_i18n_Translation-\*:
		local _NUM_OF_DB_FILES; _NUM_OF_DB_FILES=$(find "${_DST}/var/lib/apt/lists/" -maxdepth 1 -size +0 \
			\( -name \*_Release -o -name \*_InRelease -o -name \*_Packages \) 2>/dev/null | wc -l )
		[ "${_NUM_OF_DB_FILES}" -lt 2 ] && warn "Missing or empty apt-get db files under ${_DST}/var/lib/apt/lists" && return 1
	fi
	return 0
}

count_entries_in_dir(){
	local _DIR="${1%/}"	# strip trailing '/', if any
	local _PATTERN="$2"

	find "${_DIR}" -mindepth 1 -maxdepth 1 -name "${_PATTERN}" 2>/dev/null | wc -l
}

isEmptyDir(){
	local _DIR="$1"

	# Also possible: [ -z "$(find "${CHROOT_DIR}" -maxdepth 0 -type d -empty 2>/dev/null)" ]
	[ -z "$(find "${_DIR}" -maxdepth 1 -mindepth 1 -print -quit 2>/dev/null)" ]
}

# Extracts and verifies the content of archive/bootstrap _ARCHIVE_FILE
# Prints to stdout the archive information if _PRINT_INFO_FILE is set to any value
# Returns status of the extraction
# The file/directory structure of an archive is as follows:
# 	Max 2 directories:
#		/${_CONST_CUSTOM_DIR}/* 		# max one entry; this entry may contain bootstrap or something else
#		/"${_CONST_PKG_DIR}"/*.deb	# more than 0 *.deb files
#	An ${_CONST_APT_DB_FILENAME} if /"${_CONST_PKG_DIR}"/ exist
#	Max 2 named script files:
#		${_CONST_ADD_SCRIPTS["PRE"]}		# the PREADD script name
#		${_CONST_ADD_SCRIPTS["POST"]}		# the POSTADD script name
#	Always an ${_CONST_ARCHIVE_INFO_FILE_NAME}
verify_archive(){
	local _ARCHIVE_FILE="$1"
	local _PRINT_INFO="${2:-FALSE}"

	local _VERIF_DIR; _VERIF_DIR="$(mktemp -d)"
	local _STATUS=0
	extract_archive "${_ARCHIVE_FILE}" "${_VERIF_DIR}" || _STATUS=$?

	if [ "${_PRINT_INFO}" != FALSE ] ; then
		[ -s "${_VERIF_DIR}/${_CONST_ARCHIVE_INFO_FILE_NAME}" ] &&
			cat "${_VERIF_DIR}/${_CONST_ARCHIVE_INFO_FILE_NAME}"
		local _CUST_ENTRY_NAME="$(ls "${_VERIF_DIR}/${_CONST_CUSTOM_DIR}" 2>/dev/null)" # declaring with assignment to mask return value (=neglecting shellcheck error SC2155)
		printf "Custom file/directory: %s" "${_CUST_ENTRY_NAME:-NONE}" >&2
		if [ -z "${_CUST_ENTRY_NAME}" ] ; then
			echo "" >&2		# a newline
		else
			# the /tmp direcoty is used during extraction of "${_CONST_PKG_DIR}":
			if [[	-d "${_VERIF_DIR}/${_CONST_CUSTOM_DIR}/${_CUST_ENTRY_NAME}"/bin	&& \
					-d "${_VERIF_DIR}/${_CONST_CUSTOM_DIR}/${_CUST_ENTRY_NAME}"/lib	&& \
					-d "${_VERIF_DIR}/${_CONST_CUSTOM_DIR}/${_CUST_ENTRY_NAME}"/tmp	&& \
					-d "${_VERIF_DIR}/${_CONST_CUSTOM_DIR}/${_CUST_ENTRY_NAME}"/etc ]] ; then
				echo "	(looks like a proper bootstrap)" >&2
			else
				echo "	(doesn't look like a bootstrap)" >&2
			fi
		fi
		local _PKG_FILE_COUNT="$(find "${_VERIF_DIR}/${_CONST_PKG_DIR}" -maxdepth 0 2>/dev/null | wc -l )" # declaring with assignment to mask return value (=neglecting shellcheck error SC2155)
		echo "${_CONST_PKG_EXT} package files (${_PKG_FILE_COUNT}): $(find "${_VERIF_DIR}/${_CONST_PKG_DIR}" -maxdepth 0 2>/dev/null | tr '\n' ' ' )" >&2 || true
		local _FILE
		_FILE="$(cd "${_VERIF_DIR}"; ls "${_CONST_ADD_SCRIPTS["PRE"]}" 2>/dev/null)" || true
		echo "PREADD script:	${_FILE:-NONE}" >&2
		_FILE="$(cd "${_VERIF_DIR}"; ls "${_CONST_ADD_SCRIPTS["POST"]}" 2>/dev/null)" || true
		echo "POSTADD script:	${_FILE:-NONE}" >&2
	fi
	echo "" >&2
	if [ ${_STATUS} -eq 0 ] ; then
		stderr "Archive ${_ARCHIVE_FILE} verified succesfully"
	else
		warn "Archive ${_ARCHIVE_FILE} verification failed."
	fi
	/bin/rm -r "${_VERIF_DIR}"
	return ${_STATUS}
}

# adds packages given by ${_PKG_LIST} by filename or by package name into the environment at the root directory at ${_CHROOT_DIR}
# synchronizes apt-get database with Internet, if requested with ${_UPDATE_APT_GET}
# cleans apt-get cache before adding packages, if requested with ${_CLEAN_APT_GET}
# supresses ${_HARMLESS_MSGS} in lenghty apt-get/dpkg stdout
add_packages(){
	local _CHROOT_DIR="$1";		shift
	local _HARMLESS_MSGS="$1";	shift	# values: NOSUPRESS or a regex
	local _UPDATE_APT_GET="$1";	shift	# values: UPDATE or other (e.g. NOUPDATE)
	local _CLEAN_APT_GET="$1";	shift	# values: CLEAN or other (e.g. NOCLEAN)
	local _PKG_LIST=("${@}")				# values: DIST_UPGRADE or a list of packages

	do_chroot "${_CHROOT_DIR}" /bin/bash << EOF || die "Could not add package(s): ${_PKG_LIST[*]} in chroot ${_CHROOT_DIR}, using higher-level package mgmt tool"
		set -eu
		set -o pipefail	# to fail pipes when grepping the output
		# prevent automatic start of services (from initd, but systemd should handle this as well):
		echo "#!/bin/sh
				exit 101" > /usr/sbin/policy-rc.d
		chmod a+x /usr/sbin/policy-rc.d
		export LC_ALL=C		# to avoid apt-get complainign on locales

		if [ "${_HARMLESS_MSGS}" == NOSUPRESS ] ; then
			GREP="cat"
			MSG=""
		else
			GREP="grep -v \"${_HARMLESS_MSGS}\""
			MSG="
Supressing the following apt-get messages considered harmless: ${_HARMLESS_MSGS}"
		fi
		if [ "${_UPDATE_APT_GET}" == UPDATE ] ; then
			echo "Updating apt-get local database to install ${_PKG_LIST[*]}\${MSG}" >&2
			apt-get update 2>&1 | eval "\${GREP}"
		fi

		[ "${_CLEAN_APT_GET}" == CLEAN ] && apt-get clean	# to clean the apt-get cache of any previously downloaded *.deb package files

		echo "Installing ${_PKG_LIST[*]} \${MSG}" >&2
		if [ "${_PKG_LIST[0]}" == DIST_UPGRADE ] ; then
			# piping to GREP will not mask failures, because -o pipefail is set above
			DEBIAN_FRONTEND=noninteractive RUNLEVEL=1 apt-get -y dist-upgrade \
				| eval "\${GREP}"
		else
			# piping to GREP will not mask failures, because -o pipefail is set above
			DEBIAN_FRONTEND=noninteractive RUNLEVEL=1 apt-get install -y --no-install-recommends ${_PKG_LIST[@]} 2>&1 \
				| eval "\${GREP}"
		fi

		rm /usr/sbin/policy-rc.d
EOF
}

get_codename(){
	local _DESCRIPTION="$1"

	local _DIST=""
	local _CORRECT_DIST=FALSE
	[ "${#_CONST_DISTROS[@]}" -ne 0 ] && for _DIST in "${!_CONST_DISTROS[@]}"; do
		[ "${_DIST}" == "${_DESCRIPTION}" ] && _CORRECT_DIST=TRUE
	done
	[ "${_CORRECT_DIST}" == FALSE ] && die "Unsupported description given: ${_DESCRIPTION}. Supported descriptions: ${!_CONST_DISTROS[*]}"

	echo "${_CONST_DISTROS["${_DESCRIPTION}"]}"
}

# Ensure file/dir _DST_NAME does not exists
# If it does, move existing file(s) by appending _EXT to each, recursively
# The function accepts that _DST_NAME may end with _EXT
reserve_name(){
	local _DST_NAME="$1"
	local _EXT="$2"

	local _DST_BCKP="${_DST_NAME}"
	if [ ! -e "${_DST_BCKP}" ] ; then
		notice "File/directory ${_DST_BCKP} does not exists, no need to free the name"
	else
		# find the shortest, yet unused, backup name based on _DST_NAME
		while [ -e "${_DST_BCKP}${_EXT}" ] ; do
			_DST_BCKP="${_DST_BCKP}${_EXT}"
		done
		# Now it is sure ${_DST_BCKP}.${_EXT} does not exist
		# rename all existing backup names to free the name _DST_NAME
		while [[ "${_DST_BCKP}" != "${_DST_NAME}" ]] ; do
			# TODO: use sudo or fakeroot?
			mv "${_DST_BCKP}" "${_DST_BCKP}${_EXT}"
			_DST_BCKP="${_DST_BCKP%${_EXT}}"		# cut off the trailing _EXT
		done
		# TODO: use sudo or fakeroot?
		mv "${_DST_NAME}" "${_DST_NAME}${_EXT}"
	fi
	# _DST_NAME name is unused now. Its content will be overriden if it exists and a file/dir is copied/renamed to it
}

# Revert the effects of reserve_name() by moving the content of _DST_NAME backup files.
# Recreates the name and contents of _DST_NAME and of its backup files, recursively (by removing the appendix _EXT  appended to backup files of _DST_NAME)
# The _DST_NAME will be created or its content will get overwritten, if its backup exists
release_name(){
	local _DST_NAME="$1"
	local _EXT="$2"

	# move all potentially remaining backup versions by cutting one level of _EXT
	[ ! -e "${_DST_NAME}${_EXT}" ] && notice "Release of ${_DST_NAME} requested, but no backup exists"
	[ -d "${_DST_NAME}" ] && [ -e "${_DST_NAME}${_EXT}" ] && /bin/rm -fr "${_DST_NAME}"	# moving below will not work apropriately if _DST_NAME directoy exists
	while [ -e "${_DST_NAME}${_EXT}" ] ; do
		# TODO: use sudo or fakeroot?
		mv "${_DST_NAME}${_EXT}" "${_DST_NAME}"
		_DST_NAME="${_DST_NAME}${_EXT}"
	done
}

# Abbreviations used in the command faking rules below:
#	ENV = do not fake, use the current environmental settings (perhaps left by previous SAa applied in the process)
#	INFO_FILE = fake with values from the INFO_FILE of the SA (only possible during application)
#	CMDLINE = use values from the current command-line invocation for fakeing or for storing in the INFO_FILE
#
# Command faking rules:
# --------------------------------------------------------------------------------------
#                                 Application of SA        Creation of SA
#                                 faking                   faking & storing in INFO_FILE
# --------------------------------------------------------------------------------------
# no cmd-line						INFO_FILE                  ENV
# not a bootstrap                (do not use ENV)           or notice & store "ANY"
#                                                            (if no lsb_release)
# --------------------------------------------------------------------------------------
# no cmd-line	                    INFO_FILE				   ERROR
# bootstrap
# --------------------------------------------------------------------------------------
# cmd-line                        CMDLINE=INFO_FILE            CMDLINE
#                           (if constistent with INFO_FILE) (notice if different from ENV)
#                              or notice & use CMDLINE
#                                    (otherwise)
# --------------------------------------------------------------------------------------

# Create the faked _COMMAND  name for its  given _VERSION using the appropriate format
createFakedCommandName(){
	local _COMMAND="$1"
	local _VERSION="$2"	# VERSION start with 1
	awk "BEGIN {printf(\"${_COMMAND}.%03d.${_CONST_FAKE_EXT}\", ${_VERSION})}"
}

# returns the name of the original, unfaked _COMMAND in the format it gets when faked
# Does not modify anything, does not check _COMMAND existence, does not check if the _COMMAND has been already faked indeed
getFakedCommandSavedName(){
	local _COMMAND="$1"
	createFakedCommandName "${_COMMAND}" 1
}

# returns the name of the original, unfaked _COMMAND
# If the command has not yet been faked, the original name of _COMMAND is returned
getOriginalOfFakedCommand(){
	local _COMMAND="$1"
	local _SAVED_NAME="$( createFakedCommandName "${_COMMAND}" 1 )"
	[ ! -f "${_SAVED_NAME}" ] && echo "${_COMMAND}" && return 0
	echo "${_SAVED_NAME}"
}

# find the faked command with the highest version, i.e. probably the one recently faked and return the next one
getFakedCommandBackupName(){
	local _COMMAND="$1"
	local _CMD_DIR; _CMD_DIR="$( dirname -- "${_COMMAND}")"
	local _CMD_NAME; _CMD_NAME="$( basename -- "${_COMMAND}")"

	# the "10#" forces base 10 for numbers starting with 0:
	local _CURRENT_HIGHEST; _CURRENT_HIGHEST="10#$(find "${_CMD_DIR}"/ -mindepth 1 -maxdepth 1 -name "${_CMD_NAME}.???.${_CONST_FAKE_EXT}" 2>/dev/null | sort -r | head -1 | sed -e "s|.*${_CMD_NAME}\.\(.*\)\.${_CONST_FAKE_EXT}.*|\1|")"
	createFakedCommandName "${_COMMAND}" $(( _CURRENT_HIGHEST + 1))
}

# renaming commands would be weak as their behaviour may depend on arg[0]
# manipulating PATH would be weak as executable path may be incluided in one of foreign executables
# Returns the backup name of the command (which can not be the original command at this point)
fakeAndRegisterCommand(){
	local _COMMAND="$1"
	local _METHOD="${2:-RETAIN_PATH}"	# RETAIN_PATH or RETAIN_NAME; this is a hook to unify faking methods in a single function (a TODO: , mentioned above)

	[ ! -f "${_COMMAND}" ] && die "Command ${_COMMAND} does not exist, can not fake it !"
	local _FAKED_ORIGINAL_NAME; _FAKED_ORIGINAL_NAME="$(getFakedCommandSavedName "${_COMMAND}")"
	[ -f "${_FAKED_ORIGINAL_NAME}" ] && notice "Command ${_COMMAND} already faked, original command exists as ${_FAKED_ORIGINAL_NAME}"

	# Saving command's complete permissions is a nice idea but requires installing getfact/setfact which are rare and are not added to nodes:
	# local _CMD_PERMISSIONS; _CMD_PERMISSIONS="$(mktemp --tmpdir permissions_store.XXX )"
	# getfacl --absolute-names "${_COMMAND}" > "${_CMD_PERMISSIONS}"
	local _COMMAND_BACKUP; _COMMAND_BACKUP="$(getFakedCommandBackupName "${_COMMAND}")"
	# mv "${_COMMAND}" "${_COMMAND_BACKUP}"
	${_CONST_SUDO} cp --preserve=all "${_COMMAND}" "${_COMMAND_BACKUP}"
	echo "#!/bin/bash
#
# Faked by host:$(hostname),program:$0 on $(date)
# Original command still exists as '${_FAKED_ORIGINAL_NAME}'
# Previously faked (if any) version saved as '${_COMMAND_BACKUP}'
" | ${_CONST_SUDO}  tee "${_COMMAND}" > /dev/null
	cat - | ${_CONST_SUDO}  tee -a "${_COMMAND}" > /dev/null	# append stdin to ${_COMMAND}

	# restore command permissions:
	# setfacl --restore="${_CMD_PERMISSIONS}"
	# /bin/rm "${_CMD_PERMISSIONS}"

	# remove duplicate slashes, as a duplicate slash is used as field separator in the array below. Yes, thus loosing the triple slashes semantics.
	_COMMAND="$(tr -s / <<< "${_COMMAND}")"
	_COMMAND_BACKUP="$(tr -s / <<< "${_COMMAND_BACKUP}")"

	_FAKED_COMMANDS_REGISTER+=("${_COMMAND}"//"${_COMMAND_BACKUP}" )
	echo "${_COMMAND_BACKUP}"
}

# Restoring registered faked commands, in reverse order.
restoreFakedRegisteredCommands(){
	local _LAST_ONLY="${1:-""}"		# set to e.g. LASTONLY to only restore the last one

	local _END_IDX=0
	[ -n "${_LAST_ONLY}" ] && _END_IDX=$((${#_FAKED_COMMANDS_REGISTER[@]} - 1))
	# stderr "Restoring registered faked commands, in reverse order. The register contains: ${_FAKED_COMMANDS_REGISTER[@]}"
	for (( IDX=${#_FAKED_COMMANDS_REGISTER[@]}-1 ; IDX>=_END_IDX	; IDX-- )) ; do
		local _COMMAND_ORIGN; _COMMAND_ORIGN="$(awk -F// '{print $1}' <<<"${_FAKED_COMMANDS_REGISTER[IDX]}")"
		local _COMMAND_SAVED; _COMMAND_SAVED="$(awk -F// '{print $2}' <<<"${_FAKED_COMMANDS_REGISTER[IDX]}")"
		${_CONST_SUDO} mv "${_COMMAND_SAVED}" "${_COMMAND_ORIGN}"
	done
	if [ -n "${_LAST_ONLY}" ] ; then
		unset "_FAKED_COMMANDS_REGISTER[${#_FAKED_COMMANDS_REGISTER[@]}-1]" # remove the last element; the quotes prevent wildcard expansion in [], shoud any result by error
	else
		_FAKED_COMMANDS_REGISTER=()	#remove all elements by resetting the array
	fi
}

# Returns an integer:
# 00b -saved faked command does not exists, not registered
# 01b -saved faked command does not exists, but registered (should never happen!)
# 10b -saved faked command does exists, but unregistered (can happen if faking was done in another script run)
# 11b -saved faked command does exists, and registered
# In summary: value >1 means command is to be unFaked
isFakedOrRegisteredCommand(){
	die "Call to deprecated isFakedOrRegisteredCommand()"	# TODO: this needs replacement/removal since changing _FAKED_COMMANDS_REGISTER to array of pairs
	local _COMMAND="$1"

	local _CODE=0
	local _REGISTERED=""
	[ "${#_FAKED_COMMANDS_REGISTER[@]}" -ne 0 ] && for _REGISTERED in "${_FAKED_COMMANDS_REGISTER[@]}"; do
		[ "${_REGISTERED}" == "${_COMMAND}" ] && _CODE=1 && break
	done
	[ -f "${_COMMAND}.${_CONST_FAKE_EXT}" ] && _CODE=$((_CODE+2))
	return ${_CODE}
}
fakeUname(){
	local _CHROOT_DIR="$1"
	local _FAKED_KER_VER="$2"

	fakeAndRegisterCommand "${_CHROOT_DIR}"/bin/uname <<EOF >/dev/null
		case "\$1" in
			-r)
				echo ${_FAKED_KER_VER}
				;;
			*)
				"$(getFakedCommandSavedName /bin/uname)" "\$@"
				;;
		esac
EOF
}

fakeLsb_release(){
	local _CHROOT_DIR="$1"
	local _FAKED_DISTRO="$2"
	local _FAKED_OS_VER="$3"

	# this variable is assigned to _OS below:
	local -r -A _CENTOS=(
		[is]="CentOS"
		[rs]="${_FAKED_OS_VER}"
		[ds]="CentOS Linux release ${_FAKED_OS_VER} (Core)"
	)
	# this variable is assigned to _OS below:
	local -r -A _UBUNTU=(
		[is]="Ubuntu"
		[rs]="${_FAKED_OS_VER}"
		[ds]="Ubuntu ${_FAKED_OS_VER} LTS"
	)

	local _VAR_NAME=_"${_FAKED_DISTRO}"					# it is risky to base variable name on an external value, but in unexpected cases the 'declare' below will fail with a meaningfull message ("bash: declare: _WINDOWS: not found")
	local _PRINTOUT; _PRINTOUT="$( declare -p "${_VAR_NAME}" )"
	eval "${_PRINTOUT/$(echo "${_VAR_NAME}=")/_OS=}"	# neglecting shellcheck error SC2116; redefine variable which name is stored in _VAR_NAME into the new (local) variable _OS

	fakeAndRegisterCommand "${_CHROOT_DIR}"/usr/bin/lsb_release <<EOF >/dev/null
		case "\$1" in
			-is)
				echo "${_OS[is]}"
				;;
			-rs)
				echo "${_OS[rs]}"
				;;
			-ds)
				echo "${_OS[ds]}"
				;;
			*)
				"$(getFakedCommandSavedName /usr/bin/lsb_release)" "\$@"
				;;
		esac
EOF
}

# TODO: Make the below conform to remaining faking cases, and remove unFakeDepmod:
fakeDepmod(){
	local _CHROOT_DIR="$1"
	local _SYMLINK_DIR="$2"	# eg. /usr
	local _FAKED_KER_VER="$3"

	# Faking depmod on CentOS 7.4(Ubuntu 16.04.4 only differs with /sbin instead of /usr/sbin) causes two challenges:
	# 1. It is a link to a binary /usr/bin/kmod, which should not be overwritten
	# 2. The /usr/bin/kmod uses argv[0], so depmod cannot be renamed
	mkdir -p "${_CHROOT_DIR}"/tmp/sbin
	# TODO: better error handling is needed if the file aready exists:
	ln -s "$(chroot "${_CHROOT_DIR}" readlink -f "${_SYMLINK_DIR}"/sbin/depmod)" "${_CHROOT_DIR}"/tmp/sbin/depmod || true
	rm "${_CHROOT_DIR}/${_SYMLINK_DIR}"/sbin/depmod	# this is only a link to _SYMLINK_DIR/bin/kmod
	cat > "${_CHROOT_DIR}/${_SYMLINK_DIR}"/sbin/depmod <<EOF
#!/bin/bash
		case "\$1" in
		  -a)
			 /tmp/sbin/depmod -a "${_FAKED_KER_VER}"
			 ;;
		  *)
			 /tmp/sbin/depmod "\$@"
			 ;;
		esac
EOF
	chmod 755 "${_CHROOT_DIR}/${_SYMLINK_DIR}"/sbin/depmod
}

unFakeDepmod(){
	local _CHROOT_DIR="$1"
	local _SYMLINK_DIR="$2"	# eg. /usr

	rm "${_CHROOT_DIR}/${_SYMLINK_DIR}"/sbin/depmod
	ln "${_CHROOT_DIR}/${_SYMLINK_DIR}"/bin/kmod "${_CHROOT_DIR}/${_SYMLINK_DIR}"/sbin/depmod
	/bin/rm -fr "${_CHROOT_DIR}"/tmp/sbin
}

# Run the  pre-add or post-add script with parameters.
run(){
	local _STEP="$1"
	local _PHASE="$2"
	local _ARCHIVE_DIR="$3"		# relative to _CHROOT_DIR; directory to the archive; during archive creation parts may be located far from the rest of other archive components so are bind-mounted or copied to _ARCHIVE_DIR
	local _SCRIPTS_DIR="$4"		# relative to _CHROOT_DIR; path to the build scripts directory
	local _CHROOT_DIR="$5"

	local _SCRIPT; _SCRIPT="$( realpath "${_CHROOT_DIR}/${_ARCHIVE_DIR}/${_CONST_ADD_SCRIPTS["${_STEP}"]}" )"

	if [ -f "${_SCRIPT}" ] ; then
		stderr "Running the ${_SCRIPT} script, phase ${_PHASE}, step ${_STEP}"
		/bin/bash -euE "${_SCRIPT}" "${_STEP}" "${_PHASE}" "${_ARCHIVE_DIR}" "${_SCRIPTS_DIR}" "${_CHROOT_DIR}" || die "Phase ${_PHASE} step ${_STEP} script ${_SCRIPT} failed"
	else
		return 0
	fi
}

destroy_virtual_archive(){
	local _VIRTUAL_ARCHIVE_DIR="$1"

	[ -d "${_VIRTUAL_ARCHIVE_DIR}/${_CONST_PKG_DIR}" ] && umount "${_VIRTUAL_ARCHIVE_DIR}/${_CONST_PKG_DIR}"
	[ -d "${_VIRTUAL_ARCHIVE_DIR}/${_CONST_CUSTOM_DIR}" ] && umount "${_VIRTUAL_ARCHIVE_DIR}/${_CONST_CUSTOM_DIR}"

	/bin/rm -fr "${_VIRTUAL_ARCHIVE_DIR}"
}

# Build directory structure in _VIRTUAL_ARCHIVE_DIR, which should reasonably be a location accessible from within CHROOT_DIR. The directory structure can be build incrementally, i.e. by successive calls to build_virtual_archive
# Creates a dir with files/mountpoints (hard links can not be used across  filesystems) named appropriately.
# Missing archive elements not created; the function can be called later to complete the missing items
# The caller is responsible for calling destroy_virtual_archive to umount the mountpoints, and to remove files/dirs created here
# hard-linkng files (due to potential different filesystems) is not correct. Need to copy them.
# The directory structure of a virtual archive is as follows:
# 	Max 2 directories:
#		/${_CUSTOM_DIR}/ 		# max one entry; this entry may contain bootstrap or something else
#		/${_PACKAGE_DIR}/*.[deb|rpm]	# 0 or more *.deb  files or *.rpm files # TODO: is a directory tree acceptable?
#	Up to (num_of_chars*num_of_chars)+1 soft links to directories/files with package databases:
#		pkg_db_root_link
#		pkg_db_link.XX
#	Max 2 named script files:
#		/${_ADD_SCRIPTS["PRE"]}		# the PREADD script name
#		/${_ADD_SCRIPTS["POST"]}		# the POSTADD script name
#	Always:
#		 /${_ARCHIVE_INFO_FILE_NAME}
build_virtual_archive(){
	local _VIRTUAL_ARCHIVE_DIR="$1" ;shift
	local _PACKAGE_DIR="$1"	;		shift	# calling with "" means: none
	local _PKG_DB_ROOT="$1" ;		shift	# absolute path; only used as root for the _PKG_DB_NAME_LIST; otherwise can be  "" which means: none
	local _PKG_DB_NAME_LIST="$1" ;	shift	# quoted, whitespace separated list of dirs relative to _PKG_DB_ROOT; e.g. "/var/lib/dpkg /var/lib/apt"; calling with "" means: none
	local _CUSTOM_FILE_DIR="$1"	;	shift	# calling with "" means: none
	local _PREADD_SCRIPT="$1" ;		shift	# calling with "" means: none
	local _POSTADD_SCRIPT="$1" ;	shift	# calling with "" means: none
	#local _INFO_FILE="$1" ;			shift	# calling with "" means: none

	# Archive is ro during creation, as it is during application:
	[ -n "${_PACKAGE_DIR}" ] && mkdir -p "${_VIRTUAL_ARCHIVE_DIR}/${_CONST_PKG_DIR}" && mount --bind -o ro  "${_PACKAGE_DIR}" "${_VIRTUAL_ARCHIVE_DIR}/${_CONST_PKG_DIR}"
	local _LINK;
	# write the links
	[ -n "${_PKG_DB_ROOT}" ] && ln -s "${_PKG_DB_ROOT}" "${_VIRTUAL_ARCHIVE_DIR}/${_CONST_PKGDB_ROOT}"
	for _LINK in ${_PKG_DB_NAME_LIST} ; do
		ln -s --force "${_LINK}" "$(mktemp --tmpdir="${_VIRTUAL_ARCHIVE_DIR}" "${_CONST_PKGDB_LINK}".XXX )"
	done
	[ -n "${_PREADD_SCRIPT}" ]		&& cp "${_PREADD_SCRIPT}"	"${_VIRTUAL_ARCHIVE_DIR}/${_CONST_ADD_SCRIPTS["PRE"]}"
	[ -n "${_POSTADD_SCRIPT}" ]		&& cp "${_POSTADD_SCRIPT}"	"${_VIRTUAL_ARCHIVE_DIR}/${_CONST_ADD_SCRIPTS["POST"]}"
	#[ -n "${_INFO_FILE}" ]			&& cp "${_INFO_FILE}"	"${_VIRTUAL_ARCHIVE_DIR}/${_CONST_ARCHIVE_INFO_FILE_NAME}"
	if [ -n "${_CUSTOM_FILE_DIR}" ] ; then
		if [ -d "${_CUSTOM_FILE_DIR}" ] ; then
			mkdir -p "${_VIRTUAL_ARCHIVE_DIR}/${_CONST_CUSTOM_DIR}"
			# _CONST_CUSTOM_DIR is rw during creation and application:
			mount --bind -o rw "${_CUSTOM_FILE_DIR}" "${_VIRTUAL_ARCHIVE_DIR}/${_CONST_CUSTOM_DIR}"
		else #so this is just one file
			cp "${_CUSTOM_FILE_DIR}"	"${_VIRTUAL_ARCHIVE_DIR}/${_CONST_CUSTOM_DIR}"
		fi
	fi
}

#returns path relative to _ARCHIVE_DIR
get_archive_element_path(){
	local _ARCHIVE_DIR="$1"
	local _ARCHIVE_ELEM="$2"	# one of CUSTOM_DIR,

	case "${_ARCHIVE_ELEM}" in
		CUSTOM_DIR)
			echo "${_CONST_CUSTOM_DIR}"
			;;
		*)
			die "Unknown name of archive element: ${_ARCHIVE_ELEM}"
	esac
}

# call dpkg to add packages, supressing its messages except "^Selecting previousy unselected"
do_dpkg(){
	local _CHROOT_DIR="$1";		shift
	local _WILDCARDED_PKG_LIST=("${@}")	# should contain wildcards which will be expanded in chroot

	local -r _CONST_HARMLESS="^Setting up \|^Preparing to unpack \|^Unpacking \|^Processing triggers for\|^(Reading database ..."

	do_chroot "${_CHROOT_DIR}" /bin/bash <<EOF || die "Could not add package(s): ${_WILDCARDED_PKG_LIST[*]} in chroot ${_CHROOT_DIR}, using lover-level package mgmt tool"
		set -eu
		set -o pipefail	# to fail pipe when grepping the output
		# prevent automatic start of services (from initd, but systemd should handle this as well):
		echo "#!/bin/sh
				exit 101" > /usr/sbin/policy-rc.d
		chmod a+x /usr/sbin/policy-rc.d
		export LC_ALL=C		# to avoid complainign on locales during installation

		echo "Installing ${_WILDCARDED_PKG_LIST[*]} in ${_CHROOT_DIR}" >&2
		echo "Supressing the following dpkg messages considered harmless: ${_CONST_HARMLESS}"

		dpkg --install --force-confnew ${_WILDCARDED_PKG_LIST[@]} 2>&1 | grep -v "${_CONST_HARMLESS}"
EOF
}

# _PARENT can not be a mount point if both _PARENT and _RELATIVE_CHILD should remain in the original _PARENT filesystem
# Before:
# _GRANDPARENT
# 	_PARENT\
#		some_dir1\
#		some_file1
#		_RELATIVE_CHILD\
#			some_dir2\
#			some_file2
# After:
# _GRANDPARENT
#	_RELATIVE_CHILD\
#		some_dir2\
#		some_file2
#		_PARENT_NAME\
#			some_dir1\
#			some_file1
swapParentAndChildDir(){
	local _PARENT="$1"
	local _RELATIVE_CHILD="$2"	# relative to _PARENT

	local _GRANDPARENT; _GRANDPARENT="$( dirname "${_PARENT}" )"

	[ -n "$(find -L "${_GRANDPARENT}" -maxdepth 0 -samefile "${_PARENT}" 2>/dev/null)" ] && die "Cannot swap parent '${_PARENT}' and child '${_PARENT}/${_RELATIVE_CHILD}': grandparent directory '${_GRANDPARENT}' is the same as the parent '${_PARENT}'"
	[ -e "${_GRANDPARENT}/${_RELATIVE_CHILD}" ] && die "Cannot swap parent '${_PARENT}' and child '${_PARENT}/${_RELATIVE_CHILD}': child '${_RELATIVE_CHILD}' already exists in the grandparent directory '${_GRANDPARENT}'"
	[ ! -e "${_PARENT}/${_RELATIVE_CHILD}" ] && die "Cannot swap parent '${_PARENT}' and child '${_PARENT}/${_RELATIVE_CHILD}': child '${_RELATIVE_CHILD}' does not exists in the parent directory '${_PARENT}'"
	local _PARENT_NAME; _PARENT_NAME="$( basename -- "${_PARENT}" )"
	[ -e "${_PARENT}/${_RELATIVE_CHILD}/${_PARENT_NAME}" ] && die "Cannot swap ${_PARENT} and ${_PARENT}/${_RELATIVE_CHILD}: parent name ${_PARENT_NAME} already exists in the child directory ${_PARENT}/${_RELATIVE_CHILD}"
	mv -n "${_PARENT}/${_RELATIVE_CHILD}" "${_GRANDPARENT}/${_RELATIVE_CHILD}" || die "Could not move ${_PARENT}/${_RELATIVE_CHILD} to ${_GRANDPARENT}/${_RELATIVE_CHILD}"
	mv -n "${_PARENT}" "${_GRANDPARENT}/${_RELATIVE_CHILD}/${_PARENT_NAME}" || die "Could not move ${_PARENT} to ${_GRANDPARENT}/${_RELATIVE_CHILD}/${_PARENT_NAME}"
}

# Assumes initially the _PARENT directory only contains the _CHILD directory; where _CHILD can be a relatve single-component path (like: child/) or a relative multiple-components-path (like: path/to/child/
# _CHILD paths starting with "." or ".." hae not been tested.
# At the end, the _PARENT directory will directly hold the file/dir names from its _CHILD, without the _CHILD multiple/single-component path itself
# Before:
# _PARENT\
#	[path/to/child/]_CHILD\
#		some_dir\
#		some_file
# After:
# _PARENT\
#	some_dir\
#	some_file
replaceParentWithChildDirContent(){
	local _PARENT="$1"
	local _CHILD="$2"	# relative to _PARENT

	[ ! -d "${_PARENT}" ] && die "Can not replace the parent directory ${_PARENT} with the child ${_CHILD}: parent directory does not exist"
	[ ! -d "${_PARENT}/${_CHILD}" ] && die "Can not replace the parent directory ${_PARENT} with the child ${_CHILD}: child directory does not exist below parent"
	local _CHILD_LEADING_PATH; _CHILD_LEADING_PATH="$( cut -d/ -f1 <<< "${_CHILD}" )"
	[ -n "$(find "${_PARENT}" -maxdepth 1 -mindepth 1 ! -name "${_CHILD_LEADING_PATH}" )" ] && die "Can not replace the parent directory ${_PARENT} with the child ${_CHILD}: parent contains other entries than ${_CHILD_LEADING_PATH}"
	[ -n "$(find "${_PARENT}/${_CHILD}" -maxdepth 1 -mindepth 1 -name "${_CHILD_LEADING_PATH}" )" ] && die "Can not replace the parent directory ${_PARENT} with its child ${_CHILD}: the child contains its leading path component name '${_CHILD_LEADING_PATH}' among other files/directories"

	shopt -s nullglob # to ignore the literal .* in _CHILD below:
	GLOBIGNORE="*/.:*/.."	#  ignore . and .. in _CHILD below:
	mv -n "${_PARENT}/${_CHILD}"/*  "${_PARENT}/${_CHILD}"/.*  "${_PARENT}"
	GLOBIGNORE=""
	shopt -u nullglob
	rmdir "${_PARENT}/${_CHILD}"	# should be empty now
	/bin/rm -fr "${_PARENT:?}/${_CHILD_LEADING_PATH:?}"
}

# Returns 0 if the _RELEASE contains two dots (e.g.16.04.3, so it is a point release),  or returns 1 otherwise. A base release (like: 16.04) has only one dot in version decription:
isUbuntuPointRelease(){
	local _RELEASE="$1"

	awk -F . '{ exit NF!=3 }' <<< "${_RELEASE}" # exit with 0 if _RELEASE has 3 fields
}

# only remove _LINK if it is a symlink
removeIfSymlink(){
	local _LINK="$1"
	[ -L "${_LINK}" ] && /bin/rm "${_LINK}"
	return 0
}

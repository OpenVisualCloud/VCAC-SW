#!/bin/bash
# Makes the indicated software archive (SA) - one of: bootstrap, base, vca, mss, kvmgt.
# Also makes all the SAs which follow the indicated SA in the process of creation of a VCA bootable image.

set  -euEo pipefail
ARCHIVES=()		# initialize array to be filled later
ARCHIVE_PATH_IN=""	# to be set from the command line parameters
ARCHIVE_PATH_OUT="."	# path to archives to be created
DESCR=""	# to be set from the command line parameters
KERNEL_VER=""	# to be set from the command line parameters
readonly SCRIPT_DIR="$( cd "$(dirname "$0")" && pwd )"	# $0 works for more shells than  ${BASH_SOURCE[0]}
PATH="${SCRIPT_DIR}":${PATH}	# to incude the path where this script resides
PKGS_DIR_VCA=""	# optional, to be set from the command line parameters
PKGS_DIR_MSS=""	# optional, to be set from the command line parameters
TARGET=""	# to be set from the command line parameters
STOP_AFTER_BASE= # if set from the command line parameters extra SAs such as MSS and KVMGT would not be generated

stderr(){
	echo "*** $*" >&2
}
die(){
	local EXIT_CODE=$(( $? == 0 ? 99 : $? ))
	stderr "ERROR: $*"
	exit ${EXIT_CODE}
}

in_set(){
	local _VALUE="$1";
	local _SET="$2"

	for _V in ${_SET} ; do
		[ "${_V}" == "${_VALUE}" ]  && return 0
	done
	return 1
}

show_help() {
	# Keep parameters alphabetically sorted in help and in the 'case' switch which parses them.
	# Prefer single-character options, i.e. do not introduce long options without an apparent reason.
	echo "Usage: $0 [OPTIONS]
Makes the indicated software archive (SA). Also makes all the SAs which follow the indicated SA in the process of creation of a VCA bootable image.

Options:
-b, --base-and-bootstrap-only	Generate only bootstrap and base SAs. Implies \`--target bootstrap'.
-d, --descr <descrptn>	OS version description, for which the SA is prepared.
-h, --help		Show this help screen.
-i, --in-dir <dir>	The input directory containing existing predecessor SAs which will be used during creation the requested SA.
-k, --kernel <version>	The VCA kernel version, to fake the target environment.
-m, --mss <dir>		Name of directory containing the MSS installation files. Only needed when requesting the creation of the mss SA, or any of its predecessors in the process of creation of a VCA bootable image.
-o, --out-dir <dir>	The name of the destination directory for the SAs created. By default the current directory is assumed.
-t, --target <name>	The name of the SA to be created. Additionally, all the SAs will be created which follow the indicated SA in the process of creation of a VCA bootable image. Supported names:
	bootstrap, base, vca, mss, kvmgt.
-v, --vca <dir>	Name of directory containing the VCA *.deb files. Only needed when requesting the creation of the vca SA, or any of its predecessors in the process of creation of a VCA bootable image.

Examples:
To create the following SAs: bootstrap, base, vca, mss, and kvmgt in one shot (this means not using any existing SAs) in /tmp/b:
$0 -d \"UBUNTU 16.04\" -o /tmp/b -k 4.4.0-1.2.1.182.vca -t bootstrap -v /tmp/vca_pkgs_dir -m /tmp/mss_instal_dir
To create the base SA (using existing bootstrap SA from /tmp/b), and thus to create vca, mss, and kvmgt SAa as well in /tmp/o:
$0 -d \"UBUNTU 16.04\" -o /tmp/o -k 4.4.98-1.2.1.217.vca -t base -v /tmp/v -m /tmp/m/custom -i /tmp/b
To create only the kvmgt SA in /tmp/o (using existing bootstrap, base, and vca SAs from /tmp/b):
$0 -d \"UBUNTU 16.04\" -o /tmp/o -k 4.4.98-1.2.1.217.vca -t kvmgt -i /tmp/b
"
}

parse_parameters(){
	while [ $# -gt 0 ] ; do
		case "$1" in
			-b|--base-and-bootstrap-only)
				STOP_AFTER_BASE=y
				TARGET=bootstrap
				shift;;
			-d|--descr)
				DESCR="${2:-""}"
				shift; shift;;
			-h|--help)
				show_help
				exit 0;;
			-i|--in-dir)
				ARCHIVE_PATH_IN="${2:-""}"
				shift; shift;;
			-k|--kernel)
				KERNEL_VER="${2:-""}"
				shift; shift;;
			-m|--mss)
				PKGS_DIR_MSS="${2:-""}"
				shift; shift;;
			-o|--out-dir)
				ARCHIVE_PATH_OUT="${2:-""}"
				shift; shift;;
			-t|--target)
				TARGET="${2:-""}"
				case "${TARGET}" in
					bootstrap|base|vca|mss|kvmgt)
					;;
					*)
						show_help && die "Unknown target ${TARGET}"
				esac
				shift; shift;;
			-v|--vca)
				PKGS_DIR_VCA="${2:-""}"
				shift; shift;;
			*)
				show_help && die "Unknown parameter '$1'"
		esac
	done
}

check_parameters(){
	[ -z "${DESCR}" ] 	&& show_help && die "OS version description not given"
	[ -z "${KERNEL_VER}" ]	&& show_help && die "Kernel version not given"
	[ -z "${TARGET}" ]	&& show_help && die "Target software archive name not given"
	[ "${TARGET}" != bootstrap ] && [ -z "${ARCHIVE_PATH_IN}" ] && show_help && die "The input directory with existing predecessor SAs not given"

	[ -z "${STOP_AFTER_BASE}" ] && {
		[ -z "${PKGS_DIR_VCA}" ] && in_set "${TARGET}" "bootstrap base vca" && show_help && die "Name of directory containing the VCA *.deb files not given"
		[ -z "${PKGS_DIR_MSS}" ] && in_set "${TARGET}" "bootstrap base vca mss" && show_help && die "Name of directory containing the MSS installation files not given"
	}
	return 0
}

main(){
	parse_parameters "$@"
	check_parameters

	ROOTFS="$(mktemp --tmpdir --directory rootfs.XXX)"
	ROOTFS_COPY="$(mktemp --tmpdir --directory rootfs_copy.XXX)"

	# create rootfs from existing SAs in an ordered manner:
	# This rootfs is the build environment for creating the desired target SA
	if [ "${TARGET}" !=  bootstrap ] ; then
		# make rootfs with bootstrap
		ARCHIVES=(-b "${ARCHIVE_PATH_IN}"/deb.tar)
		if [ "${TARGET}" !=  base ] ; then
			# make rootfs with base
			ARCHIVES+=(-a "${ARCHIVE_PATH_IN}"/base.tar)
			if [ "${TARGET}" !=  vca ] ; then
				# make rootfs with vca
				ARCHIVES+=(-a "${ARCHIVE_PATH_IN}"/vca.tar)
			fi
		fi
		create_rootfs.sh -k "${KERNEL_VER}" -d "${DESCR}" -o "${ROOTFS}" "${ARCHIVES[@]}"
	fi

	if [ ! -d "${ARCHIVE_PATH_OUT}" ] ; then
		stderr "Going to create non-existent output directory for archives: '${ARCHIVE_PATH_OUT}'"
		mkdir -p "${ARCHIVE_PATH_OUT}" || die "Could not create output directory for archives: '${ARCHIVE_PATH_OUT}'"
	fi

	create_sw_archives

	#just in failure case could anything remain mounted
	umount "${ROOTFS}"/virtual_archive.*/custom 2>/dev/null
	/bin/rm -fr "${ROOTFS}"
	/bin/rm -fr "${ROOTFS_COPY}"
}

# create next SAs in an ordered manner:
create_sw_archives(){
	CREATE_ARCHIVE_ARGS=(-c "host:$(hostname),script:$0,pwd:$(pwd)" -d "${DESCR}" -k "${KERNEL_VER}" -r "${ROOTFS}")
	case "${TARGET}" in
		bootstrap)
			create_archive.sh "${CREATE_ARCHIVE_ARGS[@]}" -a "${ARCHIVE_PATH_OUT}"/deb.tar	-b --pre "${SCRIPT_DIR}"/archive_scripts/bootstrap/ubuntu_preadd.sh || die "Could not create the bootstrap archive"
		;&	# fall trough
		base)
			local _UBUNTU_PKG_LIST
			_UBUNTU_PKG_LIST="${SCRIPT_DIR}"/archive_scripts/base/ubuntu"$(cut -d' ' -f 2 <<<"${DESCR}")"_pkgs.list
			[ -f "${_UBUNTU_PKG_LIST}" ] || _UBUNTU_PKG_LIST="${SCRIPT_DIR}"/archive_scripts/base/ubuntu_pkgs.list
			create_archive.sh "${CREATE_ARCHIVE_ARGS[@]}" -a "${ARCHIVE_PATH_OUT}"/base.tar	--post "${SCRIPT_DIR}"/archive_scripts/base/postadd.sh -p $(awk -F# '{ print $1 }' "${_UBUNTU_PKG_LIST}")  || die "Could not create the base archive"
		[ -n "${STOP_AFTER_BASE}" ] \
			&& return # break generation sequence
		;&	# fall trough
		vca)
			create_archive.sh "${CREATE_ARCHIVE_ARGS[@]}" -a "${ARCHIVE_PATH_OUT}"/vca.tar --post "${SCRIPT_DIR}"/archive_scripts/vca/postadd.sh -g "${PKGS_DIR_VCA}" || die "Could not create the vca archive"
			# create a copy of the chroot environment, as kvmgt is assumed not to be build on top of mss:
			tar cf - -C "${ROOTFS}" . | tar xf - -C "${ROOTFS_COPY}"
		;&	# fall trough
		mss)
			[ "${DESCR}" != "UBUNTU 18.04" ] \
				&& create_archive.sh "${CREATE_ARCHIVE_ARGS[@]}" -a "${ARCHIVE_PATH_OUT}"/mss.tar	--post "${SCRIPT_DIR}"/archive_scripts/mss/ubuntu_postadd.sh -g "${PKGS_DIR_MSS}" -p $(awk -F# '{ print $1 }' "${SCRIPT_DIR}"/archive_scripts/mss/ubuntu_pkgs.list) || die "Could not create the mss archive"
		;;&	# fall trough but check the label
		bootstrap|base|vca|kvmgt)	# in every case except mss
			# overwrite the root directory parameter with ROOTFS_COPY only if it contains a rootfs (indicated by /etc), i.e. if any lower-level SA have also been created in the same pass:
			[ -d "${ROOTFS_COPY}"/etc ] && CREATE_ARCHIVE_ARGS+=( -r "${ROOTFS_COPY}" )
			create_archive.sh "${CREATE_ARCHIVE_ARGS[@]}" -a "${ARCHIVE_PATH_OUT}"/kvmgt.tar	-p $(awk -F# '{ print $1 }' "${SCRIPT_DIR}"/archive_scripts/kvmgt/ubuntu_pkgs.list) || die "Could not create the kvmgt archive"
		;;	# do not fall trough
	esac
}

stderr "Called as: $0 $*"
main "$@" && stderr "Finished: $0 $*"

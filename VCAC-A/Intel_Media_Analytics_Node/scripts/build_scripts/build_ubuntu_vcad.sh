#!/bin/bash

set  -euEo pipefail
ARCHIVES=()		# initialize array to be filled later
ARCHIVE_PATH_IN=""	# to be set from the command line parameters
ARCHIVE_PATH_OUT="."	# path to archives to be created
DESCR=""	# to be set from the command line parameters
DEFAULT_SIZE="24"      # images default size
SIZE="${DEFAULT_SIZE}"      # to set images size
KERNEL_VER=""	# to be set from the command line parameters
readonly SCRIPT_DIR="$( cd "$(dirname "$0")" && pwd )"	# $0 works for more shells than  ${BASH_SOURCE[0]}
PATH="${SCRIPT_DIR}":${PATH}	# to incude the path where this script resides
PKGS_DIR_VCA=${SCRIPT_DIR}/../artifacts	# optional, to be set from the command line parameters
PKGS_DIR_MSS=""	# optional, to be set from the command line parameters
TARGET=""	# to be set from the command line parameters
OS=UBUNTU

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

Options:
-d, --descr <descrptn>	OS version description, for which the SA is prepared.
-h, --help		Show this help screen.
-i, --in-dir <dir>	The input directory containing existing predecessor SAs which will be used during creation the requested SA.
-k, --kernel <version>	The VCA kernel version, to fake the target environment.
-m, --mss <dir>	Name of directory containing the MSS installation files. Only needed when requesting the creation of the mss SA, or any of its predecessors in the process of creation of a VCA bootable image.
-o, --out-dir <dir>	The name of the destination directory for the SAs created. By default the current directory is assumed.
-t, --target <name>	The name of the SA to be created. Additionally, all the SAs will be created which follow the indicated SA in the process of creation of a VCA bootable image. Supported names:
	bootstrap, base, vca, mss, kvmgt.
-p, --pkg <pkg version>	custom vcad package version.
-s, --image_size To set the image size
Examples:
$0 -d \"16.04\" -o /tmp/b -k 4.14.20-1.0.0.2.vca+ -t bootstrap -m ../mss -p \"2.2.21\"
"
}

mv_vca_kernel(){
	echo "*** $*" >&2
	cd ${SCRIPT_DIR}/../vca_repackage/
	rm -rf image/*
	rm -rf build/*
	rm -rf vca_mod/*
	dpkg -X linux-image-${KERNEL_VER}*.deb image
	dpkg -X vcass-modules*_amd64.deb vca_mod
	dpkg -e vcass-modules*_amd64.deb vca_mod/DEBIAN/
	rm  vca_mod/lib/modules/*/modules.*
	dpkg -b vca_mod ./
#
#	rm -rf vca_mod/lib/modules/4.14.20-1.2.3.3.vca
#	mkdir -p vca_mod/lib/modules/${KERNEL_VER}/extra
#	cp -r image/lib/modules/${KERNEL_VER}/kernel/drivers/vca vca_mod/lib/modules/${KERNEL_VER}/extra/
#	cp image/lib/modules/${KERNEL_VER}/kernel/drivers/vca/plx87xx.ko vca_mod/lib/modules/${KERNEL_VER}/extra/
#        cp -r vca_mod/lib/modules/${KERNEL_VER}/extra image/lib/modules/${KERNEL_VER}/kernel/drivers/vca/
#        cp vca_mod/lib/modules/${KERNEL_VER}/extra/plx87xx.ko image/lib/modules/${KERNEL_VER}/kernel/drivers/vca/
#	dpkg-deb -b vca_mod build
	
	rm -rf ${PKGS_DIR_VCA}/*
	cp linux-*${KERNEL_VER}*.deb ${PKGS_DIR_VCA}
#	cp vcass-config_2.3.5-2_amd64.deb ${PKGS_DIR_VCA}
        cp vcass-modules*_amd64.deb ${PKGS_DIR_VCA}
	cd ${SCRIPT_DIR}
}

parse_parameters(){
	while [ $# -gt 0 ] ; do
		case "$1" in
			-d|--descr)
				DIST="${2:-""}"
				DESCR="$OS $DIST"
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
			-p|--pkg)
				PKG_VER="${2:-""}"
				shift; shift;;
                        -f|--feature)
                                FEATURE="${2:-""}"
                                shift; shift;;
                        -s|--image_size)
				SIZE="${2:-${DEFAULT_SIZE}}"
				shift; shift;;
			-t|--target)
				TARGET="${2:-""}"
				case "${TARGET}" in
					bootstrap|base|vca|kvmgt)
					;;
					*)
						show_help && die "Unknown target ${TARGET}"
				esac
				shift; shift;;
			#-v|--vca)
			#	PKGS_DIR_VCA="${2:-""}"
			#	shift; shift;;
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

	in_set "${TARGET}" "bootstrap base vca" && [ -z "${PKGS_DIR_VCA}" ]	&& show_help && die "Name of directory containing the VCA *.deb files not given"
	#in_set "${TARGET}" "bootstrap base vca mss" && [ -z "${PKGS_DIR_MSS}" ]	&& show_help && die "Name of directory containing the MSS installation files not given"
	return 0
}

create_tar(){
	parse_parameters "$@"
	check_parameters
	mv_vca_kernel

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
	CREATE_ARCHIVE_ARGS=(-c "host:$(hostname),script:$0,pwd:$(pwd)" -d "${DESCR}" -k "${KERNEL_VER}" -r "${ROOTFS}")
	# create next SAs in an ordered manner:
	case "${TARGET}" in
		bootstrap)
			create_archive.sh "${CREATE_ARCHIVE_ARGS[@]}" -a "${ARCHIVE_PATH_OUT}"/deb.tar	-b --pre "${SCRIPT_DIR}"/archive_scripts/bootstrap/ubuntu_preadd.sh || die "Could not create the bootstrap archive"
		;&	# fall trough
		base)
			create_archive.sh "${CREATE_ARCHIVE_ARGS[@]}" -a "${ARCHIVE_PATH_OUT}"/base.tar	--post "${SCRIPT_DIR}"/archive_scripts/base/postadd.sh -p $(awk -F# '{ print $1 }' "${SCRIPT_DIR}"/archive_scripts/base/ubuntu_pkgs.list)  || die "Could not create the base archive"
		;&	# fall trough
		vca)
			create_archive.sh "${CREATE_ARCHIVE_ARGS[@]}" -a "${ARCHIVE_PATH_OUT}"/vca.tar --post "${SCRIPT_DIR}"/archive_scripts/vca/postadd.sh -g "${PKGS_DIR_VCA}" || die "Could not create the vca archive"
			# create a copy of the chroot environment, as kvmgt is assumed not to be build on top of mss:
			tar cf - -C "${ROOTFS}" . | tar xf - -C "${ROOTFS_COPY}"
		;;&	# fall trough
	#	mss)
	#		create_archive.sh "${CREATE_ARCHIVE_ARGS[@]}" -a "${ARCHIVE_PATH_OUT}"/mss.tar	--post "${SCRIPT_DIR}"/archive_scripts/mss/ubuntu_postadd.sh -g "${PKGS_DIR_MSS}" -p $(awk -F# '{ print $1 }' "${SCRIPT_DIR}"/archive_scripts/mss/ubuntu_pkgs.list) || die "Could not create the mss archive"
	#	;;&	# fall trough but check the label
		bootstrap|base|vca|kvmgt)	# in every case except mss
			# overwrite the root directory parameter with ROOTFS_COPY only if it contains a rootfs (indicated by /etc), i.e. if any lower-level SA have also been created in the same pass:
			[ -d "${ROOTFS_COPY}"/etc ] && CREATE_ARCHIVE_ARGS+=( -r "${ROOTFS_COPY}" )
			create_archive.sh "${CREATE_ARCHIVE_ARGS[@]}" -a "${ARCHIVE_PATH_OUT}"/kvmgt.tar	-p $(awk -F# '{ print $1 }' "${SCRIPT_DIR}"/archive_scripts/kvmgt/ubuntu_pkgs.list) || die "Could not create the kvmgt archive"
		;;	# do not fall trough
	esac

	#just in failure case could anything remain mounted
	umount "${ROOTFS}"/virtual_archive.*/custom 2>/dev/null
	/bin/rm -fr "${ROOTFS}"
	/bin/rm -fr "${ROOTFS_COPY}"
}

mv_tar(){
	mv ${ARCHIVE_PATH_OUT}/* ${SCRIPT_DIR}
	mv ${SCRIPT_DIR}/vca.tar ${SCRIPT_DIR}/vca-on-the-fly.tar
}

stderr "Called as: $0 $*"
create_tar "$@" && stderr "Finished: $0 $*"
mv_tar

#PKG_VER=`echo ${PKG_VER}` OS=UBUNTU DIST=16.04.3 KER_VER=`echo ${KERNEL_VER}` OUT=../INSTALL FEATURE=MSS ${SCRIPT_DIR}/quickbuild/generate_images.sh

PKG_VER=`echo ${PKG_VER}` OS=UBUNTU DIST=`echo ${DIST}` KER_VER=`echo ${KERNEL_VER}` OUT=../INSTALL FEATURE=STD ${SCRIPT_DIR}/quickbuild/generate_images.sh ${SIZE}

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

set -euEo pipefail

readonly SCRIPT_DIR="$( cd "$(dirname "$0")" && cd .. && pwd )"	# $0 works for more shells than  ${BASH_SOURCE[0]}
. "${SCRIPT_DIR}/library_image_creation.sh"

function print_help {
	echo "Synopsis:
OS={CENTOS|UBUNTU} PKG_VER=<pkg version> KER_VER=<kernel version> DIST=<distribution version> FEATURE={STD|MSS} [ARTIFACTS_DIR=<input dir>] $0

Ex. OS=UBUNTU DIST=16.04.3 PKG_VER=2.1.13 FEATURE=STD OUT=../INSTALL  KER_VER=4.10.0 BLOCKIO_DISK_SIZE=3 ./generate_images.sh
Ex. OS=CENTOS DIST=7.4     PKG_VER=2.2.16 FEATURE=MSS OUT=../INSTALL  KER_VER=3.10.0-693.11.6 quickbuild/generate_images.sh
KER_VER is mandatory for Ubuntu reference images
Supported DIST: 7.4, 16.04, 16.04.3, 16.10, 18.04
Supported KER_VER: 3.10.0, 4.4.70, 4.4.90, 4.4.98, 4.4.110, 4.10.0, 4.13*, 4.14.20*, 4.15*, 4.19*
Optional environment parameters:
ARTIFACTS_DIR: default=${SCRIPT_DIR}/../artifacts, set to input files directory;
SKIP_INITIAL_CLEANUP: default='no', set to 'yes' to skip initial cleanup step, which is designated to be run only in QB integration environment.
"
}

stderr(){
	echo "*** $*" >&2
}
die(){
	local EXIT_CODE=$(( $? == 0 ? 99 : $? ))
	stderr "ERROR: $*"
	exit ${EXIT_CODE}
}

function gen_centos {
	PERSISTENCY=$1
	ENVIRONMENT=$2
	BOOTING_METHOD=""
	if [ "${PERSISTENCY}" == PRST ]; then
		BOOTING_METHOD=$3
		MSS_ARCHIVE=${4:-""}
		SIZE=${5:-""}
		CSTATE=${6:-"ON"}
	else
		MSS_ARCHIVE=${3:-""}
		SIZE=${4:-""}
		CSTATE=${5:-"ON"}
	fi

	TARGET="${OS} image DIST=${DIST} FEATURE=${FEATURE}, PERSISTENCY=${PERSISTENCY}, ENVIRONMENT=${ENVIRONMENT}"
	echo "Creating ${TARGET}"

	local _ARCHIVES=""	# to be appended below
	local _BOOTSTRAP="$(mktemp --tmpdir bootstrap.XXX)"
	local _CREATE_IMAGE_OPTIONS=""
	local _EXTRAS=""
	local _GRUB_CFG
	local _IMAGE_TYPE
	local _IMAGE_NAME="vca_"
	local _IMAGE_NAME_EXT
	local _CSTATE_NAME=""

	# TODO: replace hardcoded paths (in VCA_*_REPO) into parameters
	export VCA_OS_REPO="file:/usr/lib/vca/repos/CentOS${DIST}/os_repo"
	export VCA_EXTRAS_REPO="file:/usr/lib/vca/repos/CentOS${DIST}/extras_repo"
	export VCA_BUILD_REPO="file:/usr/lib/vca/repos/build_repo"	# to be passed to create_local_repo.sh over sudo, option "sudo -E" is needed

	# TODO: move this ugly cleanup script to another part of QB
	sudo rm -rf /usr/lib/vca/repos/build_repo/*

	case "${PERSISTENCY}" in
		VLTL)
			_IMAGE_NAME_EXT=img
			case "${ENVIRONMENT}" in
				BRM)
					_IMAGE_TYPE=volatile-bm
					_IMAGE_NAME=vca_baremetal
					_GRUB_CFG="${SCRIPT_DIR}"/grub_cfgs/grub_baremetal.cfg
					_KS_FILE="${SCRIPT_DIR}"/ks_files/vv_production_baremetal.ks
					[ "${FEATURE}" == MSS ] && _KS_FILE="${SCRIPT_DIR}"/ks_files/vv_reference_baremetal.ks
				;;
				KVM)
					_IMAGE_TYPE=volatile-bm
					_IMAGE_NAME=vca_kvm
					_GRUB_CFG="${SCRIPT_DIR}"/grub_cfgs/grub_kvm.cfg
					_KS_FILE="${SCRIPT_DIR}"/ks_files/vv_production_kvm.ks
					_EXTRAS="-e ${VCA_EXTRAS_REPO}"
					_ARCHIVES="${_ARCHIVES} --archive amd_passthru.tar"
					prepare_amd_passthru_archive
				;;
				XEN)
					_IMAGE_TYPE=volatile-dom0
					_IMAGE_NAME=vca_xen
					_GRUB_CFG="${SCRIPT_DIR}"/grub_cfgs/grub_reference_dom0.cfg
					_KS_FILE="${SCRIPT_DIR}"/ks_files/vv_reference_dom0.ks
				;;
				DMU)
					_IMAGE_TYPE=domu
					_IMAGE_NAME=vca_domu
					_GRUB_CFG="${SCRIPT_DIR}"/grub_cfgs/grub_reference_domu.cfg
					_KS_FILE="${SCRIPT_DIR}"/ks_files/vv_reference_domu.ks
				;;
				*)
					die "Unsupported ENVIRONMENT=${ENVIRONMENT}"
				;;
			esac # ENVIRONMENT
		;;
		PRST)
			case "${ENVIRONMENT}" in
				BRM)
					case "${BOOTING_METHOD}" in
						BLK)
							[ -n "${BLOCKIO_DISK_SIZE:-}" ] && _CREATE_IMAGE_OPTIONS="${_CREATE_IMAGE_OPTIONS} --blockio-disk-size ${BLOCKIO_DISK_SIZE}"
							[ -n "${SIZE}" ] && _CREATE_IMAGE_OPTIONS="${_CREATE_IMAGE_OPTIONS} --blockio-disk-size ${SIZE}"	# the last occurence of --blockio-disk-size takes precedence
							_IMAGE_NAME_EXT=vcad
							_IMAGE_TYPE=vca-disk
							_IMAGE_NAME=vca_disk
							_GRUB_CFG="${SCRIPT_DIR}"/grub_cfgs/grub_vca_disk.cfg
							_KS_FILE="${SCRIPT_DIR}"/ks_files/vv_production_vca_disk.ks
							[ "${FEATURE}" == MSS ] && _KS_FILE="${SCRIPT_DIR}"/ks_files/vv_reference_vca_disk.ks
						;;
						NFS)
							_IMAGE_NAME_EXT=img
							_IMAGE_TYPE=persistent-bm
							_IMAGE_NAME=persistent-bm
							_GRUB_CFG="${SCRIPT_DIR}"/grub_cfgs/grub_reference_persistent.cfg
							_KS_FILE="${SCRIPT_DIR}"/ks_files/vv_reference_persistent.ks
							[ "${FEATURE}" == MSS ] && _KS_FILE="${SCRIPT_DIR}"/ks_files/vv_reference_baremetal.ks
						;;
						*)
							die "Unsupported BOOTING_METHOD=${BOOTING_METHOD}"
					esac # BOOTING_METHOD
				;;
				*)
					die "Unsupported ENVIRONMENT=${ENVIRONMENT}"
				;;
			esac # ENVIRONMENT
		;;
		*)
			die "Unsupported PERSISTENCY=${PERSISTENCY}"
		;;
	esac # PERSISTENCY

	case "${FEATURE}"  in
		MSS)
			if [ -z "${MSS_ARCHIVE}" ]; then
				die "Missing MSS_ARCHIVE name"
			fi
			_IMAGE_NAME="${_IMAGE_NAME}_reference"
			_ARCHIVES="${_ARCHIVES} --archive ${MSS_ARCHIVE}"
		;;
		STD)
		;;
		*)
			die "Unsupported FEATURE=${FEATURE}"
		;;
	esac # FEATURE
	if [ "${CSTATE}" == OFF ]; then
		_GRUB_CFG="${_GRUB_CFG%.cfg}_cstate_off.cfg"
		_CSTATE_NAME="_cstate_off"
	fi

	./create_local_repo.sh -r "${ARTIFACTS_DIR}"

	# TODO: move the ugly step of determining the VCA kernel version to another part of QB setup
	local _KER_VER="$(get_vca_kernel_version "${ARTIFACTS_DIR}"/rpmbuild/RPMS/x86_64/ "kernel-[0-9]*x86_64.rpm" )"
	[ -z "${_KER_VER}" ] && die "Could not determine kernel version from VCA package files"

	# include ${SIZE} in _IMAGE_NAME after "vca_disk", but before potential "_reference":
	[ -z "${SIZE}" ] || _IMAGE_NAME="$(awk -v size="_${SIZE}gb" 'BEGIN { FS=OFS="_" } { $2=$2 size; print }' <<< "${_IMAGE_NAME}")"

	"${SCRIPT_DIR}"/create_archive.sh -a "${_BOOTSTRAP}" -b -c "$(hostname):$0, pwd=$(pwd)"		\
		-d "${OS} ${DIST}" -f ${_KS_FILE} ${_EXTRAS} -o "${VCA_OS_REPO}" -k "${_KER_VER}" -v "${VCA_BUILD_REPO}"\
		--pre "${SCRIPT_DIR}"/archive_scripts/bootstrap/centos_preadd.sh || die "Could not create ${OS} archive"

	export VCA_ADDITIONAL_GRUB_PARAMS=quiet # todo: refactor grub.cfg online generation/replacement
	"${SCRIPT_DIR}"/create_image.sh 												\
		--bootstrap "${_BOOTSTRAP}"  ${_ARCHIVES}									\
		--image-type "${_IMAGE_TYPE}" --grub-cfg ${_GRUB_CFG}						\
		--image-name "${_IMAGE_NAME}${_CSTATE_NAME}_k${KER_VER%.*-*}"_centos"${DIST}_${PKG_VER}.${_IMAGE_NAME_EXT}"	\
		--out-dir "${OUT}" --descr "${OS} ${DIST}" 										\
		${_CREATE_IMAGE_OPTIONS}													\
		--kernel "${_KER_VER}"	|| die "Error while creating ${TARGET}"
	echo "Creating ${TARGET} has ended with status: SUCCESSFUL"
}

function gen_ubuntu {
	MSS_ARCHIVE=""
	SIZE=""
	PERSISTENCY=$1
	ENVIRONMENT=$2
	if [ ${FEATURE} == MSS ]; then
		MSS_ARCHIVE=${3:-""}
		SIZE=${4:-""}
		shift 2
	fi
	CSTATE=${3:-"ON"}


	TARGET="${OS} image DIST=${DIST} FEATURE=${FEATURE}, PERSISTENCY=${PERSISTENCY}, ENVIRONMENT=${ENVIRONMENT}"
	echo "Creating ${TARGET}"

	local _CREATE_IMAGE_OPTIONS=""
	local _IMAGE_TYPE
	local _IMAGE_NAME="vca_"	# to be appended below
	local _IMAGE_NAME_EXT
	local _GRUB_CFG
	local _ARCHIVES=""	# to be appended below
	local _CSTATE_NAME=""

	case "${PERSISTENCY}" in
		VLTL)
			_IMAGE_NAME_EXT=img
			case "${ENVIRONMENT}" in
				BRM)
					_IMAGE_TYPE=volatile-bm
					_IMAGE_NAME=vca_baremetal
					_GRUB_CFG="${SCRIPT_DIR}"/grub_cfgs/grub_baremetal.cfg
					;;
				KVM)
					_IMAGE_TYPE=volatile-kvm
					_IMAGE_NAME=vca_kvmgt
					_GRUB_CFG="${SCRIPT_DIR}"/grub_cfgs/grub_gvt.cfg
					_ARCHIVES="${_ARCHIVES} --archive kvmgt.tar --archive qemu-on-the-fly.tar"
					;;
				*)
					die "Unsupported ENVIRONMENT=${ENVIRONMENT}"
				;;
			esac
		;;
		PRST)
			[ -n "${BLOCKIO_DISK_SIZE:-}" ] && _CREATE_IMAGE_OPTIONS="${_CREATE_IMAGE_OPTIONS} --blockio-disk-size ${BLOCKIO_DISK_SIZE}"
			[ -n "${SIZE}" ] && _CREATE_IMAGE_OPTIONS="${_CREATE_IMAGE_OPTIONS} --blockio-disk-size ${SIZE}"	# the last occurence of --blockio-disk-size takes precedence
			_IMAGE_NAME_EXT=vcad
			case "${ENVIRONMENT}" in
				BRM)
					_IMAGE_TYPE=vca-disk
					_IMAGE_NAME=vca_disk
					_GRUB_CFG="${SCRIPT_DIR}"/grub_cfgs/grub_vca_disk.cfg
				;;
				KVM)
					die "Not implemented: ${TARGET}"
				;;
				*)
					die "Unsupported ENVIRONMENT=${ENVIRONMENT}"
				;;
			esac
		;;
		*)
			die "Unsupported PERSISTENCY=${PERSISTENCY}"
		;;
	esac
	case "${FEATURE}"  in
		MSS)
			if [ -z "${MSS_ARCHIVE}" ]; then
				die "Missing MSS_ARCHIVE name"
			fi
			_IMAGE_NAME="${_IMAGE_NAME}_reference"
			_ARCHIVES="${_ARCHIVES} --archive ${MSS_ARCHIVE}"
		;;
		STD)
			_ARCHIVES="${_ARCHIVES}"
		;;
		*)
			die "Unsupported FEATURE=${FEATURE}"
		;;
	esac
	if [ "${CSTATE}" == OFF ]; then
		_GRUB_CFG="${_GRUB_CFG:0:-4}_cstate_off.cfg"
		_CSTATE_NAME="_cstate_off"
	fi

	# TODO: move the ugly step of determining the VCA kernel version to another part of QB setup
	local _KER_VER="$(get_vca_kernel_version "${ARTIFACTS_DIR}" "linux-image-*_amd64.deb" )"
	[ -z "${_KER_VER}" ] && die "Could not determine kernel version from VCA package files"
	export VCA_ADDITIONAL_GRUB_PARAMS='i915.enable_rc6=0' # todo: refactor grub.cfg online generation/replacement

	# include ${SIZE} in _IMAGE_NAME after "vca_disk", but before potential "_reference":
	[ -z "${SIZE}" ] || _IMAGE_NAME="$(awk -v size="_${SIZE}gb" 'BEGIN { FS=OFS="_" } { $2=$2 size; print }' <<< "${_IMAGE_NAME}")"
	"${SCRIPT_DIR}"/create_image.sh 													\
		--bootstrap deb.tar --archive base.tar --archive vca-on-the-fly.tar ${_ARCHIVES}\
		--image-type "${_IMAGE_TYPE}" --grub-cfg ${_GRUB_CFG}							\
		--image-name "${_IMAGE_NAME}${_CSTATE_NAME}_k${KER_VER%.*-*}"_ubuntu"${DIST}_${PKG_VER}.${_IMAGE_NAME_EXT}"\
		--out-dir "${OUT}" --descr "${OS} ${DIST}" 											\
		${_CREATE_IMAGE_OPTIONS}														\
		--kernel "${_KER_VER}"  	|| die "Error while creating ${TARGET}"

	echo "Creating ${TARGET} has ended with status: SUCCESSFUL"
}

# input:
#	"${ARTIFACTS_DIR}/vca-qemu*.deb"
# output: regular archive for KVM: packages + custom dir + postadd script
# the archive is created without test-adding packages, assuming blindly that they are not public and that their dependencies are met
# if they need dependencies during add, the build environment may handle this...
# TODO: move this ugly step to another part of QB setup
function prepare_qemu_archive () {
	local _QEMU_DEB="$1"
	local _ARCHIVE_NAME="$2"

	local _VIRT_ARCHIVE="$(mktemp --directory --tmpdir archive.XXX)/virtual"
	mkdir -p "${_VIRT_ARCHIVE}"

	# it is possible that there are two qemu versions (2.10, 2.12), only one could be passed to chroot
	cp ${_QEMU_DEB} "${_VIRT_ARCHIVE}" || die "Cannot copy QUEMU files from ${_QEMU_DEB} to ${_VIRT_ARCHIVE}"

	# returns a dir full of links named appropriately:
	local _VIRTUAL_ARCHIVE_DIR; _VIRTUAL_ARCHIVE_DIR="$(mktemp --directory --tmpdir archive.XXX)" || die "Could not create directory for virtual archive"
	build_virtual_archive "${_VIRTUAL_ARCHIVE_DIR}"	\
		"" 											\
		"" 											\
		"" 											\
		"${_VIRT_ARCHIVE}"							\
		"" 											\
		"${SCRIPT_DIR}"/archive_scripts/qemu/postadd.sh

	create_info_file "${_VIRTUAL_ARCHIVE_DIR}" "${OS}" "${DIST}"  "${KER_VER}" "$(hostname):$0, pwd=$(pwd)" ""
	assemble_archive "${_VIRTUAL_ARCHIVE_DIR}" "${_ARCHIVE_NAME}" || die "Could not create archive ${_ARCHIVE_NAME} from ${_VIRTUAL_ARCHIVE_DIR}"
	destroy_virtual_archive "${_VIRTUAL_ARCHIVE_DIR}"
	rm -rf "${_VIRT_ARCHIVE}"
}

function prepare_amd_passthru_archive() {
	local _ARCHIVE_NAME="amd_passthru.tar"
	local _VIRTUAL_ARCHIVE_DIR; _VIRTUAL_ARCHIVE_DIR="$(mktemp --directory --tmpdir archive.XXX)" || die "Could not create directory for virtual archive"
	build_virtual_archive "${_VIRTUAL_ARCHIVE_DIR}"	\
		"" 											\
		"" 											\
		"" 											\
		"" 											\
		"" 											\
		"${SCRIPT_DIR}"/archive_scripts/kvm/amd_passthru_postadd.sh
	create_info_file "${_VIRTUAL_ARCHIVE_DIR}" "${OS}" "${DIST}"  "${KER_VER}" "$(hostname):$0, pwd=$(pwd)" ""
	assemble_archive "${_VIRTUAL_ARCHIVE_DIR}" "${_ARCHIVE_NAME}" || die "Could not create archive ${_ARCHIVE_NAME} from ${_VIRTUAL_ARCHIVE_DIR}"
	destroy_virtual_archive "${_VIRTUAL_ARCHIVE_DIR}"
}

# input:
#	"${ARTIFACTS_DIR}/*.deb"
# output: regular archive with VCA: packages + custom dir + postadd script
# the archive is created without test-adding packages, assuming blindly that they are not public and that their dependencies are met
# if they need dependencies during add, the build environment may handle this...
# TODO: move this ugly step to another part of QB setup
function prepare_vca_archive () {
	local _VCA_DEB_DIR="$1"
	local _ARCHIVE_NAME="$2"

	local _ARCHIVE="$(mktemp --directory --tmpdir archive.XXX)"
	local _IGNORED_FILES="vca-qemu"

	rm "${_VCA_DEB_DIR}"/linux-image-*-dbg*_amd64.deb 2>/dev/null || true	# remove the kernel package with debug symbols
	ls "${_VCA_DEB_DIR}"/*.deb | grep -vP "${_IGNORED_FILES}" | xargs -I files cp files "${_ARCHIVE}" || die "Cannot copy DEB files from ${_VCA_DEB_DIR} to ${_ARCHIVE}"

	# returns a dir full of links named appropriately:
	local _VIRTUAL_ARCHIVE_DIR; _VIRTUAL_ARCHIVE_DIR="$(mktemp --directory --tmpdir archive.XXX)" || die "Could not create directory for virtual archive"
	build_virtual_archive "${_VIRTUAL_ARCHIVE_DIR}"	\
		"" 											\
		"" 											\
		"" 											\
		"${_ARCHIVE}"							\
		"" 											\
		"${SCRIPT_DIR}"/archive_scripts/vca/postadd.sh

	create_info_file "${_VIRTUAL_ARCHIVE_DIR}" "${OS}" "${DIST}"  "${KER_VER}" "$(hostname):$0, pwd=$(pwd)" ""
	assemble_archive "${_VIRTUAL_ARCHIVE_DIR}" "${_ARCHIVE_NAME}" || die "Could not create archive ${_ARCHIVE_NAME} from ${_VIRTUAL_ARCHIVE_DIR}"
	destroy_virtual_archive "${_VIRTUAL_ARCHIVE_DIR}"
	/bin/rm -fr "${_ARCHIVE}"
}

# TODO: is Xen needed for Ubuntu now?
function prepare_xen_archive () {
	:
}

function validate_parameters {
	case $DIST in
		7.3|7.4|7.6|8.0|16.04|16.04.3|16.10|18.04)
		;;
		*)
			die "Provided distribution is not supported"
		;;
	esac
}

# Get the version of the kernel from the name of the VCA *.deb or *.rpm package named _PKG_EXPECTED (e.g. linux-image-4.10.0-1.2.1.138.vca_1.0_amd64.deb, kernel-3.10.0-693.11.6.el7.2.1.179.VCA.x86_64.rpm -> 3.10.0-693.11.6.el7.2.1.179.VCA.x86_64
# TODO: kernel-4.4.0_1.2.0.10.VCA-1.x86_64.rpm -> 4.4.0-1.2.0.10.VCA)
# The RPM package name should be <name>-<version>-<localrelease>.<arch>.rpm The <name> is the package name (e.g. funky-gui-devel-libs, the '-'s separating stuff are counted from the end), <version> is the upstream version (no '-'!), <localrelease> is a local version (there might be several rounds of patches on the same upstream base, it usually includes the target system), <arch> is the architecture
# The rpm filename should be consistent with informaton  be obtained by: rpm --queryformat "%{NAME}-%{VERSION}-%{RELEASE}.%{ARCH}.rpm" -qp file.rpm
# The rpm filename should be consistent with directory name: /lib/modules/"%{VERSION}-%{RELEASE}.%{ARCH}"
# The deb filename should be: packagename_version-release_architecture.deb
get_vca_kernel_version(){
	local _VCA_PKG_DIR="$1"
	local _PKG_EXPECTED="$2" # e.g. "kernel-4*x86_64.rpm" or "linux-image-*_amd64.deb"

	local _PKG_REAL; _PKG_REAL="$(cd "${_VCA_PKG_DIR}" ; ls --ignore "*-dbg*_amd64.deb" ${_PKG_EXPECTED} )" || die "Could not list the content of ${_VCA_PKG_DIR}"

	case "${OS}" in
		UBUNTU)
			echo "${_PKG_REAL}" | awk -F_ '{ print $1 }' | awk -F- '{ for(i=3; i<NF; i++) printf "%s-", $i } END { print $NF }'
		;;
		CENTOS)
			# package file name in the form: kernel-3.10.0-693.11.6.el7.2.1.179.VCA.x86_64.rpm
			_PKG_REAL="$( awk -F. '{ printf "%s",$1; for(i=2; i<NF; i++) printf ".%s",$i }' <<< "${_PKG_REAL}" )"
			# package name in the form: kernel-3.10.0-693.11.6.el7.2.1.179.VCA.x86_64
			# TODO: package file name in the form: kernel-4.4.0_1.2.0.10.VCA-1.x86_64.rpm
			local KER_MAJOR=$(echo "${KER_VER}" | sed -re 's/([0-9]\.[0-9]+).*/\1/')
			[ "${KER_MAJOR}" == 3.10 ] && echo "${_PKG_REAL}" | sed -re 's/kernel-(.+VCA\.x86_64).*/\1/'
			[ "${KER_MAJOR}" == 4.18 ] && echo "${_PKG_REAL}" | sed -re 's/kernel-(.+VCA\.x86_64).*/\1/'
			[ "${KER_MAJOR}" == 4.4  ] && echo "${_PKG_REAL}" | sed -re 's/kernel-(.+VCA).*/\1/' | tr '_' '-'
			[ "${KER_MAJOR}" == 4.14 ] && echo "${_PKG_REAL}" | sed -re 's/kernel-(.+VCA).*/\1/' | tr '_' '-'
		;;
		*)
			die "Unsupported OS: ${OS}"
	esac
}

generate_images(){
	validate_parameters

	[ -d "${ARTIFACTS_DIR:=${SCRIPT_DIR}/../artifacts}" ] || die "ARTIFACTS_DIR: '${ARTIFACTS_DIR}' must exist and be populated with VCA artifacts needed for requested image"
	if [ "${SKIP_INITIAL_CLEANUP:-no}" != yes ]; then
		local _TMP_DIR="$(dirname "$(mktemp -u)")" # umount everything where the mount point begins with _TMP_DIR, starting with the deepest paths
		${SCRIPT_DIR}/quickbuild/initial_cleanup.sh --dir-to-rm "${OUT}" --clear-tmp-dir "${_TMP_DIR}"
	fi
	mkdir -p "${OUT}"

	# TODO: move this ugly step to another part of QB setup
	# building vca-disk with dracut does NOT require (kernel) modules matching the currently-created image
	# [ "${OS}" == CENTOS ] && yum --assumeyes --quiet --nogpgcheck localinstall  "${ARTIFACTS_DIR}"/rpmbuild/RPMS/x86_64/kernel-3*x86_64.rpm "${ARTIFACTS_DIR}"/rpmbuild/RPMS/x86_64/vcass-modules-3*x86_64.rpm 2>&1 #  --errorlevel=10

	if [ "$OS" == "UBUNTU"  ]; then
		# TODO: move this ugly step to another part of QB setup
		# patching dracut-core /usr/lib/dracut/modules.d/09console-setup/module-setup.sh as per bug=828915:
		# Need to 'cd /' temporarily, because absolute name of the file to be patched is used
		(cd / ; patch -p0 < "${SCRIPT_DIR}"/persistent_files/dracut.patch > /dev/null 2>&1 || true )

		# TODO: move this ugly step to another part of QB setup
		prepare_vca_archive "${ARTIFACTS_DIR}" vca-on-the-fly.tar
	fi

	# PERSISTENCY supported values: VLTL, PRST
	# ENVIRONMENT supported values: BRM, KVM, XEN
	case "${KER_VER}" in
		3.10.0*)
			case "${FEATURE}" in
				MSS)
					gen_centos VLTL BRM "mss2018R2HF1_189_k3.10.tar"
					gen_centos PRST BRM BLK "mss2018R2HF1_189_k3.10.tar" 24
				;;
				STD)
					gen_centos VLTL KVM
					gen_centos PRST BRM BLK 24
				;;
				*)
					die "Unsupported FEATURE=${FEATURE}"
			esac
		;;
		4.4.0|4.4.1*)
			case "${FEATURE}" in
				MSS)
					# gen_centos VLTL DMU
				;;
				STD)
					# gen_centos VLTL XEN
				;;
				*)
					die "Unsupported FEATURE=${FEATURE}"
			esac
		;;
		4.4.98)
			case "${FEATURE}" in
				MSS)
					# gen_ubuntu VLTL BRM
					# gen_ubuntu PRST BRM
					# gen_ubuntu PRST BRM 24
				;;
				STD)
					# gen_ubuntu VLTL BRM
				;;
				*)
					die "Unsupported FEATURE=${FEATURE}"
			esac
		;;
		4.10.0|4.13*)
			case "${FEATURE}" in
				MSS)
					# gen_ubuntu VLTL BRM
					# gen_ubuntu PRST BRM
				;;
				STD)
					cat /dev/null > qemu-on-the-fly.tar
					prepare_qemu_archive "${ARTIFACTS_DIR}/vca-qemu-v2.10*.deb" qemu-on-the-fly.tar
					gen_ubuntu VLTL KVM
				;;
				*)
					die "Unsupported FEATURE=${FEATURE}"
			esac
		;;
		4.14*)
			case "${OS}" in
				UBUNTU)
					case "${FEATURE}" in
						MSS)
							gen_ubuntu VLTL BRM "mss2018R2HF1_189_k4.14_ubuntu.tar"
							gen_ubuntu PRST BRM "mss2018R2HF1_189_k4.14_ubuntu.tar" 24
							gen_ubuntu VLTL BRM "mss2018R2HF1_189_k4.14_ubuntu.tar" "" OFF
							gen_ubuntu PRST BRM "mss2018R2HF1_189_k4.14_ubuntu.tar" 24 OFF
						;;
						STD)
							echo -e "no STD images selected\n"
						;;
						*)
							die "Unsupported FEATURE=${FEATURE}"
					esac
				;;
				CENTOS)
					case "${FEATURE}" in
						MSS)
							gen_centos VLTL BRM "mss2018R2HF1_189_k4.14.tar"
							gen_centos PRST BRM BLK "mss2018R2HF1_189_k4.14.tar" 24
							gen_centos VLTL BRM "mss2018R2HF1_189_k4.14.tar" "" OFF
							gen_centos PRST BRM BLK "mss2018R2HF1_189_k4.14.tar" 24 OFF
						;;
						STD)
							echo -e "no STD images selected\n"
							# gen_centos PRST BRM BLK 8
						;;
						*)
							die "Unsupported FEATURE=${FEATURE}"
					esac
				;;
				*)
					die "Unsupported OS=${OS}"
			esac
		;;
		4.15*|4.19*)
			case "${FEATURE}" in
				MSS)
					echo -e "no MSS images selected\n"
				;;
				STD)
					# gen_ubuntu VLTL BRM
					# gen_ubuntu PRST BRM
					gen_ubuntu VLTL BRM OFF
					gen_ubuntu PRST BRM OFF
				;;
				*)
					die "Unsupported FEATURE=${FEATURE}"
				;;
			esac
		;;
		4.18)
			gen_centos VLTL BRM
			#gen_centos PRST BRM BLK 24
		;;
		*)
			die "Unsupported KER_VER=${KER_VER}"
		;;
	esac
}

stderr "Called as: $0 $*"
generate_images "$@" && stderr "Finished: $0 $*"

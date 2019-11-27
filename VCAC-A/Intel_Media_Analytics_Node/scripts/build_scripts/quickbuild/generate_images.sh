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
	echo 'Help:
<variable assignments> $0

Ex. OS=UBUNTU DIST=16.04.3 PKG_VER=2.1.0 FEATURE=STD OUT="../INSTALL"  KER_VER=4.10.0 BLOCKIO_DISK_SIZE=3 ./generate_images.sh
Ex. OS=CENTOS DIST=7.4 PKG_VER=2.2.16 FEATURE=MSS OUT=../INSTALL  KER_VER=3.10.0-693.11.6 quickbuild/generate_images.sh
FEATURE is mandatory.	Supported FEATURE values: STD, MSS
KER_VER is mandatory for Ubuntu reference images
Supported OS: CENTOS, UBUNTU
Supported DIST: 7.4, 16.04, 16.04.3, 16.10
Supported KER_VER: 3.10.0, 4.4.70, 4.4.90, 4.4.98, 4.4.110, 4.10.0, 4.13*, 4.14.20*
'
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
	[ "${PERSISTENCY}" == PRST ] && BOOTING_METHOD=$3

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
					die "Unsuported ENVIRONMENT=${ENVIRONMENT}"
				;;
			esac # ENVIRONMENT
		;;
		PRST)
			case "${ENVIRONMENT}" in
				BRM)
					case "${BOOTING_METHOD}" in
						BLK)
							[ -n "${BLOCKIO_DISK_SIZE:-}" ] && _CREATE_IMAGE_OPTIONS="${_CREATE_IMAGE_OPTIONS} --blockio-disk-size ${BLOCKIO_DISK_SIZE}"
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
							die "Unsuported BOOTING_METHOD=${BOOTING_METHOD}"
					esac # BOOTING_METHOD
				;;
				*)
					die "Unsuported ENVIRONMENT=${ENVIRONMENT}"
				;;
			esac # ENVIRONMENT
		;;
		*)
			die "Unsupported PERSISTENCY=${PERSISTENCY}"
		;;
	esac # PERSISTENCY

	case "${FEATURE}"  in
		MSS)
			_IMAGE_NAME="${_IMAGE_NAME}_reference"
#			_ARCHIVES="${_ARCHIVES} --archive mss2018R1.tar"
		;;
		STD)
		;;
		*)
			die "Unsuported FEATURE=${FEATURE}"
		;;
	esac # FEATURE

	./create_local_repo.sh -r ../artifacts

	# TODO: move the ugly step of determining the VCA kernel version to another part of QB setup
	local _KER_VER="$(get_vca_kernel_version "${WORKSPACE}"/../artifacts/rpmbuild/RPMS/x86_64/ "kernel-[0-9]*x86_64.rpm" )"
	[ -z "${_KER_VER}" ] && die "Could not determine kernel version from VCA package files"

	"${SCRIPT_DIR}"/create_archive.sh -a "${_BOOTSTRAP}" -b -c "$(hostname):$0, pwd=$(pwd)"		\
		-d "${OS} ${DIST}" -f ${_KS_FILE} ${_EXTRAS} -o "${VCA_OS_REPO}" -k "${_KER_VER}" -v "${VCA_BUILD_REPO}"\
		--pre "${SCRIPT_DIR}"/archive_scripts/bootstrap/centos_preadd.sh || die "Could not create ${OS} archive"

	"${SCRIPT_DIR}"/create_image.sh 												\
		--bootstrap "${_BOOTSTRAP}"  ${_ARCHIVES}									\
		--image-type "${_IMAGE_TYPE}" --grub-cfg ${_GRUB_CFG}						\
		--image-name "${_IMAGE_NAME}"_centos"${DIST}_${PKG_VER}.${_IMAGE_NAME_EXT}"	\
		--out-dir "${OUT}" --descr "${OS} ${DIST}" 										\
		${_CREATE_IMAGE_OPTIONS}													\
		--kernel "${_KER_VER}"	|| die "Error while creating ${TARGET}"
	echo "Creating ${TARGET} has ended with status: SUCCESSFUL"
}

function gen_ubuntu {
	PERSISTENCY=$1
	ENVIRONMENT=$2
	SIZE=${3:-""}

	TARGET="${OS} image DIST=${DIST} FEATURE=${FEATURE}, PERSISTENCY=${PERSISTENCY}, ENVIRONMENT=${ENVIRONMENT}"
	echo "Creating ${TARGET}"

	local _CREATE_IMAGE_OPTIONS=""
	local _IMAGE_TYPE
	local _IMAGE_NAME="vca_"	# to be appended below
	local _IMAGE_NAME_EXT
	local _GRUB_CFG
	local _ARCHIVES=""	# to be appended below

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
					die "Unsuported ENVIRONMENT=${ENVIRONMENT}"
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
					die "Unsuported ENVIRONMENT=${ENVIRONMENT}"
				;;
			esac
		;;
		*)
			die "Unsupported PERSISTENCY=${PERSISTENCY}"
		;;
	esac
	case "${FEATURE}"  in
		MSS)
			_IMAGE_NAME="${_IMAGE_NAME}_reference"
			_ARCHIVES="${_ARCHIVES} --archive mss.tar"
		;;
		STD)
			_ARCHIVES="${_ARCHIVES}"
		;;
		*)
			die "Unsuported FEATURE=${FEATURE}"
		;;
	esac

	# TODO: move the ugly step of determining the VCA kernel version to another part of QB setup
	local _KER_VER="$(get_vca_kernel_version "${WORKSPACE}"/../artifacts "linux-image-*_amd64.deb" )"
	[ -z "${_KER_VER}" ] && die "Could not determine kernel version from VCA package files"

	# include ${SIZE} in _IMAGE_NAME after "vca_disk", but before potential "_reference":
	[ -z "${SIZE}" ] || _IMAGE_NAME="$(awk -v size="${SIZE}" 'BEGIN { FS=OFS="_" } { $2=$2 size; print }' <<< "${_IMAGE_NAME}")"

		#--bootstrap deb.tar --archive base.tar --archive vca-on-the-fly.tar ${_ARCHIVES}\
	"${SCRIPT_DIR}"/create_image.sh 													\
		--bootstrap deb.tar --archive base.tar --archive vca-on-the-fly.tar ${_ARCHIVES}\
		--image-type "${_IMAGE_TYPE}" --grub-cfg ${_GRUB_CFG}							\
		--image-name "${_IMAGE_NAME}_k${KER_VER%.*-*}"_ubuntu"${DIST}_${PKG_VER}.${_IMAGE_NAME_EXT}" \
		--out-dir "${OUT}" --descr "${OS} ${DIST}" 											\
		${_CREATE_IMAGE_OPTIONS}														\
		--kernel "${_KER_VER}"  	|| die "Error while creating ${TARGET}"

	echo "Creating ${TARGET} has ended with status: SUCCESSFUL"
}

# input:
#	"$WORKSPACE/../artifacts/*VcaQemu*.zip"
# output: regular archive for KVM: packages + custom dir + postadd script
# the archive is created without test-adding packages, assuming blindly that they are not public and that their dependencies are met
# if they need dependencies during add, the build environment may handle this...
# TODO: who is goiong to remove the _TMP_ARCHIVE ?
# TODO: move this ugly step to another part of QB setup
function prepare_qemu_archive () {
	local _QEMU_ZIP="$1"
	local _ARCHIVE_NAME="$2"

	local _ARCHIVE="$(mktemp --directory --tmpdir archive.XXX)"
	local _TMP_ARCHIVE="${_ARCHIVE}/tmp"
	local _VIRT_ARCHIVE="${_ARCHIVE}/virtual"
	mkdir -p "${_TMP_ARCHIVE}"
	mkdir -p "${_VIRT_ARCHIVE}"

	unzip "${_QEMU_ZIP}" -d "${_TMP_ARCHIVE}" > /dev/null || die "Could not decompress ${_QEMU_ZIP} into ${_TMP_ARCHIVE}"
	cp "${_TMP_ARCHIVE}"/vca-qemu*.deb "${_VIRT_ARCHIVE}" || die "Cannot copy files from ${_TMP_ARCHIVE} to ${_VIRT_ARCHIVE}"
	cp "${_TMP_ARCHIVE}"/roms/seabios/out/bios.bin "${_VIRT_ARCHIVE}" || die "Cannot copy file from ${_TMP_ARCHIVE} to ${_VIRT_ARCHIVE}"


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
}

# input:
#	"${WORKSPACE}/../artifacts/*.deb"
# output: regular archive with VCA: packages + custom dir + postadd script
# the archive is created without test-adding packages, assuming blindly that they are not public and that their dependencies are met
# if they need dependencies during add, the build environment may handle this...
# TODO: who is goiong to remove the _TMP_ARCHIVE ?
# TODO: move this ugly step to another part of QB setup
function prepare_vca_archive () {
	local _VCA_DEB_DIR="$1"
	local _ARCHIVE_NAME="$2"

	local _ARCHIVE="$(mktemp --directory --tmpdir archive.XXX)"

	rm "${_VCA_DEB_DIR}"/linux-image-*-dbg*_amd64.deb 2>/dev/null || true	# remove the kernel package with debug symbols
	cp "${_VCA_DEB_DIR}"/*.deb "${_ARCHIVE}" || die "Cannot copy files from ${_VCA_DEB_DIR} to ${_ARCHIVE}"

	# returns a dir full of links named appropriately:
	local _VIRTUAL_ARCHIVE_DIR; _VIRTUAL_ARCHIVE_DIR="$(mktemp --directory --tmpdir archive.XXX)" || die "Could not create directory for virtual archive"
	build_virtual_archive "${_VIRTUAL_ARCHIVE_DIR}"	\
		"" 											\
		"" 											\
		"" 											\
		"${_VCA_DEB_DIR}"							\
		"" 											\
		"${SCRIPT_DIR}"/archive_scripts/vca/postadd.sh

	create_info_file "${_VIRTUAL_ARCHIVE_DIR}" "${OS}" "${DIST}"  "${KER_VER}" "$(hostname):$0, pwd=$(pwd)" ""
	assemble_archive "${_VIRTUAL_ARCHIVE_DIR}" "${_ARCHIVE_NAME}" || die "Could not create archive ${_ARCHIVE_NAME} from ${_VIRTUAL_ARCHIVE_DIR}"
	destroy_virtual_archive "${_VIRTUAL_ARCHIVE_DIR}"
}

# TODO: is Xen needed for Ubuntu now?
function prepare_xen_archive () {
	:
}

function validate_parameters {
	case $DIST in
		7.3|7.4|16.04|16.04.3|18.04)
		;;
		*)
			echo $DIST
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
			[ "${KER_MAJOR}" == 4.4 ] && echo "${_PKG_REAL}" | sed -re 's/kernel-(.+VCA).*/\1/' | tr '_' '-'
		;;
		*)
			die "Unsupported OS: ${OS}"
	esac
}

generate_images(){
	validate_parameters

	WORKSPACE="$(pwd)"
	rm -rf "$OUT"
	mkdir "$OUT"

	# TODO: move this ugly step to another part of QB setup
	# building vca-disk with dracut does NOT require (kernel) modules matching the currently-created image
	# [ "${OS}" == CENTOS ] && yum --assumeyes --quiet --nogpgcheck localinstall  "${WORKSPACE}"/../artifacts/rpmbuild/RPMS/x86_64/kernel-3*x86_64.rpm "${WORKSPACE}"/../artifacts/rpmbuild/RPMS/x86_64/vcass-modules-3*x86_64.rpm 2>&1 #  --errorlevel=10

	# TODO: move this ugly cleanup step to another part of QB setup
	# umount everything where the mount point begins with _MNT_DIR, starting with the deepest paths:
	local _MNT_DIR="/tmp/"
	awk '{ print $2 }' /proc/mounts | grep "^${_MNT_DIR}" | awk -F/ '{ printf "%d*%s\n" , NF, $0 }' | sort -rn | awk -F'*' '{ print $2 }' | xargs umount 2>/dev/null
	# wicked to get the path of the current TMPDIR (Which seems unset) to check it is non-empty
        local _TMP_DIR_NAME="$( dirname "$(mktemp)" )"
	# clear everything in _TMP_DIR_NAME except krb5cc*:
	[ -n "${_TMP_DIR_NAME}" ] && find "${_TMP_DIR_NAME}" -mindepth 1 -maxdepth 1 ! -name krb5cc\* -exec /bin/rm -fr {} 2>/dev/null \;
	losetup -D

	if [ "$OS" == "UBUNTU"  ]; then
		# TODO: move this ugly step to another part of QB setup
		# patching dracut-core /usr/lib/dracut/modules.d/09console-setup/module-setup.sh as per bug=828915:
		# Need to 'cd /' temporarily, because absolute name of the file to be patched is used
		(cd / ; patch -p0 < "${WORKSPACE}"/persistent_files/dracut.patch > /dev/null 2>&1 || true )

		# TODO: move this ugly step to another part of QB setup
		prepare_vca_archive "${WORKSPACE}"/../artifacts vca-on-the-fly.tar
	fi

	# PERSISTENCY supported values: VLTL, PRST
	# ENVIRONMENT supported values: BRM, KVM, XEN
	case "${KER_VER}" in
		3.10.0*)
			case "${FEATURE}" in
				MSS)
					#gen_centos VLTL BRM
					gen_centos PRST BRM BLK
				;;
				STD)
					#gen_centos VLTL BRM
					#gen_centos VLTL KVM
					gen_centos PRST BRM BLK
                ;;
				*)
                    die "Unsupported FEATURE=${FEATURE}"
			esac
		;;
		4.4.0|4.4.1*)
			case "${FEATURE}" in
				MSS)
					gen_centos VLTL DMU
				;;
				STD)
					gen_centos VLTL XEN
				;;
				*)
					die "Unsupported FEATURE=${FEATURE}"
			esac
		;;
		4.4.98)
			case "${FEATURE}" in
				MSS)
					gen_ubuntu VLTL BRM
					gen_ubuntu PRST BRM
					gen_ubuntu PRST BRM 24
				;;
				STD)
					gen_ubuntu VLTL BRM
				;;
				*)
					die "Unsupported FEATURE=${FEATURE}"
			esac
		;;
		4.10.0|4.13*)
			case "${FEATURE}" in
				MSS)
					gen_ubuntu VLTL BRM
					gen_ubuntu PRST BRM
				;;
				STD)
					cat /dev/null > qemu-on-the-fly.tar
					prepare_qemu_archive "${WORKSPACE}/../artifacts/*VcaQemu*.zip" qemu-on-the-fly.tar
					gen_ubuntu VLTL KVM
				;;
				*)
					die "Unsupported FEATURE=${FEATURE}"
			esac
		;;
		4.14*)
			case "${FEATURE}" in
				MSS)
					gen_ubuntu PRST BRM $1
				;;
				STD)
					#echo -e "no STD images selected\n"
					gen_ubuntu VLTL BRM
				;;
				*)
					die "Unsupported FEATURE=${FEATURE}"
			esac
		;;
                4.19*)
                        case "${FEATURE}" in
                                MSS)
                                        gen_ubuntu PRST BRM $1
                                ;;
                                STD)
                                        #echo -e "no STD images selected\n"
                                        #gen_ubuntu VLTL BRM
                     			gen_ubuntu PRST BRM $1
                                ;;
                                *)
                                        die "Unsupported FEATURE=${FEATURE}"
                        esac
                ;;
		*)
			die "Unsupported KER_VER=${KER_VER}"
		;;
	esac
}

stderr "Called as: $0 $*"
generate_images "$@" && stderr "Finished: $0 $*"

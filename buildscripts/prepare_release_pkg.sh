#!/bin/bash

# This is helper utility for VCASS team only.
# It is intended to generate release package from QuickBuild artifacts.
# (And document generation process).
#
# Release package should not contain any binary files
# (to be explicit: any files that are not OK adhering to BDBA scan).
# Release package should not contain this file in top-level directory,
# to not confuse released package users.
#
# For reproducible results, there should be least amount of manual work.

# shellcheck source=library_fs.sh
source "$(cd "$(dirname "$(readlink -f "$0")")"; pwd)/"library_fs.sh

usage(){
echo -n 'Mandatory arguments:
--in-from-qb <qbdir> : top directory containing all files needed for release,
	(non-releasable files could be present,
	ie: this could be our gksfiler mounted resource)
--out-pkg <outname.zip> : name of resulting archive

Optional arguments:
--only-pattern <pattern>
	prepare only configurations which match <pattern> (by grep -E) (see available configurations in '"$0"'::main())
'
}

set -Eeuo pipefail
stderr(){ echo -e "*** $*" >&2; }
die(){
	EXIT_CODE=$?
	EXIT_CODE=$((EXIT_CODE == 0 ? 99 : EXIT_CODE))
	stderr "FATAL ${FUNCNAME[1]}(): $*"
	exit ${EXIT_CODE}
}
trap 'die "(cmd: $BASH_COMMAND)"' ERR

INPUT_DIR=
OUT_PKG_NAME=
OUTPUT_DIR=
ONLY_PATTERN= # optional

parse_parameters(){
	while [ $# -gt 1 ]; do
		case "${1}" in
		--in-from-qb) INPUT_DIR="${2}";;
		--only-pattern) ONLY_PATTERN="${2}";;
		--out-pkg)
			OUT_PKG_NAME="$(grep '.zip$' <<< "${2}")" \
				|| die '--out-pkg <pkgname> must be zip archive'
		;;
		*) die "unrecognized parameter: '$1', run $0 without arguments for usage information";;
		esac
		shift 2
	done
	if [ -z "${INPUT_DIR}" ] || [ -z "${OUT_PKG_NAME}" ]; then
		usage
		exit 1
	fi
}

main(){
	command -v filterdiff >/dev/null || die 'filterdiff utility is necessary for patch transformations'

	# all commands get input from INPUT_DIR and put their result into OUTPUT_DIR,
	# which will finally be zipped to OUT_PKG_NAME
	OUTPUT_DIR="$(mktemp -d)"

	copy_sources build_scripts
	copy_sources vca_apps
	copy_sources vcass-modules
	prepare_if_selected extract_vca_kernel_patches_for_centos 3.10.0-693
	prepare_if_selected extract_vca_kernel_patches_for_centos 3.10.0-957 sgx
	prepare_if_selected extract_vca_kernel_patches_for_ubuntu 4.14.20
	prepare_if_selected extract_vca_kernel_patches_for_ubuntu 4.15.0	sgx
	prepare_if_selected extract_vca_kernel_patches_for_ubuntu 4.19.0
	prepare_if_selected copy_kernel_patch_and_config_for_centos 5.1.16
	prepare_if_selected extract_windows_img_build_scripts

	# extract files intended to use directly by customer during build
	extract_and_copy README_master_build.txt 'build_scripts*.tar.gz'
	mv "${OUTPUT_DIR}"/README{_master_build,}.txt
	extract_and_copy build.sh 'build_scripts*.tar.gz'
	extract_and_copy library_fs.sh 'build_scripts*.tar.gz'
	extract_and_copy master_build.sh 'build_scripts*.tar.gz'
	extract_and_copy download_dependencies.sh 'build_scripts*.tar.gz'
	extract_and_copy Dockerfiles/build_centos.dockerfile 'build_scripts*.tar.gz'
	extract_and_copy Dockerfiles/build_ubuntu.dockerfile 'build_scripts*.tar.gz'

	# make final zip archive and cleanup
	[ -f "${OUT_PKG_NAME}" ] && {
		stderr "move existing ${OUT_PKG_NAME} file to /tmp as backup"
		mv "${OUT_PKG_NAME}" /tmp || {
			stderr "removing ${OUT_PKG_NAME} despite failed backup"
			rm "${OUT_PKG_NAME}"
		}
	}
	mkdir -p "$(dirname "${OUT_PKG_NAME}")"
	cd "${OUTPUT_DIR}"
	# files added by excluded .patch are already in its place at appropriate moment
	find . -type f | sort \
		| grep -v 'Add-files-generated-by-fakeroot*debian-rules-clean.patch' \
		| zip -q -@ "${OUT_PKG_NAME}"
	cd
	rm -rf "${OUTPUT_DIR}"
}

prepare_if_selected(){
	if ! grep -qE "${ONLY_PATTERN}" <<< "$*"; then
		echo "Configuration $* not selected by --only-pattern"
		return
	fi
	"$@"
}

# find "*${_KERNEL_VERSION}/**/kernel.{config,patch}" in ${INPUT_DIR} and copy them into ${OUTPUT_DIR}
copy_kernel_patch_and_config_for_centos(){
	local _FIND_PATT _IN_FILE _KERNEL_VERSION _OUT_DIR _FILE_NAME
	_KERNEL_VERSION="$1"
	_OUT_DIR="${OUTPUT_DIR}/centos-with-kernel-${_KERNEL_VERSION}"
	mkdir -p "${_OUT_DIR}"
	for _FILE_NAME in kernel.config kernel.patch; do
		_FIND_PATT="*centos*${_KERNEL_VERSION}*/${_FILE_NAME}"
		_IN_FILE="$(find_single "${INPUT_DIR}" -type f -ipath "${_FIND_PATT}")"
		cp -a "${_IN_FILE}" "${_OUT_DIR}"
	done
}

# find $1*tar.gz and copy it into ${OUTPUT_DIR}
# note: will ignore duplicate findings, becasue source is always the same and duplicates are expected
copy_sources(){
	local _SOURCE_NAME _IN_FILE _PKG_VER _FIND_PATT
	_SOURCE_NAME="$1"
	_FIND_PATT="${_SOURCE_NAME}*tar.gz" # like vcass-modules_4.4.155-1.2.6.265.vca_2.6.265_src.tar.gz
	_IN_FILE="$(find_any "${INPUT_DIR}" -type f -name "${_FIND_PATT}")" \
		|| die "Could not find ${_FIND_PATT}"
	echo "Using ${_IN_FILE} as ${_SOURCE_NAME} sources." >&2
	# do not use whole _IN_FILE name, as it likely contains "some" kernel name (which will be confusing when used for other kernels)
	# use _PKG_VER in out file name, but this is not crucial, so || true
	_PKG_VER=$(grep -Po '(\d+\.\d+\.\d+)' <<< "${_IN_FILE}" | tail -1) || true
	cp -a "${_IN_FILE}" "${OUTPUT_DIR}/${_SOURCE_NAME}_${_PKG_VER}_src.tar.gz"
}

# extract file matching $1 from ${INPUT_DIR}/$2 into $3 (by default=${OUTPUT_DIR})
# $2 must be tar.gz
# unnecessarily slow when invoked multiple times for the same big archive
extract_and_copy(){
	local _FILE_PATH _FIND_PATT _OUT_DIR _ARCHIVE _OUT_FILE
	_FILE_PATH="$1"
	_FIND_PATT="$2"
	_OUT_DIR="${3-${OUTPUT_DIR}}"
	_OUT_FILE="${_OUT_DIR}/${_FILE_PATH}"
	_ARCHIVE="$(find_single "${INPUT_DIR}" -type f -name "${_FIND_PATT}")"
	mkdir -p "$(dirname "${_OUT_FILE}")"
	tar --to-stdout --wildcards -xzf "${_ARCHIVE}" "*/${_FILE_PATH}" > "${_OUT_FILE}"
}

# extract windows image generation scripts to tmp dir, then zip them to ${OUTPUT_DIR}
extract_windows_img_build_scripts(){
(
	local _TMP_DIR _IN_ARCHIVE _FIND_PATT
	_FIND_PATT='build_scripts*.tar.gz'
	_IN_ARCHIVE="$(find_single "${INPUT_DIR}" -type f -name "${_FIND_PATT}")"
	_TMP_DIR="$(mktemp -d -p "${OUTPUT_DIR}")" # use ${OUTPUT_DIR} as parent to have free cleanup on failure
	cd "${_TMP_DIR}"
	# extract necessary sources to ${_TMP_DIR}; ignore two top levels in build_scripts
	tar --strip-components=2 --wildcards -f "${_IN_ARCHIVE}" '*/windows' -C . -x --exclude=Log.txt --exclude=WS2012R2 --exclude=.gitattributes
	zip -qr "${OUTPUT_DIR}/windows_image_generation_files.zip" .
	cd
	rm -rf "${_TMP_DIR}"
)
}

# extract vca kernel patches for ubuntu, kernel version passed via $1 (tested only for 4.19.0)
# (we want to distribute patches as (multiple) .patch files)
extract_vca_kernel_patches_for_ubuntu(){
	local _KERNEL_VERSION _KERNEL_PATCHES_ARCHIVE _OUT_KERNEL_DIR
	_KERNEL_VERSION="$1"
	_OUT_KERNEL_DIR="${OUTPUT_DIR}/ubuntu-with-kernel-${_KERNEL_VERSION}"
	mkdir -p "${_OUT_KERNEL_DIR}"
	_KERNEL_PATCHES_ARCHIVE="$(find_single "${INPUT_DIR}" -name "vca_patches_kernel_${_KERNEL_VERSION}*_src.tar.gz" -type f)"
	tar -f "${_KERNEL_PATCHES_ARCHIVE}" -C "${_OUT_KERNEL_DIR}" -xz
}

# extract vca kernel patches for centos, kernel version passed via $1 (tested only for 3.10.0-693)
# (we do not want to distribute our kernel.src.rpm)
extract_vca_kernel_patches_for_centos(){
	local _KERNEL_VERSION _FIND_PATT _KERNEL_SRC_RPM _OUT_DIR _OUT_PATCH_FILE _OUT_CONF_FILE
	_KERNEL_VERSION="$1"
	_FIND_PATT="kernel-${_KERNEL_VERSION}*VCA.src.rpm"
	_KERNEL_SRC_RPM="$(find_single "${INPUT_DIR}" -name "${_FIND_PATT}" -type f)"
	_OUT_DIR="${OUTPUT_DIR}/centos-with-kernel-${_KERNEL_VERSION}"
	_OUT_CONF_FILE="${_OUT_DIR}/kernel.config"
	_OUT_PATCH_FILE="${_OUT_DIR}/kernel.patch"
	mkdir -p "${_OUT_DIR}"
	extract_file_from_rpm 'vca_patches.patch' "${_KERNEL_SRC_RPM}" | split_dot_config_out "${_OUT_CONF_FILE}" > "${_OUT_PATCH_FILE}"
}

# exctract file named $1 from rpm named $2 to stdout (output could be easily redirected)
extract_file_from_rpm(){
	local _FILE _RPM
	_FILE="$1"
	_RPM="$2"
	rpm2cpio "${_RPM}" | cpio -i --quiet --to-stdout "${_FILE}"
}

# split stdin that .config resulting after patches application will be saved in $1
# and all patches (chunks) NOT manipulating .config will emited to stdout
split_dot_config_out(){
	local _IN _CONF_FILE
	_CONF_FILE="$1"
	_IN=$(</dev/stdin)
	filterdiff -i '*/\.config' <<< "${_IN}" | patch -p1 "${_CONF_FILE}" >/dev/null # '>/dev/null' to make 'patch' quiet
	filterdiff -x '*/\.config' <<< "${_IN}" \
		| sed '1 i# patches changing only .config file were removed, .config is distributed as separate file'
}

parse_parameters "$@"
stderr "Called as: $0 $*"
main
stderr "Finished: $0 $*"

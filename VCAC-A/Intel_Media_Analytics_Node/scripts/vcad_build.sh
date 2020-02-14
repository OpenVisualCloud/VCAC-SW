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

readonly SCRIPT_DIR="$( cd "$(dirname "$0")" && pwd )"	# $0 works for more shells than  ${BASH_SOURCE[0]}
. "${SCRIPT_DIR}/build_scripts/library_image_creation.sh"

# Constants
readonly ROOT_DIR="$(dirname ${SCRIPT_DIR})"
readonly PKG_ROOT_DIR="$(dirname ${ROOT_DIR})"
readonly CACHE_DIR="${PKG_ROOT_DIR}/cache"
readonly TAR_DIR="${ROOT_DIR}/tar"
readonly DEB_DIR="${ROOT_DIR}/deb"
readonly DEFAULT_BUILD_DIR="${ROOT_DIR}/build"
readonly DEFAULT_RUN_MODE="host"
readonly DEFAULT_SIZE=48

readonly FFMPEG_NAME="FFmpeg"

readonly MSS_OCL_NAME="MediaServerStudioEssentials2019R1HF3_16.9_10020.tar.gz"
readonly MSS_OCL_LINK="https://github.com/Intel-Media-SDK/MediaSDK/releases/download/MSS-KBL-2019-R1-HF1/${MSS_OCL_NAME}"

readonly OPENVNO_DATE="2020.1.023"
readonly OPENVNO_NAME="l_openvino_toolkit_p_$OPENVNO_DATE.tgz"
readonly OPENVNO_LINK="http://registrationcenter-download.intel.com/akdlm/irc_nas/16345/${OPENVNO_NAME}"

readonly KERNEL_PATCH_ARCHIVE="${TAR_DIR}/ubuntu18.04_kernel4.19.87_patch.tar.gz"
readonly MODULES_PATCH_ARCHIVE="${TAR_DIR}/vcass-modules-4.19-patch.tar.gz"

readonly VCAA_DOCKER_NAME="vcaa/ubuntu-18.04-test"
readonly VCAA_DOCKER_VERSION="1.0"

readonly KERNEL_VERSION="4.19"
readonly KERNEL_VER="4.19.87"
readonly KERNEL_SRC_NAME="linux-${KERNEL_VER}"
readonly KERNEL_SRC_ARCHIVE="${KERNEL_SRC_NAME}.tar.gz"
readonly KERNEL_SRC_LINK="https://mirrors.edge.kernel.org/pub/linux/kernel/v4.x/${KERNEL_SRC_ARCHIVE}"

readonly VCA_SRC_ARCHIVE="VCAC-A_R2.tar.gz"
readonly VCA_SRC_LINK="https://github.com/OpenVisualCloud/VCAC-SW/archive/VCAC-A_R2.tar.gz"
readonly MODULES_SRC_NAME="vca_modules_2.3.26_src"
readonly MODULES_SRC_ARCHIVE="${MODULES_SRC_NAME}.tar.gz"

readonly INITIAL_DEBUG_LEVEL=1

readonly MODULES_BUILD_SCRIPT_PATH="./generate_modules.sh"

readonly DEFAULT_TASKS_TO_RUN="build,package,install"

readonly ADDITONAL_BINARY_NAME="kbl_dmc_ver1_04.bin"
readonly ADDITONAL_BINARY_LINK="https://cgit.freedesktop.org/drm/drm-firmware/tree/i915/${ADDITONAL_BINARY_NAME}"

readonly OPENCV_NAME="opencv_python-4.1.2.30-cp36-cp36m-manylinux1_x86_64.whl" 
readonly OPENVN_LINK="https://files.pythonhosted.org/packages/c0/a9/9828dfaf93f40e190ebfb292141df6b7ea1a2d57b46263e757f52be8589f/${OPENCV_NAME}"
readonly NUMPY_NAME="numpy-1.18.1-cp36-cp36m-manylinux1_x86_64.whl"
readonly NUMPY_LINK="https://files.pythonhosted.org/packages/62/20/4d43e141b5bc426ba38274933ef8e76e85c7adea2c321ecf9ebf7421cedf/${NUMPY_NAME}"
# Globals
BUILD_DIR=${DEFAULT_BUILD_DIR}
SET_SIZE=${DEFAULT_SIZE}
SET_OPT=
RUN_MODE="${DEFAULT_RUN_MODE}"
DOWNLOAD_USING_CACHE=0
PARAM_HTTP_PROXY=
PARAM_HTTPS_PROXY=
PARAM_NO_PROXY=
SKIP_DOCKER=0
NO_CLEAN=0
INSTALL_VCAD=0
TASKS_TO_RUN=${DEFAULT_TASKS_TO_RUN}
DEBUG_LEVEL=2

show_help() {
	# Keep parameters alphabetically sorted in help and in the 'case' switch which parses them.
	# Prefer single-character options, i.e. do not introduce long options without an apparent reason.
	echo "Usage: $0 [OPTIONS]
This script is used to generate vcad file for VCAA.
It does following tasks:
- Build docker image based on Dockerfile used to build kernel and modules.
- Download kernel and modules from official release then apply VCAA patches, and do the build.
- Generate vcad based on the kernel and modules, mss will be include.
Requirement:
- Need sudo to run docker.
- Internet connection.
- Docker installed.
- wget installed.
The scripts main input is:
-

Options:
-b, --build-dir <path>	The directory where to do the build, default: ${DEFAULT_BUILD_DIR}.
-c, --cache	If there's file under <PACKAGE_ROOT>/cache, use this file instead of downloading.
-d, --debug	Enable debug log.
-i, --install-vcad	Build full vcad image.
-m, --mode <mode>	host or docker, host mode manages docker, docker mode runs the actual build. Default mode is host, which will run docker mode in docker container automatically.
-n, --no-clean	Don't do clean before build.
-s, --skip-docker	Skip creating docker image if you're sure it's been created correctly.
-t, --tasks <tasks>	tasks to be run in docker, separated with comma, default: \"build,package\".
-x, --http-proxy <http_proxy>	Set the http_proxy environment variable.
-y, --https-proxy <https_proxy>	Set the https_proxy environment variable.
-z, --no-proxy <no_proxy>	Set the no_proxy environment variablie.
-e, --size <image size> To set the image size, default: ${DEFAULT_SIZE}
-o, --opt <build options(IPS,FULL,BASIC，EXTENDED)> To choose which build option and this command is necessary .
-h, --help	Show this help screen.
"
}

parse_parameters(){
	debug ${INITIAL_DEBUG_LEVEL} "++ parse_parameters ($@)"

	while [ $# -gt 0 ] ; do
		case "$1" in
			-b|--build-dir)
				BUILD_DIR="${2:-${DEFAULT_BUILD_DIR}}"
				shift; shift;;
			-c|--cache)
				DOWNLOAD_USING_CACHE=1
				shift;;
			-d|--debug)
				DEBUG_LEVEL=1
				shift;;
			-i|--install-vcad)
				INSTALL_VCAD=1
				shift;;
			-m|--mode)
				RUN_MODE="${2:-${DEFAULT_RUN_MODE}}"
				shift; shift;;
			-n|--no-clean)
				NO_CLEAN=1
				shift;;
			-s|--skip-docker)
				SKIP_DOCKER=1
				shift;;
			-t|--tasks)
				TASKS_TO_RUN="${2:-${DEFAULT_TASKS_TO_RUN}}"
				shift; shift;;
			-x|--http-proxy)
				PARAM_HTTP_PROXY="${2:-}"
				shift; shift;;
			-y|--https-proxy)
				PARAM_HTTPS_PROXY="${2:-}"
				shift; shift;;
			-z|--no-proxy)
				PARAM_NO_PROXY="${2:-}"
				shift; shift;;
            -e|--size)
			    SET_SIZE="${2:-${DEFAULT_SIZE}}"
				shift; shift;;
			-o|--opt)
			    SET_OPT="${2:-}"
				shift; shift;;			
			-h|--help)
				show_help
				exit 0;;
			*)
				show_help && die "Unknown parameter '$1'"
		esac
	done
        if [ "${SET_OPT}" == "" ];then
		show_help
		exit 0
	else
	        if [ "${SET_OPT}" != "IPS" -a  "${SET_OPT}" != "FULL" -a "${SET_OPT}" != "BASIC" -a "${SET_OPT}" != "EXTENDED" ];then
			show_help
			exit 0
		fi
	fi

	debug ${INITIAL_DEBUG_LEVEL} "-- parse_parameters"
}

check_parameters() {
	debug ${DEBUG_LEVEL} "++ check_parameters ($@)"

	[ -z "${BUILD_DIR}" ] && show_help && die "No build directory given"
        echo $SET_OPT

	debug ${DEBUG_LEVEL} "-- check_parameters"
}

_create_dir() {
	debug ${DEBUG_LEVEL} "++ _create_dir ($@)"

	local _DIR=$1
	if [ -d "${_DIR}" ]; then
		rm -rf "${_DIR}" || die "Failed to clean up directory: ${_DIR}"
	fi
	mkdir -p "${_DIR}" || die "Failed to create new directory: ${_DIR}"

	debug ${DEBUG_LEVEL} "-- _create_dir"
}

_copy() {
	debug ${DEBUG_LEVEL} "++ _copy ($@)"

	cp "$@" || die "Failed to copy $@"

	debug ${DEBUG_LEVEL} "-- _copy"
}

_check_binary() {
	debug ${DEBUG_LEVEL} "++ _check_binary ($@)"

	local _BIN_NAME="$1"
	local _BIN_PATH=$(command -v ${_BIN_NAME})
	[[ $? -ne 0 || -z "${_BIN_PATH}" ]] && die "Can't find ${_BIN_NAME} in PATH, ${_BIN_NAME} not installed?"
	notice "Using ${_BIN_PATH}"

	debug ${DEBUG_LEVEL} "-- _check_binary"
}

requirement_check() {
	debug ${DEBUG_LEVEL} "++ requirement_check ($@)"

	# check if required software are installed
	if [ "${RUN_MODE}" == "host" ]; then
		_check_binary docker
	elif [ "${RUN_MODE}" == "docker" ]; then
		_check_binary dpkg
		_check_binary git
		_check_binary make
		_check_binary patch
		_check_binary tar
		_check_binary wget
		_check_binary unzip
	fi

	debug ${DEBUG_LEVEL} "-- requirement_check"
}

setup_env() {
	debug ${DEBUG_LEVEL} "++ setup_env ($@)"

	# setup build directory
	if [[ "${RUN_MODE}" == "host" || ! -d "${BUILD_DIR}" ]]; then
		[[ ! -d "${BUILD_DIR}" || ${NO_CLEAN} -eq 0 ]] && _create_dir ${BUILD_DIR}
	fi

	debug ${DEBUG_LEVEL} "-- setup_env"
}

rm_docker_image() {
	debug ${DEBUG_LEVEL} "++ rm_docker_image ($@)"

	docker rmi -f ${VCAA_DOCKER_NAME}:${VCAA_DOCKER_VERSION} || die "Failed to remove docker image, ${VCAA_DOCKER_NAME}:${VCAA_DOCKER_VERSION}"

	debug ${DEBUG_LEVEL} "-- rm_docker_image"
}

build_docker() {
	debug ${DEBUG_LEVEL} "++ build_docker ($@)"

	# check if docker image already exists
	local _DOCKER_IMAGE=`docker image ls ${VCAA_DOCKER_NAME}:${VCAA_DOCKER_VERSION} --format "{{.ID}}: {{.Repository}} {{.Tag}}"`
	[ -n "${_DOCKER_IMAGE}" ] && rm_docker_image

	# create docker build context directory and copy Dockerfile
	local _DOCKER_DIR="${BUILD_DIR}/docker"
	_create_dir ${_DOCKER_DIR}
	_copy "${SCRIPT_DIR}/Dockerfile" "${_DOCKER_DIR}/Dockerfile"

	# build docker image
	local _PROXY_ARGS=""
	if [ -n "${PARAM_HTTP_PROXY}" ]; then
		_PROXY_ARGS="${_PROXY_ARGS} --build-arg HTTP_PROXY=${PARAM_HTTP_PROXY}"
	fi
	if [ -n "${PARAM_HTTPS_PROXY}" ]; then
		_PROXY_ARGS="${_PROXY_ARGS} --build-arg HTTPS_PROXY=${PARAM_HTTPS_PROXY}"
	fi
	if [ -n "${PARAM_NO_PROXY}" ]; then
		_PROXY_ARGS="${_PROXY_ARGS} --build-arg NO_PROXY=${PARAM_NO_PROXY}"
	fi

	docker build --no-cache ${_PROXY_ARGS} -t "${VCAA_DOCKER_NAME}:${VCAA_DOCKER_VERSION}" ${_DOCKER_DIR} || die "Failed to build docker image"

	debug ${DEBUG_LEVEL} "-- build_docker"
}

_download() {
	debug ${DEBUG_LEVEL} "++ _download ($@)"

	local _DOWNLOAD_SRC="$1"
	local _DOWNLOAD_DEST="$2"
	local _DOWNLOAD_FILE_NAME=

	if [ $# -gt 2 ]; then
		_DOWNLOAD_FILE_NAME="$3"
	fi

	if [[ ${DOWNLOAD_USING_CACHE} -eq 1 && -n "${_DOWNLOAD_FILE_NAME}" && -f "${CACHE_DIR}/${_DOWNLOAD_FILE_NAME}" ]]; then
		notice "Using cached file: ${CACHE_DIR}/${_DOWNLOAD_FILE_NAME}"
		_copy "${CACHE_DIR}/${_DOWNLOAD_FILE_NAME}" "${_DOWNLOAD_DEST}"
	else
		local _HTTP_PROXY="${http_proxy-}"
		local _HTTPS_PROXY="${https_proxy-}"
		local _NO_PROXY="${no_proxy-}"
		[ -n "${PARAM_HTTP_PROXY}" ] && _HTTP_PROXY="${PARAM_HTTP_PROXY}"
		[ -n "${PARAM_HTTPS_PROXY}" ] && _HTTPS_PROXY="${PARAM_HTTPS_PROXY}"
		[ -n "${PARAM_NO_PROXY}" ] && _NO_PROXY="${PARAM_NO_PROXY}"
		debug ${DEBUG_LEVEL} "Using proxy: http_proxy=${_HTTP_PROXY}, https_proxy=${_HTTPS_PROXY}, no_proxy=${_NO_PROXY}"
		http_proxy=${_HTTP_PROXY} https_proxy=${_HTTPS_PROXY} no_proxy=${_NO_PROXY} wget "${_DOWNLOAD_SRC}" -O "${_DOWNLOAD_DEST}" || die "Failed to download ${_DOWNLOAD_SRC}"
	fi

	debug ${DEBUG_LEVEL} "-- _download"
}

_extract_tgz() {
	debug ${DEBUG_LEVEL} "++ _extract_tgz ($@)"

	local _SRC_ARCHIVE="$1"
	local _DEST_DIR="$2"

	tar xzf "${_SRC_ARCHIVE}" -C "${_DEST_DIR}" || die "Failed to extract ${_SRC_ARCHIVE} to ${_DEST_DIR}"

	debug ${DEBUG_LEVEL} "-- _extract_tgz"
}

_extract_zip() {
	debug ${DEBUG_LEVEL} "++ _extract_zip ($@)"

	local _SRC_ARCHIVE="$1"
	local _DEST_DIR="$2"

	unzip "${_SRC_ARCHIVE}" -d "${_DEST_DIR}" || die "Failed to extract ${_SRC_ARCHIVE} to ${_DEST_DIR}"

	debug ${DEBUG_LEVEL} "-- _extract_zip"
}

_cd() {
	debug ${DEBUG_LEVEL} "++ _cd ($@)"

	local _DEST_DIR="$1"
	cd "${_DEST_DIR}" || die "Failed to cd to ${_DEST_DIR}"

	debug ${DEBUG_LEVEL} "-- _cd"
}

_apply_patch() {
	debug ${DEBUG_LEVEL} "++ _apply_patch ($@)"

	local _SRC_DIR="$1"
	local _PATCH_DIR="$2"

	_cd "${_SRC_DIR}"
	for _PATCH in ${_PATCH_DIR}/*.patch; do
		patch -p1 < "${_PATCH}" || die "Failed to apply patch ${_PATCH} under ${_SRC_DIR}"
	done
	_cd -

	debug ${DEBUG_LEVEL} "-- _apply_patch"
}

_apply_patch_git() {
	debug ${DEBUG_LEVEL} "++ _apply_patch_git ($@)"

	local _SRC_DIR="$1"
	local _PATCH_DIR="$2"
	local _INITIAL_COMMIT="$3"

	_cd "${_SRC_DIR}"
	git init || die "Failed to init git repo in ${_SRC_DIR}"
	git config user.name "foo" || die "Failed to config git user name in ${_SRC_DIR}"
	git config user.email "foo@bar" || die "Failed to config git user email in ${_SRC_DIR}"
	git add . || die "Failed to run git add command in ${_SRC_DIR}"
	git commit -m "${_INITIAL_COMMIT}" || die "Failed to do initial commit in ${_SRC_DIR}"
	git am ${_PATCH_DIR}/*.patch || die "Failed to apply patch under ${_PATCH_DIR} in ${_SRC_DIR}"
	_cd -

	debug ${DEBUG_LEVEL} "-- _apply_patch_git"
}

_find_most_recent_kernel_devel() {
	debug ${DEBUG_LEVEL} "++ _find_most_recent_kernel_devel ($@)"
	local _SEARCH_DIR="$1"
	local _PATH_PATTERN="$2"

	# find the latest kernel-devel or linux-headers distribution package
	# (could be more than one on dev machine or due to QB issue)
	# printf format is "timestamp filepath"
	# declaring KERNEL_HEADER_PKG separately from assigning to avoid masking return values:
	KERNEL_HEADER_PKG="$(find "${_SEARCH_DIR}" -type f -name "${_PATH_PATTERN}" -printf '%T@ %p\n' \
		| sort -rn | cut -d' ' -f2-
	)"
	local _FILE_COUNT; _FILE_COUNT="$(wc -l <<< "${KERNEL_HEADER_PKG}")"
	if [ 1 -ne "${_FILE_COUNT}" ] ; then
		warn "Exactly one kernel header package expected, got ${_FILE_COUNT} items:\n${KERNEL_HEADER_PKG}"
		KERNEL_HEADER_PKG="$(head -1 <<< "${KERNEL_HEADER_PKG}")"
		warn "Continuing with: '${KERNEL_HEADER_PKG}'"
	fi
	notice "${KERNEL_HEADER_PKG}"
	debug ${DEBUG_LEVEL} "-- _find_most_recent_kernel_devel"
}

build_kernel_and_modules() {
	debug ${DEBUG_LEVEL} "++ build_kernel_and_modules ($@)"

	# create the directories
	local _VCAA_KERNEL_DIR="${BUILD_DIR}/vcaa_kernel"
	[[ ! -d "${_VCAA_KERNEL_DIR}" || ${NO_CLEAN} -eq 0 ]] && _create_dir "${_VCAA_KERNEL_DIR}"
	local _KERNEL_DIR="${_VCAA_KERNEL_DIR}/kernel"
	[[ ! -d "${_KERNEL_DIR}" || ${NO_CLEAN} -eq 0 ]] && _create_dir "${_KERNEL_DIR}"
	local _MODULES_DIR="${_VCAA_KERNEL_DIR}/modules"
	[[ ! -d "${_MODULES_DIR}" || ${NO_CLEAN} -eq 0 ]] && _create_dir "${_MODULES_DIR}"
	local _DOWNLOAD_DIR="${_VCAA_KERNEL_DIR}/download"
	[[ ! -d "${_DOWNLOAD_DIR}" || ${NO_CLEAN} -eq 0 ]] && _create_dir "${_DOWNLOAD_DIR}"
	local _DOWNLOAD_DIR_VCA="${_DOWNLOAD_DIR}/vca"
	[[ ! -d "${_DOWNLOAD_DIR_VCA}" || ${NO_CLEAN} -eq 0 ]] && _create_dir "${_DOWNLOAD_DIR_VCA}"
	local _PATCH_DIR="${_VCAA_KERNEL_DIR}"/patch
	[[ ! -d "${_PATCH_DIR}" || ${NO_CLEAN} -eq 0 ]] && _create_dir "${_PATCH_DIR}"
	local _KERNEL_PATCH_DIR="${_PATCH_DIR}"/kernel
	[[ ! -d "${_KERNEL_PATCH_DIR}" || ${NO_CLEAN} -eq 0 ]] && _create_dir "${_KERNEL_PATCH_DIR}"
	local _MODULES_PATCH_DIR="${_PATCH_DIR}"/modules
	[[ ! -d "${_MODULES_PATCH_DIR}" || ${NO_CLEAN} -eq 0 ]] && _create_dir "${_MODULES_PATCH_DIR}"

	# download and extract kernel source
	[[ ! -f "${_DOWNLOAD_DIR}/${KERNEL_SRC_ARCHIVE}" || ${NO_CLEAN} -eq 0 ]] && _download "${KERNEL_SRC_LINK}" "${_DOWNLOAD_DIR}/${KERNEL_SRC_ARCHIVE}" "${KERNEL_SRC_ARCHIVE}"
	[ ${NO_CLEAN} -eq 0 ] && _extract_tgz "${_DOWNLOAD_DIR}/${KERNEL_SRC_ARCHIVE}" "${_KERNEL_DIR}"

	# apply kernel patch
	[ ${NO_CLEAN} -eq 0 ] && _extract_tgz "${KERNEL_PATCH_ARCHIVE}" "${_KERNEL_PATCH_DIR}"
	[ ${NO_CLEAN} -eq 0 ] && _apply_patch_git "${_KERNEL_DIR}/${KERNEL_SRC_NAME}" "${_KERNEL_PATCH_DIR}" "${KERNEL_SRC_NAME}"

	# download and extract modules source
	[[ ! -f "${_DOWNLOAD_DIR}/${VCA_SRC_ARCHIVE}" || ${NO_CLEAN} -eq 0 ]] && _download "${VCA_SRC_LINK}" "${_DOWNLOAD_DIR}/${VCA_SRC_ARCHIVE}" "${VCA_SRC_ARCHIVE}"
	[ ${NO_CLEAN} -eq 0 ] && _extract_tgz "${_DOWNLOAD_DIR}/${VCA_SRC_ARCHIVE}" "${_DOWNLOAD_DIR_VCA}"
#	[ ${NO_CLEAN} -eq 0 ] && _extract_tgz "${_DOWNLOAD_DIR_VCA}/${MODULES_SRC_ARCHIVE}" "${_MODULES_DIR}"
	[ ${NO_CLEAN} -eq 0 ] && _copy -r "${_DOWNLOAD_DIR_VCA}/VCAC-SW-VCAC-A_R2/modules" "${_MODULES_DIR}/../"

	# apply modules patch
	[ ${NO_CLEAN} -eq 0 ] && _extract_tgz "${MODULES_PATCH_ARCHIVE}" "${_MODULES_PATCH_DIR}"
	[ ${NO_CLEAN} -eq 0 ] && _apply_patch_git "${_MODULES_DIR}" "${_MODULES_PATCH_DIR}" "${MODULES_SRC_NAME}"

	# remove previous built files
	rm -rf ${_KERNEL_DIR}/*.changes || die "Failed to remove *.changes kernel files"
	rm -rf ${_KERNEL_DIR}/*.tar.gz || die "Failed to remove *.tar.gz kernel files"
	rm -rf ${_KERNEL_DIR}/*.dsc || die "Failed to remove *.dsc kernel files"
	rm -rf ${_KERNEL_DIR}/*.deb || die "Failed to remove *.deb kernel files"
         
	# build kernel
	_cd "${_KERNEL_DIR}/${KERNEL_SRC_NAME}"
	local _COMMIT_ID_KERNEL=$(git rev-parse --short HEAD)
	[ -z "${_COMMIT_ID_KERNEL}" ] && die "Failed to get kernel commit id"
        grep_config=`cat arch/x86/configs/x86_64_vcxa_defconfig |grep CONFIG_RETPOLINE=y`
        if [ ${grep_config} == "" ];then
           echo "CONFIG_RETPOLINE=y" >> arch/x86/configs/x86_64_vcxa_defconfig
        fi
	make x86_64_vcxa_defconfig
	OS="UBUNTU" PKGVERSION=${_COMMIT_ID_KERNEL} make -j`nproc` deb-pkg || die "Failed to build kernel"
	dpkg -i ${_KERNEL_DIR}/linux-headers-*.deb || die "Failed to install kernel headers"
        

	# remove previous built files
	local _MODULES_OUTPUT_DIR="${_VCAA_KERNEL_DIR}/output"
	rm -rf ${_MODULES_OUTPUT_DIR}/*.tar.gz || die "Failed to remove *.tar.gz module files"
	rm -rf ${_MODULES_OUTPUT_DIR}/*.deb || die "Failed to remove *.deb module files"

	# build modules
	_cd ${_MODULES_DIR}
	local _COMMIT_ID_MODULES=$(git rev-parse --short HEAD)
	[ -z "${_COMMIT_ID_MODULES}" ] && die "Failed to get modules commit id"
	KERNEL_HEADER_PKG=""
	_find_most_recent_kernel_devel "${_KERNEL_DIR}" "linux-headers-${KERNEL_VERSION}*_amd64.deb"
	OS=UBUNTU PKG_VER=2.1.1 LINUX_HEADERS_OR_KERNEL_DEVEL_PKG=${KERNEL_HEADER_PKG} ${MODULES_BUILD_SCRIPT_PATH} || die "Failed to build modules"
	rm -rf vca_mod || die "Failed to remove vca_mod dir"
	dpkg -X ${_MODULES_DIR}/output/vcass-modules*_amd64.deb vca_mod || die "Failed to extract modules deb"
	dpkg -e ${_MODULES_DIR}/output/vcass-modules*_amd64.deb vca_mod/DEBIAN/ || die "Failed to extract modules control info"
	rm -rf vca_mod/lib/modules/*/modules.* || die "Failed to remove modules files"
	dpkg -b vca_mod ./ || die "Failed to build modules deb"

	debug ${DEBUG_LEVEL} "-- build_kernel_and_modules"
}

build_vcad() {
	debug ${DEBUG_LEVEL} "++ build_vcad ($@)"

	local _VCAA_KERNEL_DIR="${BUILD_DIR}/vcaa_kernel"
	local _KERNEL_DIR="${_VCAA_KERNEL_DIR}/kernel"
	local _MODULES_DIR="${_VCAA_KERNEL_DIR}/modules"
	local _MODULES_OUTPUT_DIR="${_MODULES_DIR}/output"
	local _DOWNLOAD_DIR="${_VCAA_KERNEL_DIR}/download"
	local _IMAGE_BUILD_PATH="${BUILD_DIR}/vcad"
	local _VCAD_MSS_PATH="${_IMAGE_BUILD_PATH}/mss"
	local _VCAD_REPKG_PATH="${_IMAGE_BUILD_PATH}/vca_repackage"
	local _VCAD_ARTIFACT_PATH="${_IMAGE_BUILD_PATH}/artifacts"
	local _BUILD_SCRIPTS_DIR="${_IMAGE_BUILD_PATH}/build_scripts"
	local _ADDITIONAL_BIN_DIR="${_BUILD_SCRIPTS_DIR}/additional_binaries"

	_create_dir "${_IMAGE_BUILD_PATH}"
	_create_dir "${_VCAD_MSS_PATH}"
	_create_dir "${_VCAD_REPKG_PATH}"
	_create_dir "${_VCAD_ARTIFACT_PATH}"

	# copy kernel and modules deb files
	for _DEB_FILE in ${_KERNEL_DIR}/*.deb; do
		if [[ "${_DEB_FILE}" != *dbg* ]];then
			_copy "${_DEB_FILE}" "${_VCAD_REPKG_PATH}/$(basename ${_DEB_FILE})"
		fi
	done
	for _DEB_FILE in ${_MODULES_OUTPUT_DIR}/*.deb; do
		_copy "${_DEB_FILE}" "${_VCAD_REPKG_PATH}/$(basename ${_DEB_FILE})"
	done

	# copy build scripts
	_copy -r "${SCRIPT_DIR}/build_scripts" "${_IMAGE_BUILD_PATH}"
	# download additional binary
	_create_dir "${_ADDITIONAL_BIN_DIR}/i915"
        _download "${ADDITONAL_BINARY_LINK}" "${_ADDITIONAL_BIN_DIR}/i915/${ADDITONAL_BINARY_NAME}" "${ADDITONAL_BINARY_NAME}"
	echo "${ADDITONAL_BINARY_NAME}\n\tfirmware with performance fix for kvmgt\n\tfrom: https://cgit.freedesktop.org/drm/drm-firmware/tree/i915" > "${_ADDITIONAL_BIN_DIR}/content.txt"

	# modify build scripts
	grep -r "firewalld" --binary-files=without-match "${_BUILD_SCRIPTS_DIR}" | sed  's/:firewalld//g' | xargs sed -i "s/firewalld/#firewalld/g"

	_cd "${_KERNEL_DIR}/${KERNEL_SRC_NAME}"
	local _COMMIT_ID_KERNEL=$(git rev-parse --short HEAD)
	[ -z "${_COMMIT_ID_KERNEL}" ] && die "Failed to get kernel commit id"

	_cd ${_BUILD_SCRIPTS_DIR}
	local _KERNEL_NAME=${KERNEL_VER}-1.${_COMMIT_ID_KERNEL}.vca+
	local _OS_VERSION="18.04"
	local _BUILD_ID=1
	sudo -E ${_BUILD_SCRIPTS_DIR}/build_ubuntu_vcad.sh -d ${_OS_VERSION} -s ${SET_SIZE} -o /tmp/a -k ${_KERNEL_NAME} \
		-t bootstrap -p 1.0.${_BUILD_ID} || die "Failed to build vcad file"

	debug ${DEBUG_LEVEL} "-- build_vcad"
}

install_vcad() {
	debug ${INITIAL_DEBUG_LEVEL} "++ install_vcad ($@)"

	local _MOUNT_PATH="/mnt/vcad"
	local _ROOT_PKG_PATH="${_MOUNT_PATH}/root/package"
	local _IMAGE_BUILD_PATH="${BUILD_DIR}/vcad"
	local _VCAD_INSTALL_PATH="${_IMAGE_BUILD_PATH}/INSTALL"
	local _VCAD_REPKG_PATH="${_IMAGE_BUILD_PATH}/vca_repackage"
	local _VCAA_KERNEL_DIR="${BUILD_DIR}/vcaa_kernel"
	local _DOWNLOAD_DIR="${_VCAA_KERNEL_DIR}/download"

	_create_dir "${_MOUNT_PATH}"
	_cd "${_VCAD_INSTALL_PATH}"
	gzip -d -v vca_disk*.gz || die "Failed to extract vcad archive"
	notice "extracting base vcad image..."
	mount -o loop,offset=$((616448 * 512)) vca_disk*.vcad ${_MOUNT_PATH} || die "Failed to mount vcad image"
	mount --bind /dev ${_MOUNT_PATH}/dev/ || die "Failed to mount dev"
	mount --bind /proc ${_MOUNT_PATH}/proc/ || die "Failed to mount proc"
        
	# copy packages
	_create_dir "${_ROOT_PKG_PATH}"
	for _PKG_FILE in ${ROOT_DIR}/* ; do
		if [ "${_PKG_FILE}" != "${ROOT_DIR}/build" ]; then
			_copy -rf ${_PKG_FILE} "${_ROOT_PKG_PATH}/"
		fi
	done
        if [ ${SET_OPT} != "BASIC" ];then
	_copy ${_VCAD_REPKG_PATH}/linux-headers*.deb "${_ROOT_PKG_PATH}/"
	_copy ${_VCAD_REPKG_PATH}/linux-image*.deb "${_ROOT_PKG_PATH}/"

	# download python packages
	_download "${NUMPY_LINK}" "${_DOWNLOAD_DIR}/${NUMPY_NAME}" "${NUMPY_NAME}"
	_download "${OPENVN_LINK}" "${_DOWNLOAD_DIR}/${OPENCV_NAME}" "${OPENCV_NAME}"
	_copy "${_DOWNLOAD_DIR}/${NUMPY_NAME}" "${_ROOT_PKG_PATH}/${NUMPY_NAME}"
	_copy "${_DOWNLOAD_DIR}/${OPENCV_NAME}" "${_ROOT_PKG_PATH}/${OPENCV_NAME}"


	if [ ${SET_OPT} == "FULL" -o ${SET_OPT} == "EXTENDED" ];then
		#download openvino
		_download "${OPENVNO_LINK}" "${_DOWNLOAD_DIR}/${OPENVNO_NAME}" "${OPENVNO_NAME}"
		_copy "${_DOWNLOAD_DIR}/${OPENVNO_NAME}" "${_ROOT_PKG_PATH}/${OPENVNO_NAME}"
	fi
	if [ ${SET_OPT} == "FULL" -o ${SET_OPT} == "IPS" -o ${SET_OPT} == "EXTENDED" ];then
		#Download mss_ocl
		_download "${MSS_OCL_LINK}" "${_DOWNLOAD_DIR}/${MSS_OCL_NAME}" "${MSS_OCL_NAME}"
		_copy "${_DOWNLOAD_DIR}/${MSS_OCL_NAME}" "${_ROOT_PKG_PATH}/${MSS_OCL_NAME}"
	fi
	if [ ${SET_OPT} == "IPS" -o ${SET_OPT} == "EXTENDED" ];then
		if [ -d ${CACHE_DIR}/${FFMPEG_NAME} ];then
			_copy -r "${CACHE_DIR}/${FFMPEG_NAME}" "${_ROOT_PKG_PATH}"
		else
			_cd ${_ROOT_PKG_PATH}
			git clone https://github.com/FFmpeg/FFmpeg.git
		fi
	fi

	# generate install script
	_cd ${BUILD_DIR}
	cat > install_package_in_image.sh <<EOF 
#!/bin/bash
opt=${SET_OPT}
export LC_ALL=C
apt-get update && apt-get install -y libjson-c3 libboost-program-options1.65-dev libboost-thread1.65 libboost-filesystem1.65 libusb-dev cron python3-pip build-essential curl wget libssl-dev ca-certificates git libboost-all-dev gcc-multilib g++-multilib libgtk2.0-dev pkg-config libpng-dev libcairo2-dev libpango1.0-dev libglib2.0-dev libusb-1.0-0-dev i2c-tools libgstreamer-plugins-base1.0-dev libavformat-dev libavcodec-dev libswscale-dev libgstreamer1.0-dev  libusb-1.0-0-dev i2c-tools libjson-c-dev usbutils ocl-icd-libopencl*  ocl-icd-opencl-dev libsm-dev libxrender-dev libavfilter-dev tzdata

rm -rf /usr/bin/python && cd /usr/bin && ln -s python3.6 python

cd /root/package
pip3 install numpy-*.whl
pip3 install opencv_python-*.whl
install_ffmpeg()
{
  cd /root/package
  cd FFmpeg
  git checkout -b 4.2 origin/release/4.2
  apt install nasm
  ./configure --disable-lzma --enable-shared --disable-static
  make -j\`nproc\`
  make install
  apt-get install -y lockfile-progs ffmpeg
  cd ..
  rm -rf FFmpeg
}
#install mss_ocl
install_mss()
{
  cd /root/package
  tar zxf MediaServerStudioEssentials2019R1HF3_16.9_10020.tar.gz
  if [ \$? != 0 ];then
	echo "failed to tar msdk."
  fi
  cd MediaServerStudioEssentials2019R1HF3_16.9_10020
  tar -zxvf intel-linux-media*.tar.gz
  cd intel-linux-media-16.9-10020/
  echo y | bash install_media.sh
  if [ \$? != 0 ];then
	echo "failed to install mss."
  fi
}
if [ \$opt == "IPS" ];then
   cd /root/package/deb
   dpkg -i intel-vcaa*.deb
   install_ffmpeg
   install_mss
fi
#install openvino
install_openvino()
{
   rm -rf /opt/
   cd /root/package
   tar -zxf ${OPENVNO_NAME}
   cd l_openvino_toolkit_p_2020.1.023
   ./install_openvino_dependencies.sh
   accept_eula=\`cat silent.cfg |grep ACCEPT_EULA=\`
   accept_eula_name=\${accept_eula#*=}
   if [ "\$accept_eula_name" != "accept" ];then
      sed -i "s/\$accept_eula/ACCEPT_EULA=accept/" silent.cfg
      if [ \$? != 0 ];then
         echo "[Error] fail to set the value of accept_eula to accept " 
      fi
   fi
   bash install.sh --ignore-signature --cli-mode -s silent.cfg
   cd /opt/intel/openvino_2020.1.023/install_dependencies
   ./install_NEO_OCL_driver.sh
}
if [ \$opt == "EXTENDED" ];then
   install_openvino
   install_mss
   cd /root/package/deb
   dpkg -i intel-vcaa-ddwo*.deb
   install_ffmpeg
fi
if [ \$opt == "FULL" ];then
   install_openvino
   install_mss
fi
#NFD
gen_nfd_file()
{
    mkdir -p /opt/intel/openvino/k8s-nfd/
    cd /opt/intel/openvino/k8s-nfd/
    echo "node.vcaa.nfd/vcaa_myriadx_nums=12" > nfd-vca-features
    echo "node.vcaa.nfd/vcaa_nn_TOPs=8.4" >> nfd-vca-features
    echo "node.vcaa.nfd/vcaa_vpu_memory_in_MB=512" >> nfd-vca-features
    echo "node.vcaa.nfd/vcaa_system_memory_in_GB=8" >> nfd-vca-features
    echo "node.vcaa.nfd/vcaa_hw_h264_codec=true" >> nfd-vca-features
    echo "node.vcaa.nfd/vcaa_hw_h265_codec=true" >> nfd-vca-features
    echo "node.vcaa.nfd/vcaa_hw_jpeg_codec=true" >> nfd-vca-features
    echo "node.vcaa.nfd/vcaa_gpu_freq_in_MHz=1150" >> nfd-vca-features
    echo "node.vcaa.nfd/vcaa_gpu_memory_in_MB=256" >> nfd-vca-features

}
gen_nfd_file
echo "/sbin/modprobe i2c-i801" >> /root/.profile
echo "/sbin/modprobe i2c-dev" >> /root/.profile
echo "/sbin/modprobe myd_vsc" >> /root/.profile
echo "/sbin/modprobe myd_ion" >> /root/.profile
rm -rf /root/package
EOF

       fi
       if [ ${SET_OPT} == "BASIC" ];then
          _cd ${BUILD_DIR}
     cat > install_package_in_image.sh <<EOF

#!/bin/bash
apt update && apt -y install dbus
mkdir -p /opt/intel/openvino/k8s-nfd/
cd /opt/intel/openvino/k8s-nfd/
echo "node.vcaa.nfd/vcaa_myriadx_nums=12" > nfd-vca-features
echo "node.vcaa.nfd/vcaa_nn_TOPs=8.4" >> nfd-vca-features
echo "node.vcaa.nfd/vcaa_vpu_memory_in_MB=512" >> nfd-vca-features
echo "node.vcaa.nfd/vcaa_system_memory_in_GB=8" >> nfd-vca-features
echo "node.vcaa.nfd/vcaa_hw_h264_codec=true" >> nfd-vca-features
echo "node.vcaa.nfd/vcaa_hw_h265_codec=true" >> nfd-vca-features
echo "node.vcaa.nfd/vcaa_hw_jpeg_codec=true" >> nfd-vca-features
echo "node.vcaa.nfd/vcaa_gpu_freq_in_MHz=1150" >> nfd-vca-features
echo "node.vcaa.nfd/vcaa_gpu_memory_in_MB=256" >> nfd-vca-features


EOF
       fi
 
	chmod +x install_package_in_image.sh || die "Failed to chmod +x install_package_in_image.sh" 
	_copy "install_package_in_image.sh" "${_ROOT_PKG_PATH}/install_package_in_image.sh"

	chroot ${_MOUNT_PATH} /root/package/install_package_in_image.sh


	umount ${_MOUNT_PATH}/proc/ || die "Failed to umount proc"
	umount ${_MOUNT_PATH}/dev/ || die "Failed to umount dev"
	umount ${_MOUNT_PATH} || die "Failed to umount vcad image"
	rm -rf ${_MOUNT_PATH} || die "Failed to remove ${_MOUNT_PATH}"
	

	_cd "${_VCAD_INSTALL_PATH}"
	gzip -v vca_disk*.vcad || die "Failed to compress vcad archive"


	debug ${INITIAL_DEBUG_LEVEL} "-- install_vcad"
}

vcad_build() {
	debug ${INITIAL_DEBUG_LEVEL} "++ vcad_build ($@)"

	parse_parameters "$@"
	check_parameters

	requirement_check
	setup_env

	if [ "${RUN_MODE}" == "host" ]; then
		if [ ${SKIP_DOCKER} -eq 0 ]; then
			build_docker
		fi
		local _PARAM_TO_DOCKER=""
		[ ${DEBUG_LEVEL} -eq 1 ] && _PARAM_TO_DOCKER=" ${_PARAM_TO_DOCKER} -d "
		[ -n "${PARAM_HTTP_PROXY}" ] && _PARAM_TO_DOCKER=" ${_PARAM_TO_DOCKER} -x ${PARAM_HTTP_PROXY} "
		[ -n "${PARAM_HTTPS_PROXY}" ] && _PARAM_TO_DOCKER=" ${_PARAM_TO_DOCKER} -y ${PARAM_HTTPS_PROXY} "
		[ -n "${PARAM_NO_PROXY}" ] && _PARAM_TO_DOCKER=" ${_PARAM_TO_DOCKER} -z ${PARAM_NO_PROXY} "
		[ ${NO_CLEAN} -eq 1 ] && _PARAM_TO_DOCKER=" ${_PARAM_TO_DOCKER} -n "
		if [ -n "${TASKS_TO_RUN}" ]; then
			local _DOCKER_TASK="${TASKS_TO_RUN}"
			if [ ${INSTALL_VCAD} -eq 1 ]; then
				_DOCKER_TASK="${_DOCKER_TASK},install"
			fi
			_PARAM_TO_DOCKER=" ${_PARAM_TO_DOCKER} -t ${_DOCKER_TASK} "
		fi
		[ ${DOWNLOAD_USING_CACHE} -eq 1 ] && _PARAM_TO_DOCKER=" ${_PARAM_TO_DOCKER} -c "
		docker run -t -u 0:0 -v /dev:/dev --privileged -w ${BUILD_DIR} \
			-v ${PKG_ROOT_DIR}:${PKG_ROOT_DIR}:rw,z \
			${VCAA_DOCKER_NAME}:${VCAA_DOCKER_VERSION} ${ROOT_DIR}/scripts/vcad_build.sh \
			-m docker ${_PARAM_TO_DOCKER} -e ${SET_SIZE} -o ${SET_OPT} 
	elif [ "${RUN_MODE}" == "docker" ]; then
		if [ -n "${TASKS_TO_RUN}" ]; then
			local _TASKS=${TASKS_TO_RUN//,/ }
			for _TASK in ${_TASKS}; do
				if [ "${_TASK}" == "build" ]; then
				       build_kernel_and_modules
				elif [ "${_TASK}" == "package" ]; then
					build_vcad
 				elif [ "${_TASK}" == "install" ]; then
                                         install_vcad
				fi
			done
		fi
	fi

	debug ${INITIAL_DEBUG_LEVEL} "-- vcad_build"
	return 0
} 

stderr "Called as: $0 $*"
vcad_build "$@" && stderr "Finished: $0 $*"




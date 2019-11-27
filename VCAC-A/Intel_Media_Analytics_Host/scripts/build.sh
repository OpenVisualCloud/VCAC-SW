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

# Constants
readonly ROOT_DIR="$(dirname ${SCRIPT_DIR})"
readonly PKG_ROOT_DIR="$(dirname ${ROOT_DIR})"
readonly CACHE_DIR="${PKG_ROOT_DIR}/cache"
readonly TAR_DIR="${ROOT_DIR}/tar"
readonly DEB_DIR="${ROOT_DIR}/deb"
readonly DEFAULT_BUILD_DIR="${ROOT_DIR}/build"
readonly DEFAULT_RUN_MODE="host"

readonly KERNEL_PATCH_ARCHIVE="${TAR_DIR}/centos7.4-kernel3.10.0-patch.tar.gz"
readonly MODULES_PATCH_ARCHIVE="${TAR_DIR}/vcass-modules-3.10.0-patch.tar.gz"
readonly DAEMON_PATCH_ARCHIVE="${TAR_DIR}/vca-apps-3.10.0-patch.tar.gz"

readonly VCAA_DOCKER_NAME="vcaa/centos-7.4-test"
readonly VCAA_DOCKER_VERSION="1.0"

readonly BOOST_VER="1.65.1"
readonly BOOST_NAME="boost_1_65_1"
readonly BOOST_LINK="https://dl.bintray.com/boostorg/release/${BOOST_VER}/source/${BOOST_NAME}.tar.gz"


readonly KERNEL_VERSION="3.10.0"
readonly KERNEL_SRC_NAME="linux-${KERNEL_VERSION}-693.17.1.el7"
readonly KERNEL_SRC_NAME_FULL="kernel-${KERNEL_VERSION}-693.17.1.el7.src"
readonly KERNEL_SRC_ARCHIVE="${KERNEL_SRC_NAME_FULL}.rpm"
readonly KERNEL_SRC_LINK="http://vault.centos.org/7.4.1708/updates/Source/SPackages/${KERNEL_SRC_ARCHIVE}"

readonly VCA_SRC_ARCHIVE="VCAC-A_R2.tar.gz"
readonly VCA_SRC_LINK="https://github.com/OpenVisualCloud/VCAC-SW/archive/VCAC-A_R2.tar.gz"
readonly MODULES_SRC_NAME="vca_modules_2.3.26_src"
readonly MODULES_SRC_ARCHIVE="${MODULES_SRC_NAME}.tar.gz"
readonly DAEMON_SRC_NAME="vca_apps_2.3.26_src"
readonly DAEMON_SRC_ARCHIVE="${DAEMON_SRC_NAME}.tar.gz"

readonly INITIAL_DEBUG_LEVEL=1

readonly DEFAULT_TASKS_TO_RUN="build,build_daemon"

# Globals
BUILD_DIR=${DEFAULT_BUILD_DIR}
RUN_MODE="${DEFAULT_RUN_MODE}"
DOWNLOAD_USING_CACHE=0
PARAM_HTTP_PROXY=
PARAM_HTTPS_PROXY=
PARAM_NO_PROXY=
SKIP_DOCKER=0
NO_CLEAN=0
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
-m, --mode <mode>	host or docker, host mode manages docker, docker mode runs the actual build. Default mode is host, which will run docker mode in docker container automatically.
-n, --no-clean	Don't do clean before build.
-s, --skip-docker	Skip creating docker image if you're sure it's been created correctly.
-t, --tasks <tasks>	tasks to be run in docker, separated with comma, default: \"build,package\".
-x, --http-proxy <http_proxy>	Set the http_proxy environment variable.
-y, --https-proxy <https_proxy>	Set the https_proxy environment variable.
-z, --no-proxy <no_proxy>	Set the no_proxy environment variable.
-h, --help	Show this help screen.
"
}

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
	local _EXIT_CODE=$(( $? == 0 ? 99 : $? ))
	stderr "ERROR: $*"
	exit ${_EXIT_CODE}
}
readonly _CONST_DEBUG_LEVEL=1	# 0 means no debug; positive numbers increase verbosity
debug(){
	local _LEVEL=1
	[ $# -gt 1 ] && { _LEVEL="$1" ; shift ;}
	[ "${_LEVEL}" -le "${_CONST_DEBUG_LEVEL}" ] && stderr "DEBUG[${_LEVEL}]: $*"
	return 0
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
			-h|--help)
				show_help
				exit 0;;
			*)
				show_help && die "Unknown parameter '$1'"
		esac
	done

	debug ${INITIAL_DEBUG_LEVEL} "-- parse_parameters"
}

check_parameters() {
	debug ${DEBUG_LEVEL} "++ check_parameters ($@)"

	[ -z "${BUILD_DIR}" ] && show_help && die "No build directory given"

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

_extract_rpm() {
	debug ${DEBUG_LEVEL} "++ _extract_rpm ($@)"

	local _SRC_ARCHIVE="$1"
	local _DEST_DIR="$2"

	_cd "${_DEST_DIR}"
	rpm2cpio "${_SRC_ARCHIVE}" | cpio -idmv || die "Failed to extract rpm ${_SRC_ARCHIVE} to ${_DEST_DIR}"
	_cd -

	debug ${DEBUG_LEVEL} "-- _extract_rpm"
}

_extract_xz() {
	debug ${DEBUG_LEVEL} "++ _extract_xz ($@)"

	local _SRC_ARCHIVE="$1"
	local _DEST_DIR="$2"

	tar xJf "${_SRC_ARCHIVE}" -C "${_DEST_DIR}" || die "Failed to extract ${_SRC_ARCHIVE} to ${_DEST_DIR}"

	debug ${DEBUG_LEVEL} "-- _extract_xz"
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

build_kernel_and_modules() {
	debug ${DEBUG_LEVEL} "++ build_kernel_and_modules ($@)"

	# create the directories
	local _HOST_PKG_DIR="${BUILD_DIR}/host_packages"
	[[ ! -d "${_HOST_PKG_DIR}" || ${NO_CLEAN} -eq 0 ]] && _create_dir "${_HOST_PKG_DIR}"
	local _VCAA_KERNEL_DIR="${BUILD_DIR}/vcaa_kernel"
	[[ ! -d "${_VCAA_KERNEL_DIR}" || ${NO_CLEAN} -eq 0 ]] && _create_dir "${_VCAA_KERNEL_DIR}"
	local _KERNEL_DIR="${_VCAA_KERNEL_DIR}/kernel"
	[[ ! -d "${_KERNEL_DIR}" || ${NO_CLEAN} -eq 0 ]] && _create_dir "${_KERNEL_DIR}"
	local _MODULES_DIR="${_VCAA_KERNEL_DIR}/modules"
	[[ ! -d "${_MODULES_DIR}" || ${NO_CLEAN} -eq 0 ]] && _create_dir "${_MODULES_DIR}"
	local _DOWNLOAD_DIR="${_VCAA_KERNEL_DIR}/download"
	[[ ! -d "${_DOWNLOAD_DIR}" || ${NO_CLEAN} -eq 0 ]] && _create_dir "${_DOWNLOAD_DIR}"
	local _DOWNLOAD_DIR_KERNEL="${_DOWNLOAD_DIR}/kernel"
	[[ ! -d "${_DOWNLOAD_DIR_KERNEL}" || ${NO_CLEAN} -eq 0 ]] && _create_dir "${_DOWNLOAD_DIR_KERNEL}"
	local _DOWNLOAD_DIR_VCA="${_DOWNLOAD_DIR}/vca"
	[[ ! -d "${_DOWNLOAD_DIR_VCA}" || ${NO_CLEAN} -eq 0 ]] && _create_dir "${_DOWNLOAD_DIR_VCA}"
	local _PATCH_DIR="${_VCAA_KERNEL_DIR}/patch"
	[[ ! -d "${_PATCH_DIR}" || ${NO_CLEAN} -eq 0 ]] && _create_dir "${_PATCH_DIR}"
	local _KERNEL_PATCH_DIR="${_PATCH_DIR}/kernel"
	[[ ! -d "${_KERNEL_PATCH_DIR}" || ${NO_CLEAN} -eq 0 ]] && _create_dir "${_KERNEL_PATCH_DIR}"
	local _MODULES_PATCH_DIR="${_PATCH_DIR}/modules"
	[[ ! -d "${_MODULES_PATCH_DIR}" || ${NO_CLEAN} -eq 0 ]] && _create_dir "${_MODULES_PATCH_DIR}"

	# download and extract kernel source
	[[ ! -f "${_DOWNLOAD_DIR}/${KERNEL_SRC_ARCHIVE}" || ${NO_CLEAN} -eq 0 ]] && _download "${KERNEL_SRC_LINK}" "${_DOWNLOAD_DIR}/${KERNEL_SRC_ARCHIVE}" "${KERNEL_SRC_ARCHIVE}"
	[ ${NO_CLEAN} -eq 0 ] && _extract_rpm "${_DOWNLOAD_DIR}/${KERNEL_SRC_ARCHIVE}" "${_DOWNLOAD_DIR_KERNEL}"
	[ ${NO_CLEAN} -eq 0 ] && _extract_xz "${_DOWNLOAD_DIR_KERNEL}/${KERNEL_SRC_NAME}.tar.xz" "${_KERNEL_DIR}"

	# apply kernel patch
	[ ${NO_CLEAN} -eq 0 ] && _extract_tgz "${KERNEL_PATCH_ARCHIVE}" "${_KERNEL_PATCH_DIR}"
	[ ${NO_CLEAN} -eq 0 ] && _apply_patch_git "${_KERNEL_DIR}/${KERNEL_SRC_NAME}" "${_KERNEL_PATCH_DIR}" "${KERNEL_SRC_NAME}"

	# download and extract modules source
	[[ ! -f "${_DOWNLOAD_DIR}/${VCA_SRC_ARCHIVE}" || ${NO_CLEAN} -eq 0 ]] && _download "${VCA_SRC_LINK}" "${_DOWNLOAD_DIR}/${VCA_SRC_ARCHIVE}" "${VCA_SRC_ARCHIVE}"
	#[ ${NO_CLEAN} -eq 0 ] && _extract_zip "${_DOWNLOAD_DIR}/${VCA_SRC_ARCHIVE}" "${_DOWNLOAD_DIR_VCA}"
	[ ${NO_CLEAN} -eq 0 ] && _extract_tgz "${_DOWNLOAD_DIR}/${VCA_SRC_ARCHIVE}" "${_DOWNLOAD_DIR_VCA}"
	[ ${NO_CLEAN} -eq 0 ] && cp -r "${_DOWNLOAD_DIR_VCA}/VCAC-SW-VCAC-A_R2/modules" "${_MODULES_DIR}/../"

	# apply modules patch
	[ ${NO_CLEAN} -eq 0 ] && _extract_tgz "${MODULES_PATCH_ARCHIVE}" "${_MODULES_PATCH_DIR}"
	[ ${NO_CLEAN} -eq 0 ] && _apply_patch_git "${_MODULES_DIR}" "${_MODULES_PATCH_DIR}" "${MODULES_SRC_NAME}"

	# build kernel
	rm -rf ${_HOST_PKG_DIR}/kernel*.rpm || die "Failed to remove previous kernel rpms"

	_cd "${_KERNEL_DIR}/${KERNEL_SRC_NAME}"
	local _COMMIT_ID_KERNEL=$(git rev-parse --short HEAD)
	[ -z "${_COMMIT_ID_KERNEL}" ] && die "Failed to get kernel commit id"
	make -j`nproc` rpm RPMVERSION=${_COMMIT_ID_KERNEL} || die "Failed to build kernel"

	_copy /root/rpmbuild/SRPMS/kernel*.rpm ${_HOST_PKG_DIR}
	_copy /root/rpmbuild/RPMS/x86_64/kernel*.rpm ${_HOST_PKG_DIR}
	rpm -ivh /root/rpmbuild/RPMS/x86_64/kernel-devel-*.rpm || die "Failed to install kernel devel rpm"

	# build modules
	rm -rf ${_HOST_PKG_DIR}/vcass-modules*.rpm || die "Failed to remove previous modules rpms"

	_cd ${_MODULES_DIR}
	local _COMMIT_ID_MODULES=$(git rev-parse --short HEAD)
	[ -z "${_COMMIT_ID_MODULES}" ] && die "Failed to get modules commit id"
    make -f make_rpm.mk KERNEL_SRC=/usr/src/kernels/${KERNEL_VERSION}-1.${_COMMIT_ID_KERNEL}.VCA+/ \
		KERNEL_VERSION=${KERNEL_VERSION}-1.${_COMMIT_ID_KERNEL}.VCA+ \
		KERNELRELEASE=${KERNEL_VERSION}-1.${_COMMIT_ID_KERNEL}.VCA+ RPMVERSION=1.${_COMMIT_ID_MODULES} \
		|| die "Failed to build modules"

	_copy /root/rpmbuild/SRPMS/vcass-modules*.rpm ${_HOST_PKG_DIR}
	_copy /root/rpmbuild/RPMS/x86_64/vcass-modules*.rpm ${_HOST_PKG_DIR}

	debug ${DEBUG_LEVEL} "-- build_kernel_and_modules"
}

build_vcaa_daemon() {
	debug ${DEBUG_LEVEL} "++ build_vcaa_daemon ($@)"

	local _HOST_PKG_DIR="${BUILD_DIR}/host_packages"
	local _VCAA_KERNEL_DIR="${BUILD_DIR}/vcaa_kernel"
	local _MODULES_DIR="${_VCAA_KERNEL_DIR}/modules"
	local _DOWNLOAD_DIR="${_VCAA_KERNEL_DIR}/download"
	local _DOWNLOAD_DIR_VCA="${_DOWNLOAD_DIR}/vca"
	local _DAEMON_DIR="${_VCAA_KERNEL_DIR}/daemon"
	
	[[ ! -d "${_DAEMON_DIR}" || ${NO_CLEAN} -eq 0 ]] && _create_dir "${_DAEMON_DIR}"
	local _PATCH_DIR="${_VCAA_KERNEL_DIR}/patch"
	local _DAEMON_PATCH_DIR="${_PATCH_DIR}/daemon"
	[[ ! -d "${_DAEMON_PATCH_DIR}" || ${NO_CLEAN} -eq 0 ]] && _create_dir "${_DAEMON_PATCH_DIR}"
	
	
	
	
	
	
	#[[ ! -d "${_DAEMON_DIR}" || ${NO_CLEAN} -eq 0 ]] && _create_dir "${_DAEMON_DIR}"
	#[[ ! -d "${_HOST_PKG_DIR}" || ${NO_CLEAN} -eq 0 ]] && _create_dir "${_HOST_PKG_DIR}"
	#[[ ! -d "${_VCAA_KERNEL_DIR}" || ${NO_CLEAN} -eq 0 ]] && _create_dir "${_VCAA_KERNEL_DIR}"
	#[[ ! -d "${_DOWNLOAD_DIR}" || ${NO_CLEAN} -eq 0 ]] && _create_dir "${_DOWNLOAD_DIR}"
	#[[ ! -d "${_DOWNLOAD_DIR_VCA}" || ${NO_CLEAN} -eq 0 ]] && _create_dir "${_DOWNLOAD_DIR_VCA}"
	#[[ ! -d "${_MODULES_DIR}" || ${NO_CLEAN} -eq 0 ]] && _create_dir "${_MODULES_DIR}"
	#local _PATCH_DIR="${_VCAA_KERNEL_DIR}/patch"
	#local _DAEMON_PATCH_DIR="${_PATCH_DIR}/daemon"
	#[[ ! -d "${_DAEMON_PATCH_DIR}" || ${NO_CLEAN} -eq 0 ]] && _create_dir "${_DAEMON_PATCH_DIR}"

	[[ ! -f "${_DOWNLOAD_DIR}/${BOOST_NAME}.tar.gz" || ${NO_CLEAN} -eq 0 ]] && _download "${BOOST_LINK}" "${_DOWNLOAD_DIR}/${BOOST_NAME}.tar.gz" "${BOOST_NAME}.tar.gz"
	# _extract_tgz "${_DOWNLOAD_DIR}/${BOOST_NAME}.tar.gz" "${_DAEMON_DIR}"
	#_cd ${_DOWNLOAD_DIR}
        #cd ${_DOWNLOAD_DIR}
	_extract_tgz "${_DOWNLOAD_DIR}/${BOOST_NAME}.tar.gz" "${_DAEMON_DIR}"
	_cd ${_DAEMON_DIR}/${BOOST_NAME}
	./bootstrap.sh
	./b2 -j "$(nproc)" stage variant=release link=static cxxflags="-fPIC" address-model=64 threading=multi
	./b2 --prefix="${_DAEMON_DIR}/${BOOST_NAME}" -j "$(nproc)" cxxflags=-fPIC cflags=-fPIC --with-thread --with-system --with-filesystem --with-program_options --with-regex --with-iostreams --with-log --with-chrono --with-test toolset=gcc install

	# download and extract modules source
	[[ ! -f "${_DOWNLOAD_DIR}/${VCA_SRC_ARCHIVE}" || ${NO_CLEAN} -eq 0 ]] && _download "${VCA_SRC_LINK}" "${_DOWNLOAD_DIR}/${VCA_SRC_ARCHIVE}" "${VCA_SRC_ARCHIVE}"
	 
       # cd ${_DOWNLOAD_DIR}
       # tar -zxvf ${VCA_SRC_ARCHIVE}
	_extract_tgz "${_DOWNLOAD_DIR}/${VCA_SRC_ARCHIVE}" "${_DOWNLOAD_DIR}"
	#[ ${NO_CLEAN} -eq 0 ] && _extract_tgz "${_DOWNLOAD_DIR}/${VCA_SRC_ARCHIVE}" "${_DAEMON_DIR}"
	_cd ${_DOWNLOAD_DIR}/VCAC-SW-VCAC-A_R2/apps
	BOOST_ROOT=${_DAEMON_DIR}/${BOOST_NAME} OS=CENTOS WORKSPACE=/tmp/tmp-test PKG_VER=2.7.3 MODULES_SRC=../modules/ ${_DOWNLOAD_DIR}/VCAC-SW-VCAC-A_R2/apps/generate_apps.sh
	_copy /tmp/tmp-test/daemon-vca*.rpm ${_HOST_PKG_DIR}

	#_copy /root/rpmbuild/SRPMS/daemon-vca*.rpm ${_HOST_PKG_DIR}
	#_copy /root/rpmbuild/RPMS/x86_64/daemon-vca*.rpm ${_HOST_PKG_DIR}

	debug ${DEBUG_LEVEL} "-- build_vcaa_daemon"
}

build() {
	debug ${INITIAL_DEBUG_LEVEL} "++ build ($@)"

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
		[ -n "${TASKS_TO_RUN}" ] && _PARAM_TO_DOCKER=" ${_PARAM_TO_DOCKER} -t ${TASKS_TO_RUN} "
		[ ${DOWNLOAD_USING_CACHE} -eq 1 ] && _PARAM_TO_DOCKER=" ${_PARAM_TO_DOCKER} -c "
		docker run -t -u 0:0 -v /dev:/dev --privileged -w ${BUILD_DIR} \
			-v ${PKG_ROOT_DIR}:${PKG_ROOT_DIR}:rw,z \
			${VCAA_DOCKER_NAME}:${VCAA_DOCKER_VERSION} ${ROOT_DIR}/scripts/build.sh \
			-m docker ${_PARAM_TO_DOCKER}
	elif [ "${RUN_MODE}" == "docker" ]; then
		if [ -n "${TASKS_TO_RUN}" ]; then
			local _TASKS=${TASKS_TO_RUN//,/ }
			for _TASK in ${_TASKS}; do
				if [ "${_TASK}" == "build" ]; then
					build_kernel_and_modules
				elif [ "${_TASK}" == "build_daemon" ]; then
					build_vcaa_daemon
				fi
			done
		fi
	fi

	debug ${INITIAL_DEBUG_LEVEL} "-- build"
	return 0
}

stderr "Called as: $0 $*"
build "$@" && stderr "Finished: $0 $*"


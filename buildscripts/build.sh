#!/bin/bash
set -Eeuo pipefail

# shellcheck source=library_fs.sh
source "$(cd "$(dirname "$(readlink -f "$0")")"; pwd)/"library_fs.sh

# This is helper script for master_build.sh, intended to be run via docker, and will build requested configuration.
# OS family/version selection is out of this script scope (running machine OS will be used).
# It could be run also without docker, for build-time dependencies look in *.dockerfile file of intended OS.

readonly BOOST_VER=boost_1_65_1

# params to set via command line switches
BOOST_ROOT= # optional
COMPONENT=
DOWNLOAD_DIR=
KER_VER=
OUT_DIR=
PKG_VER=2.7.363 # when building from release package, will be overridden automatically
VCA_IN_DIR=

# params set by script itself, for other parts of script
APPS_DIR=
BUILDSCRIPTS_DIR=
DISTRO=
EXTRACT_DIR=
FEATURE=STD
KERNEL_DIR=
MODULES_DIR=
PATCHES_DIR=

usage(){
echo -n 'Mandatory arguments:
--component <host|image>
	build binaries necessary for host or image for card side
--downloads-dir <downloaded-dependencies-dir>
	path to downloaded 3rd party binaries
--kernel-ver <kernel-version>
	kernel version, as specified in master_build.sh, examples: 3.10.0-693, 4.19.0, 5.1.16
--out-dir <output-dir>
	path to place resulting binaries (will be automatically created)
--vca-src-dir <vca-sw-input-dir>
	path to extracted VCASS release package or to VCASS sources cloned from git repository

Optional arguments:
--boost-root <installed-boost-location>
	boost location to use instead of building temporary one
--pkg-ver <pkg-ver>
	only when building from source (eg: git repository), package version to use
';}

parse_parameters(){
	while [ $# -gt 1 ]; do
		case "${1}" in
		--boost-root)	BOOST_ROOT="$(realpath "${2}")";;
		--component)	COMPONENT="${2}";;
		--downloads-dir)DOWNLOAD_DIR="$(realpath "${2}")";;
		--kernel-ver)	KER_VER="${2}";;
		--out-dir)	OUT_DIR="$(realpath "${2}")";;
		--pkg-ver)	PKG_VER="${2}";;
		--vca-src-dir)	VCA_IN_DIR="$(realpath "${2}")";;
		*) die "unrecognized parameter: '$1', run $0 without arguments for usage information";;
		esac
		shift 2
	done
	if [ -z "${VCA_IN_DIR}" ] || [ -z "${DOWNLOAD_DIR}" ] || [ -z "${OUT_DIR}" ] \
			|| [ -z "${KER_VER}" ] || [ -z "${COMPONENT}" ]; then
		usage
		exit 1
	fi
	die_if_empty_input_dir "${VCA_IN_DIR}"
	die_if_empty_input_dir "${DOWNLOAD_DIR}"
	grep -P '^[345]\.[\d]{1,2}\.[\d]{1,3}(-[\d]{3})?$' <<< "${KER_VER}" \
		|| die "Unsupported kernel version (--kernel-ver): ${KER_VER}"
}

die(){
	EXIT_CODE=$?
	EXIT_CODE=$((EXIT_CODE == 0 ? 99 : EXIT_CODE))
	echo -e "**** FATAL ${FUNCNAME[1]-}(): $*" >&2
	cleanup
	exit ${EXIT_CODE}
}
trap 'die "(cmd: $BASH_COMMAND)"' ERR
cleanup(){ rm -rf "${EXTRACT_DIR}"; }

die_if_empty_input_dir(){
	local _DIR; _DIR="$1"
	find_any "${_DIR}/" -type f >/dev/null 2>/dev/null \
		|| die "${_DIR} does not contain any files, but was provided as input directory"
}

extract_release_package_internal_archives(){
	local _BUILDSCRIPTS_ARCHIVE _APPS_ARCHIVE _MODULES_ARCHIVE
	_APPS_ARCHIVE="$(	 find_single "${VCA_IN_DIR}" -type f -name 'vca_apps*')"
	_BUILDSCRIPTS_ARCHIVE="$(find_single "${VCA_IN_DIR}" -type f -name 'build_scripts*')"
	_MODULES_ARCHIVE="$(	 find_single "${VCA_IN_DIR}" -type f -name 'vcass-modules*')"

	APPS_DIR="${EXTRACT_DIR}"/apps
	BUILDSCRIPTS_DIR="${EXTRACT_DIR}"/buildscripts
	MODULES_DIR="${EXTRACT_DIR}"/modules
	PATCHES_DIR="${VCA_IN_DIR}"

	mkdir -p "${APPS_DIR}" "${BUILDSCRIPTS_DIR}" "${MODULES_DIR}"
	# get PKG_VER from string like: path"${BUILDSCRIPTS_DIR}"_2.6.265_src.tar.gz
	PKG_VER="$(grep -Po '\d+.\d+.\d+' <<< "$(basename "${_APPS_ARCHIVE}")")"
	# extract to PKG_VER-independent locations; ignore top-level component in build_scripts
	tar --strip-components=1 -f "${_BUILDSCRIPTS_ARCHIVE}" -C "${BUILDSCRIPTS_DIR}" -x
	tar -f "${_APPS_ARCHIVE}" -C "${APPS_DIR}" -x
	tar -f "${_MODULES_ARCHIVE}" -C "${MODULES_DIR}" -x
}

# set paths, needed when building from sources (git repo)
set_component_dirs(){
	APPS_DIR="$(realpath "${VCA_IN_DIR}"/apps)"
	BUILDSCRIPTS_DIR="$(realpath "${VCA_IN_DIR}"/buildscripts)"
	MODULES_DIR="$(realpath "${VCA_IN_DIR}"/modules)"
	PATCHES_DIR="$(realpath "${VCA_IN_DIR}"/patches)"
	die_if_empty_input_dir "${APPS_DIR}"
	die_if_empty_input_dir "${BUILDSCRIPTS_DIR}"
	die_if_empty_input_dir "${MODULES_DIR}"
}

build_kernel(){
(
	# todo (when needed): add support for ubuntu and centos kernels with the same KER_VER
	VCA_PATCH_DIR="$(find_single "${PATCHES_DIR}" -type d -name "*kernel-${KER_VER}")"
	if [ "${DISTRO}" = centos ]; then
		VCA_DOT_CONFIG_FILE="$(find_single "${VCA_PATCH_DIR}" -name 'kernel.config' -type f)"
	fi
	if [[ ! "${KER_VER}" =~ ^3 ]]; then
		# version 4.19.0 is released as linux-4.19, so remove potential .0 suffix from KER_VER
		tar --strip-components=1 -f "${DOWNLOAD_DIR}/linux-${KER_VER%.0}.tar.xz" -C "${KERNEL_DIR}" -x
	fi
	cd "${KERNEL_DIR}"
	if [ "${KER_VER}" = 4.15.0 ]; then
		# apply patch, like: linux_4.15.0-42.45.diff.gz
		UBUNTU_PATCH="$(find_single "${DOWNLOAD_DIR}" -name "linux_${KER_VER}*.diff.gz" -type f)"
		gunzip -c "${UBUNTU_PATCH}" | patch -p1
	fi
	export SRPM_DIR="${DOWNLOAD_DIR}" VCA_PATCH_DIR VCA_DOT_CONFIG_FILE
	bash "${BUILDSCRIPTS_DIR}"/quickbuild/generate_kernel.sh || exit # "bash" to omit "-x" from script's shebang
	publish_files mv ../output "${OUT_DIR}" \
		"kernel-${KER_VER}*VCA*rpm" "linux-image-${KER_VER}*vca*deb" \
		"kernel-devel-${KER_VER}*.rpm" "linux-headers-${KER_VER}*.deb" # publish artifacts as soon as possible
)}

build_modules(){
(
	cd "${MODULES_DIR}"
	LINUX_HEADERS_OR_KERNEL_DEVEL_PKG="$(find_single "${OUT_DIR}" -name "kernel-devel-${KER_VER}*.rpm" -o -name "linux-headers-${KER_VER}*.deb")" \
		bash ./generate_modules.sh || exit
	publish_files mv ../output "${OUT_DIR}" \
		"vcass-modules-${KER_VER}*VCA*rpm" "vcass-modules-${KER_VER}*vca*deb"
)}

build_boost_lib(){
(
	echo 'building boost library (build-time dependency of apps)...' >&2
	tar --strip-components=1 -f "${DOWNLOAD_DIR}/${BOOST_VER}.tar.gz" -C "${BOOST_ROOT}" -x
	cd "${BOOST_ROOT}"
	./bootstrap.sh
	./b2 -j "$(nproc)" stage variant=release link=static cxxflags="-fPIC" address-model=64 threading=multi
	./b2 --prefix="$(pwd)" -j "$(nproc)" cxxflags=-fPIC cflags=-fPIC --with-thread --with-system --with-filesystem --with-program_options --with-regex --with-iostreams --with-log --with-chrono --with-test toolset=gcc install
)}

build_apps(){
(
	find_any "${BOOST_ROOT}/" -type f >/dev/null 2>/dev/null \
		|| build_boost_lib
	cd "${APPS_DIR}"
	export WORKSPACE="$(mktemp -d apps.workspace.XXXX)"
	MODULES_SRC="${MODULES_DIR}" BOOST_ROOT="${BOOST_ROOT}" \
		bash ./generate_apps.sh || exit
	publish_files mv "${WORKSPACE}" "${OUT_DIR}" 'daemon-vca*rpm' 'daemon-vca*deb'
	rm -rf "${WORKSPACE}"
)}

build_image(){
(
	local _ARTIFACTS_DIR _BASE_IMG_VER _PACKAGES_LOCATION
	_ARTIFACTS_DIR="${EXTRACT_DIR}/artifacts"
	mkdir -p "${_ARTIFACTS_DIR}"
	case "${DISTRO}" in
	ubuntu)
		source /etc/os-release
		_BASE_IMG_VER="$VERSION_ID"
		if [ -s "${OUT_DIR}/deb.tar" ] || [ -s "${OUT_DIR}/base.tar" ]; then
			echo -e "\nArchiving intermediate Software Archives (SAs) from previous run\n"
			# keep only last backup from each day (to save space - ~181MB from one day)
			local _OLD_DIR
			_OLD_DIR="${OUT_DIR}/snapshots-of-software-archives-$(date +%F)"
			mkdir -p "${_OLD_DIR}"
			publish_files mv "${OUT_DIR}" "${_OLD_DIR}" deb.tar base.tar
			rm -rf "${OUT_DIR}"/vca-on-the-fly.tar
		fi
		"${BUILDSCRIPTS_DIR}"/create_sw_archives.sh \
			--base-and-bootstrap-only \
			-d "UBUNTU ${_BASE_IMG_VER}" \
			-k "${KER_VER}-1.${PKG_VER}.vca" \
			-o "${OUT_DIR}"
		_PACKAGES_LOCATION="${_ARTIFACTS_DIR}"
		echo -e "\n\nIntermediate Software Archives (deb.tar and base.tar) were saved in ${OUT_DIR}, next build will recreate them. If for any reason do you want to reproduce exactly this build, keep them for reuse (manual execution of generate_images.sh would be necessary)\n" >&2
	;;
	centos)
		local _OS_REPO_DIR _EXTRAS_REPO_DIR
		_BASE_IMG_VER="$(grep -Eo '[0-9]\.[0-9]+' /etc/centos-release)"
		_OS_REPO_DIR=/usr/lib/vca/repos/CentOS7.6/os_repo/
		_EXTRAS_REPO_DIR=/usr/lib/vca/repos/CentOS7.6/extras_repo/
		mkdir -p "${_ARTIFACTS_DIR}/rpmbuild/RPMS/x86_64/"
		mkdir -p "${_OS_REPO_DIR}"
		sudo mount "${DOWNLOAD_DIR}/CentOS-7-x86_64-Everything-1810.iso" "${_OS_REPO_DIR}"
		if [ "${FEATURE}" = "STD" ]; then
			mkdir -p "${_EXTRAS_REPO_DIR}"
			cp "${DOWNLOAD_DIR}"/edk2.git-* "${_EXTRAS_REPO_DIR}"
			createrepo "${_EXTRAS_REPO_DIR}"
		fi
		_PACKAGES_LOCATION="${_ARTIFACTS_DIR}/rpmbuild/RPMS/x86_64/"
		cd "${BUILDSCRIPTS_DIR}"
	;;
	*)
		die "${DISTRO} not supported"
	;;
	esac
	# copy selected files as workaround for no option to select subset of deb's/rpm's in artifacts directory
	# copy SAs to the same artifacts directory, as workaround for no option to specify other location of SAs
	publish_files cp "${OUT_DIR}" "${_PACKAGES_LOCATION}" \
		base.tar deb.tar \
		"linux-image-${KER_VER}*vca*deb" "kernel-${KER_VER}*VCA*rpm" \
		"linux-headers-${KER_VER}*vca*deb" "kernel-devel-${KER_VER}*VCA*rpm" \
		"vcass-modules-${KER_VER}*vca*deb" "vcass-modules-${KER_VER}*VCA*rpm"
	ARTIFACTS_DIR="${_ARTIFACTS_DIR}" DIST="${_BASE_IMG_VER}" OUT="${OUT_DIR}" \
		SKIP_INITIAL_CLEANUP=yes OS="${DISTRO^^}" FEATURE="${FEATURE}" \
		"${BUILDSCRIPTS_DIR}"/quickbuild/generate_images.sh
)}

# copy or move files from $2 to $3
# arguments: $1 - 'cp' or 'mv', $2 - src directory for recursive find,
# $3 - dst dir, remaining arguments will be passed as '-o -name $arg' to find
publish_files(){
	local _CMD _SRC _DST _SEARCH_PARAMS
	_CMD="$1"
	_SRC="$2"
	_DST="$3"
	shift 3
	while [ $# -gt 0 ]; do
		_SEARCH_PARAMS+=(-o -name "$1")
		shift
	done
	# -false adds left condition to first '-o'
	find "${_SRC}" -type f \( -false "${_SEARCH_PARAMS[@]}" \) -exec "${_CMD}" {} "${_DST}" \;
}

# run $1 almost silently (2 stdout lines + whole stderr still will be reported)
silent(){
	local _FUN _OUT
	_FUN="$1"
	shift
	echo "** calling ${_FUN} $*"
	# keep last stdout line, to make it clear that build was ok even with warnings present
	_OUT="$(${_FUN} "$@")" || return
	echo "[last line of ${_FUN}()]:" "$(tail -1 <<< "${_OUT}")"
}

# detect if script was invoked from extracted release package
# (other supported working mode is from source (like git repo))
is_release_pkg_in_use(){
	find_single "${VCA_IN_DIR}" -type f -name 'vca_apps*' &>/dev/null
}

main(){
	local _EXTRACT_RELEASE_PACKAGE_ERRORS
	EXTRACT_DIR="${EXTRACT_DIR:-$(mktemp -d)/extract-dir}" # more than one level, to workaround "../output" (hardcoded) default in some scripts
	BOOST_ROOT="${BOOST_ROOT:-${EXTRACT_DIR}/${BOOST_VER}}"
	KERNEL_DIR="${EXTRACT_DIR}"/kernel
	mkdir -p "${BOOST_ROOT}" "${KERNEL_DIR}"

	if is_release_pkg_in_use; then
		extract_release_package_internal_archives
	else
		echo 'It looks that you are building from repository' >&2
		set_component_dirs || \
			die "Invalid repository structure. Aborting, because attempt to build from release package yield following errors:\n${_EXTRACT_RELEASE_PACKAGE_ERRORS}"
	fi

	DISTRO="$(grep -o -e ubuntu -m1 /etc/os-release || echo centos)"
	OUT_DIR="${OUT_DIR}/${DISTRO}_with_kernel_${KER_VER}"
	mkdir -p "${OUT_DIR}"
	export PKG_VER KER_VER

	silent build_kernel || exit
	silent build_modules || exit
	[[ "${COMPONENT}" =~ host  ]] && { silent build_apps || exit; }
	[[ "${COMPONENT}" =~ image ]] && { silent build_image || exit; }
	return 0
}

echo "*** Called as: $0 $*" >&2
parse_parameters "$@"
main && echo "*** Finished: $0 $*" >&2

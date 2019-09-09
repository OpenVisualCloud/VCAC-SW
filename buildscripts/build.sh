#!/bin/bash
set -Eeuo pipefail

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
PKG_VER=2.7.0 # when building from release package, will be overridden automatically
VCA_IN_DIR=

# params set by script itself, for other parts of script
APPS_DIR=
BUILDSCRIPTS_DIR=
DISTRO=
EXTRACT_DIR=
KERNEL_DIR=
MODULES_DIR=
SCRIPT_DIR=

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
	ensure_that_there_is_at_least_one_file_within_dir "${VCA_IN_DIR}"
	ensure_that_there_is_at_least_one_file_within_dir "${DOWNLOAD_DIR}"
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

extract_release_package(){
	local _BUILDSCRIPTS_ARCHIVE _APPS_ARCHIVE _MODULES_ARCHIVE
	_APPS_ARCHIVE="$(	 find_one "${VCA_IN_DIR}" -type f -name 'vca_apps*')"
	_BUILDSCRIPTS_ARCHIVE="$(find_one "${VCA_IN_DIR}" -type f -name 'build_scripts*')"
	_MODULES_ARCHIVE="$(	 find_one "${VCA_IN_DIR}" -type f -name 'vca_modules*')"

	APPS_DIR="${EXTRACT_DIR}"/apps
	BUILDSCRIPTS_DIR="${EXTRACT_DIR}"/buildscripts
	MODULES_DIR="${EXTRACT_DIR}"/modules

	mkdir -p "${APPS_DIR}" "${BUILDSCRIPTS_DIR}" "${MODULES_DIR}"
	# get PKG_VER from string like: path"${BUILDSCRIPTS_DIR}"_2.6.265_src.tar.gz
	PKG_VER="$(grep -Po '\d+.\d+.\d+' <<< "$(basename "${_BUILDSCRIPTS_ARCHIVE}")")"
	# extract to PKG_VER-independent locations; ignore top-level component in build_scripts
	tar --strip-components=1 -f "${_BUILDSCRIPTS_ARCHIVE}" -C "${BUILDSCRIPTS_DIR}" -x
	tar -f "${_APPS_ARCHIVE}" -C "${APPS_DIR}" -x
	tar -f "${_MODULES_ARCHIVE}" -C "${MODULES_DIR}" -x
}

# set paths, needed when building from sources (git repo)
set_component_dirs(){
	APPS_DIR="$(realpath "${SCRIPT_DIR}"/../apps)"
	BUILDSCRIPTS_DIR="$(realpath "${SCRIPT_DIR}")"
	MODULES_DIR="$(realpath "${SCRIPT_DIR}"/../modules)"
	ensure_that_there_is_at_least_one_file_within_dir "${APPS_DIR}"
	ensure_that_there_is_at_least_one_file_within_dir "${BUILDSCRIPTS_DIR}"
	ensure_that_there_is_at_least_one_file_within_dir "${MODULES_DIR}"
}

build_kernel(){
(
	VCA_PATCH_DIR="${VCA_IN_DIR}/${DISTRO}_with_kernel_${KER_VER}"
	if [ "${DISTRO}" = centos ]; then
		VCA_DOT_CONFIG_FILE="$(find_one "${VCA_IN_DIR}" -name "kernel*${KER_VER}-vca.config" -type f)"
	fi
	if [[ ! "${KER_VER}" =~ ^3 ]]; then
		# version 4.19.0 is released as linux-4.19, so remove potential .0 suffix from KER_VER
		tar --strip-components=1 -f "${DOWNLOAD_DIR}/linux-${KER_VER%.0}.tar.xz" -C "${KERNEL_DIR}" -x
	fi
	cd "${KERNEL_DIR}"
	export SRPM_DIR="${DOWNLOAD_DIR}" VCA_PATCH_DIR VCA_DOT_CONFIG_FILE
	bash "${BUILDSCRIPTS_DIR}"/quickbuild/generate_kernel.sh # "bash" to omit "-x" from script's shebang
	publish_files mv ../output "${OUT_DIR}" \
		"kernel-${KER_VER}*VCA*rpm" "linux-image-${KER_VER}*vca*deb" \
		"kernel-devel-${KER_VER}*.rpm" "linux-headers-${KER_VER}*.deb" # publish artifacts as soon as possible
)}

build_modules(){
(
	cd "${MODULES_DIR}"
	LINUX_HEADERS_OR_KERNEL_DEVEL_PKG="$(find_one "${OUT_DIR}" -name "kernel-devel-${KER_VER}*.rpm" -o -name "linux-headers-${KER_VER}*.deb")" \
		bash ./generate_modules.sh
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
	ensure_that_there_is_at_least_one_file_within_dir "${BOOST_ROOT}" 2>/dev/null \
		|| build_boost_lib
	cd "${APPS_DIR}"
	MODULES_SRC="${MODULES_DIR}" BOOST_ROOT="${BOOST_ROOT}" \
		bash ./generate_apps.sh
	publish_files mv ../output "${OUT_DIR}" 'daemon-vca*rpm' 'daemon-vca*deb'
)}

build_image(){
(
	if [ "${DISTRO}" = ubuntu ]; then
		local _ARTIFACTS_DIR _BASE_IMG_VER
		_BASE_IMG_VER="$(grep -Po 'VERSION_ID="?\K[^"]+' < /etc/os-release)" # like 18.04
		"${BUILDSCRIPTS_DIR}"/create_sw_archives.sh \
			--base-and-bootstrap-only \
			-d "UBUNTU ${_BASE_IMG_VER}" \
			-k "${KER_VER}-1.${PKG_VER}.vca" \
			-o "${OUT_DIR}"
		_ARTIFACTS_DIR="${EXTRACT_DIR}/artifacts"
		mkdir -p "${_ARTIFACTS_DIR}"
		# copy selected files as workaround for no option to select subset of deb's/rpm's in artifacts directory
		publish_files cp "${OUT_DIR}" "${_ARTIFACTS_DIR}" \
			"kernel-${KER_VER}*VCA*rpm" "linux-image-${KER_VER}*vca*deb" \
			"vcass-modules-${KER_VER}*VCA*rpm" "vcass-modules-${KER_VER}*vca*deb"
		cd "${OUT_DIR}" # cd to directory with SAs, as workaround for no option to specify location of SAs
		ARTIFACTS_DIR="${_ARTIFACTS_DIR}" DIST="${_BASE_IMG_VER}" OUT"${OUT_DIR}" SKIP_INITIAL_CLEANUP=yes OS=UBUNTU FEATURE=STD "${BUILDSCRIPTS_DIR}"/quickbuild/generate_images.sh
		echo -e "\n\nIntermediate Software Archives (SAs) were saved in ${OUT_DIR}, next build will recreate them. If for any reason do you want to reproduce exactly this build, keep them for reuse (manual execution of generate_images.sh would be necessary)\n" >&2
	fi
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

ensure_that_there_is_at_least_one_file_within_dir(){
	local _DIR; _DIR="$1"
	[ -d "${_DIR}" ] || die "${_DIR} is not a directory, but was provided as input directory for $0"
	# empty grep pattern fails only for empty input, head is to avoid listing really big tree structures
	find "${_DIR}" -type f | head -1 | grep -q '' || {
		echo "${_DIR} does not contain any files, but was provided as input directory for $0" >&2
		return 3
	}
}

# run $1 almost silently (2 stdout lines + whole stderr still will be reported)
silent(){
	local _FUN; _FUN="$1"
	shift
	echo "** calling ${_FUN}"
	# keep last stdout line, to make it clear that build was ok even with warnings present
	${_FUN} "$@" \
		| tail -1 \
		| awk "{ print \"[last line of ${_FUN}()]:\", \$0 }"
}

# same as find, but ensure that there is exactly one output line (file or dir)
find_one(){
	find "$@" | awk '
		$0 != "" { ++lines; print }
		END { if (lines != 1) {
			print lines+0, "matches found, but exactly 1 is required\n(was looking for: find '"$*"')" > "/dev/stderr"
			exit 2
		}}'
}

main(){
	local _EXTRACT_RELEASE_PACKAGE_ERRORS
	SCRIPT_DIR="$(realpath "$(dirname "$0")")"
	EXTRACT_DIR="${EXTRACT_DIR:-$(mktemp -d)/extract-dir}" # more than one level, to workaround "../output" (hardcoded) default in some scripts
	BOOST_ROOT="${BOOST_ROOT:-${EXTRACT_DIR}/${BOOST_VER}}"
	KERNEL_DIR="${EXTRACT_DIR}"/kernel
	mkdir -p "${BOOST_ROOT}" "${KERNEL_DIR}"

	if find_one "${VCA_IN_DIR}" -type f -name 'vca_apps*' &>/dev/null; then
		extract_release_package
	else
		echo 'It looks that you are building from repository, becasue content expected to be extracted from release package zip archive was not found' >&2
		set_component_dirs || \
			die "Invalid repository structure. Aborting, because attempt to build from release package yield following errors:\n${_EXTRACT_RELEASE_PACKAGE_ERRORS}"
	fi

	DISTRO="$(grep -o -e ubuntu -m1 /etc/os-release || echo centos)"
	OUT_DIR="${OUT_DIR}/${DISTRO}_with_kernel_${KER_VER}"
	mkdir -p "${OUT_DIR}"
	export PKG_VER KER_VER

	silent build_kernel
	silent build_modules
	[ "${COMPONENT}" = host  ] && silent build_apps
	[ "${COMPONENT}" = image ] && silent build_image
	return 0
}

echo "*** Called as: $0 $*" >&2
parse_parameters "$@"
main && echo "*** Finished: $0 $*" >&2

#!/bin/bash
set -Eeuo pipefail

#### README ####
# This scripts builds VCA software binaries for selected configurations using docker.
# Minimal docker version: v17.05.0-ce
# See README.txt for further informations.

# params to set via command line switches
VCA_IN_DIR=
DOWNLOAD_DIR=
OUT_DIR=
ONLY_PATTERN= # optional

## selection of binaries to build, comment out unwanted ones (by using '#')
main(){
	build centos 7.4.1708 host 3.10.0-693
	build centos 7.6.1810 host 5.1.16
	build ubuntu 16.04 host 4.14.20
	build ubuntu 18.04 host 4.19.0
	build ubuntu 18.04 image 4.19.0
	print_summary
}

usage(){
echo -n 'Mandatory arguments:
--vca-src-dir <vca-sw-input-dir>
	path to extracted VCASS release package or to VCASS sources cloned from git repository
--downloads-dir <downloaded-dependencies-dir>
	path to downloaded 3rd party binaries
--out-dir <output-dir>
	path to place resulting binaries (will be automatically created)

Optional arguments:
--only-pattern <pattern>
	build only configurations which match <pattern> (by grep -E) (see available configurations in '"$0"'::main())
';}

stderr(){ echo "*** $*" >&2; }
die(){
	EXIT_CODE=$?
	EXIT_CODE=$((EXIT_CODE == 0 ? 99 : EXIT_CODE))
	stderr "FATAL ${FUNCNAME[1]}(): $*"
	exit ${EXIT_CODE}
}
trap 'die "(cmd: $BASH_COMMAND)"' ERR

check_prerequisites(){
	command -v docker >/dev/null || die 'docker must be installed prior to building VCA software binaries, minimal required version is 17.05'
	docker --version | cut -f3 -d' ' | awk -F. '$1 < 17 || ($1 == 17 && $2 < 5) { exit 17 }' \
		|| die "$(docker --version) is too old, minimal required version is 17.05"
	docker run hello-world >/dev/null || die 'docker is unable to start any container, perhaps it is not configured correctly
	for HTTP proxy configuration see: https://docs.docker.com/config/daemon/systemd/
	for granting permission to docker deamon see: https://docs.docker.com/install/linux/linux-postinstall/'
}

parse_parameters(){
	# realpath is used to allow user to specify current directory as single dot (.), docker does not support one-letter-long volume names
	while [ $# -gt 1 ]; do
		case "${1}" in
		--vca-src-dir) VCA_IN_DIR="$(realpath "${2}")";;
		--downloads-dir) DOWNLOAD_DIR="$(realpath "${2}")";;
		--out-dir) OUT_DIR="$(realpath "${2}")";;
		--only-pattern) ONLY_PATTERN="${2}";;
		*) die "unrecognized parameter: '$1', run $0 without arguments for usage information";;
		esac
		shift 2
	done
	if [ -z "${VCA_IN_DIR}" ] || [ -z "${DOWNLOAD_DIR}" ] || [ -z "${OUT_DIR}" ]; then
		usage
		exit 1
	fi
	ensure_that_there_is_at_least_one_file_within_dir "${VCA_IN_DIR}"
	ensure_that_there_is_at_least_one_file_within_dir "${DOWNLOAD_DIR}" || die "Run\n\t${VCA_IN_DIR}/download_dependencies.sh '${DOWNLOAD_DIR}'\nto download missing files."
	mkdir -p "${OUT_DIR}"
}

# build artifacts for configuration given by ${DISTRO} via first argument
# it builds docker image using Dockerfile: $DOCKERFILE_NAME
# shellcheck disable=SC2207
build(){
	local DISTRO BASE_IMG_VER COMPONENT COMPONENT_PARAM
	local DOCKER_BUILD_ARGS DOCKER_RUN_ARGS DOCKERFILE_NAME
	local KNAME KNAME_PREFIX DOCKER_CONTEXT_DIR
	DISTRO="$1"
	BASE_IMG_VER="$2"
	COMPONENT="$3"
	COMPONENT_PARAM="$4"
	if ! grep -qE "${ONLY_PATTERN}" <<< "$@"; then
		echo "Configuration $* not selected by --only-pattern"
		return
	fi
	DOCKERFILE_NAME="${VCA_IN_DIR}/Dockerfiles/build_${DISTRO}.dockerfile"
	case "${DISTRO}" in
		centos) KNAME_PREFIX='kernel-' ;;
		ubuntu) KNAME_PREFIX='kernel_' ;;
		*) die "unknown build image to base from requested: '${DISTRO}:${BASE_IMG_VER}'" ;;
	esac
	KNAME="${KNAME_PREFIX}${COMPONENT_PARAM}"
	[[ "${COMPONENT_PARAM}" =~ ^[0-9.-]+$ ]] || die "unsupported kernel version specification format: '${COMPONENT_PARAM}'"
	case "${COMPONENT}" in
		host) ;;
		image) DOCKER_RUN_ARGS+=(--privileged -v /dev:/dev) # taken from: (long discussion) https://github.com/moby/moby/issues/27886#issuecomment-281280504
		;;
		*) die "unsupported component: '${COMPONENT}'" ;;
	esac
	DOCKER_BUILD_ARGS+=($(concat_docker_arg build-arg BASE_IMG_VER))
	DOCKER_BUILD_ARGS+=($(concat_docker_arg build-arg http_proxy))
	DOCKER_BUILD_ARGS+=($(concat_docker_arg build-arg https_proxy))
	DOCKER_BUILD_ARGS+=($(concat_docker_arg build-arg HTTP_PROXY))
	DOCKER_BUILD_ARGS+=($(concat_docker_arg build-arg HTTPS_PROXY))
	DOCKER_RUN_ARGS+=($(concat_docker_arg env http_proxy))
	DOCKER_RUN_ARGS+=($(concat_docker_arg env https_proxy))
	DOCKER_RUN_ARGS+=($(concat_docker_arg env HTTP_PROXY))
	DOCKER_RUN_ARGS+=($(concat_docker_arg env HTTPS_PROXY))

	# build docker image
	DOCKER_CONTEXT_DIR="$(mktemp -d)"
	cp "${VCA_IN_DIR}"/build.sh	"${DOCKER_CONTEXT_DIR}"/build.sh
	cp "${DOCKERFILE_NAME}"		"${DOCKER_CONTEXT_DIR}"/Dockerfile
	chmod +x "${DOCKER_CONTEXT_DIR}"/build.sh
	docker build --tag="${KNAME}" "${DOCKER_BUILD_ARGS[@]}" "${DOCKER_CONTEXT_DIR}"
	rm -rf "${DOCKER_CONTEXT_DIR}"

	trap - ERR
	# run docker container
	# ../output is used in several places, since generate_*sh scripts puts their results to ../output; for simplicity this is not changed
	# before '"${KNAME}"' there are arugments for docker run, -v are volumes mappings
	# after  '"${KNAME}"' there are arugments to be passed to build.sh, note that paths are docker containers filesystem
	docker run -i --rm \
		"${DOCKER_RUN_ARGS[@]}" \
		-v "${VCA_IN_DIR}":/vca-input \
		-v "${DOWNLOAD_DIR}":/download \
		-v "${OUT_DIR}":/vca-output \
		"${KNAME}" \
		--component	"${COMPONENT}" \
		--downloads-dir	/download \
		--kernel-ver	"${COMPONENT_PARAM}" \
		--out-dir	/vca-output \
		--vca-src-dir	/vca-input
}

ensure_that_there_is_at_least_one_file_within_dir(){
	local _DIR; _DIR="$1"
	[ -d "${_DIR}" ] || die "${_DIR} is not a directory, but was provided as input directory for $0"
	# empty grep pattern fails only for empty input, head is to avoid listing really big tree structures
	find "${_DIR}" -type f | head -1 | grep -q '' \
		|| die "${_DIR} does not contain any files, but was provided as input directory for $0"
}

# make --env or --build-arg for docker run/build
# arguments: $1: 'env' or 'build-arg'
#            $2: var name
#            $3: var value, value held under $!2 if $3 empty
concat_docker_arg(){
	local SWITCH VAR_NAME VAR_VALUE
	SWITCH="--$1"
	VAR_NAME="$2"
	VAR_VALUE="${3-${!VAR_NAME-}}"
	echo "$SWITCH" "$VAR_NAME=$VAR_VALUE"
}

print_summary(){
	echo "

To list all build binaries invoke:
	find '${OUT_DIR}' -type f | sort
To list only most often needed files invoke:
	find '${OUT_DIR}' -type f | sort | grep -v -e devel -e headers -e perf -e tools -e debug -e libc -e src -e dbg
"
}

parse_parameters "$@"
check_prerequisites
main

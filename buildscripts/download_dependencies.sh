#!/bin/bash

# This file lists all publicly available third party components,
# which are necessary to build binary components for VCA cards.

set -Eeuo pipefail
trap 'echo "(cmd failed: $BASH_COMMAND)" >&2' ERR

###############################################################################
# Modify the *_URL variables in the following section to match your preffered
# providers. There are more URLs available than the ones mentioned below.
###############################################################################

## select (uncomment) server with CentOS src.rpm's and iso.
#CENTOS_BASE_URL='http://vault.centos.org'
#CENTOS_BASE_URL='http://archive.kernel.org/centos-vault' # for USA
CENTOS_BASE_URL='http://mirror.nsc.liu.se/centos-store' # for Europe

## select (uncomment) server with kernel for Ubuntu
KERNEL_BASE_URL='https://mirrors.edge.kernel.org/pub/linux/kernel'

## boost source package
BOOST_SRC_URL='https://dl.bintray.com/boostorg/release/1.65.1/source/boost_1_65_1.tar.gz'

## elrepo source packages
ELREPO_BASE_URL='https://mirror.imt-systems.com/elrepo/archive/kernel'
ELREPO_BASE_URL='http://mirrors.coreix.net/elrepo-archive-archive/kernel'

## ubuntu custom patches
UBUNTU_BASE_URL='https://launchpad.net/ubuntu/+archive/primary/+sourcefiles/linux'

###############################################################################
# End of modifiable section. Any edits below this line are at your own risk.
###############################################################################

## params to set via command line switches
DOWNLOAD_DIR=
ONLY_PATTERN=vcac-r # optional

parse_parameters(){
	while [ $# -gt 1 ]; do
		case "${1}" in
		--downloads-dir) DOWNLOAD_DIR="$2";;
		--only-pattern)  ONLY_PATTERN="$2";;
		*) echo "ignoring unrecognized parameter: '$1', run $0 without arguments for usage information" >&2;;
		esac
		shift 2
	done
	if [ -z "${DOWNLOAD_DIR}" ]; then
		usage; exit 12
	fi
}

usage(){
echo -n 'Mandatory arguments:
--downloads-dir <downloaded-dependencies-dir>
	path to downloaded 3rd party binaries

Optional arguments:
--only-pattern <pattern>
	download dependencies only for configurations which match <pattern> (by grep -E) (see available configurations in '"$0"'::main())
';}

if [ -z "${CENTOS_BASE_URL}" ]; then
	echo 'CENTOS_BASE_URL not provided' >&2; exit 10 # (wget has exit codes <0..8>)
fi
if [ -z "${KERNEL_BASE_URL}" ]; then
	echo 'KERNEL_BASE_URL not provided' >&2; exit 11
fi
if [ -z "${ELREPO_BASE_URL}" ]; then
	echo 'ELREPO_BASE_URL not provided' >&2; exit 13
fi
if [ -z "${UBUNTU_BASE_URL}" ]; then
	echo 'UBUNTU_BASE_URL not provided' >&2; exit 14
fi

# download $1 if $* matches $ONLY_PATTERN
download(){
	local _URL; _URL="$1"; shift
	if [ "${1-}" != all ] && ! grep -qE "${ONLY_PATTERN}" <<< "$*"; then
		echo "File '${_URL}' not selected for download by --only-pattern" >&2
		return
	fi
	echo "downloading ${_URL}..." >&2
	wget -c "${_URL}"
}

main(){
	mkdir -p "${DOWNLOAD_DIR}"
	cd "${DOWNLOAD_DIR}"

	download "${CENTOS_BASE_URL}/7.6.1810/updates/Source/SPackages/kernel-3.10.0-957.12.2.el7.src.rpm"	vcac-r sgx
	download "${KERNEL_BASE_URL}/v4.x/linux-4.19.tar.xz"	vcac-r
	download "${BOOST_SRC_URL}" all
	echo 'All files downloaded successfully' >&2
}

parse_parameters "$@"
main

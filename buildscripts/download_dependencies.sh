#!/bin/bash

# This file lists all publicly available third party components,
# which are necessary to build binary components for VCA cards.

set -Eeuo pipefail
trap 'echo "(cmd failed: $BASH_COMMAND)" >&2' ERR

## Modify *_URL variables with your preffered providers.
## There are more options available that those already listed.

## select (uncomment) server with CentOS src.rpm's and iso.
#CENTOS_BASE_URL='http://vault.centos.org/7.4.1708'
#CENTOS_BASE_URL='http://archive.kernel.org/centos-vault/7.4.1708' # for USA
CENTOS_BASE_URL='http://mirror.nsc.liu.se/centos-store/7.4.1708' # for Europe

## select (uncomment) server with kernel for Ubuntu
KERNEL_BASE_URL='https://mirrors.edge.kernel.org/pub/linux/kernel'

## boost source package
BOOST_SRC_URL='https://dl.bintray.com/boostorg/release/1.65.1/source/boost_1_65_1.tar.gz'

## elrepo source packages
ELREPO_BASE_URL='https://mirror.imt-systems.com/elrepo/archive/kernel'

if [ -z "${CENTOS_BASE_URL}" ]; then
	echo 'CENTOS_BASE_URL not provided' >&2; exit 10 # (wget has exit codes <0..8>)
fi
if [ -z "${KERNEL_BASE_URL}" ]; then
	echo 'KERNEL_BASE_URL not provided' >&2; exit 11
fi
if [ -z "${ELREPO_BASE_URL}" ]; then
	echo 'ELREPO_BASE_URL not provided' >&2; exit 13
fi
if [ -z "${1-}" ]; then
	echo "Usage: $0 <path-to-store-downloaded-files>" >&2; exit 12
fi

DOWNLOAD_DIR="$1"
mkdir -p "${DOWNLOAD_DIR}"
cd "${DOWNLOAD_DIR}"

URLS_TO_DOWNLOAD=()
URLS_TO_DOWNLOAD+=("${CENTOS_BASE_URL}/updates/Source/SPackages/kernel-3.10.0-693.17.1.el7.src.rpm")
URLS_TO_DOWNLOAD+=("${KERNEL_BASE_URL}/v4.x/linux-4.14.20.tar.xz")
URLS_TO_DOWNLOAD+=("${KERNEL_BASE_URL}/v4.x/linux-4.19.tar.xz")
URLS_TO_DOWNLOAD+=("${KERNEL_BASE_URL}/v5.x/linux-5.1.16.tar.xz")
URLS_TO_DOWNLOAD+=("${BOOST_SRC_URL}")
URLS_TO_DOWNLOAD+=("${ELREPO_BASE_URL}/el7/SRPMS/kernel-ml-5.1.16-1.el7.elrepo.nosrc.rpm")

(IFS=$'\n'; echo -e "Files to download:\n${URLS_TO_DOWNLOAD[*]}\n")

wget -c "${URLS_TO_DOWNLOAD[@]}"
echo 'All files downloaded successfully' >&2

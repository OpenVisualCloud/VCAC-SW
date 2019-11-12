#!/bin/bash -x
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

set -eEo pipefail

function print_help {
	echo 'Synopsis:'
	echo "OS={CENTOS|DEBIAN|UBUNTU} [BOOST_ROOT=<non-empty string>] [PKG_VER=<pkg version>] [SGX=y|n] [WORKSPACE=<intermediate and output dir>] [MODULES_SRC=<path to VCA modules sources (default=../ValleyVistaModules)] $0"
	echo '	where:'
	echo '		non-empty BOOST_ROOT: use the indicated Boost library instead of system-wide libraries'
	echo '		SGX set to "y": compile for SGX card. Otherwise compile for non-SGX cards)'
	echo 'Example usage:'
	echo 'OS=UBUNTU PKG_VER=2.0.0 ../ValleyVistaBuildScripts/quickbuild/generate_apps.sh'
	echo 'OS=CENTOS WORKSPACE=/tmp PKG_VER=2.0.0 ../ValleyVistaBuildScripts/quickbuild/generate_apps.sh'
}

function die {
	rc=$?
	test $rc = 0 && rc=1
	echo -e "$@" >&2
	exit $rc
}

function gen_apps {
	# generate tar.gz with full source
	local _SRC_ARCHIVE_PATH _CMAKE_DEFS
	_SRC_ARCHIVE_PATH="${WORKSPACE}/vca_apps_${PKG_VER}_src.tar.gz"
	tar --exclude './.*' --exclude "${_SRC_ARCHIVE_PATH}" -zcf "${_SRC_ARCHIVE_PATH}" . || die 'Could not make source archive'

	# build requested out of source-tree by the use of -H, -B options:
	_CMAKE_DEFS=("-DOS=${OS}" "-DPKG_VER=${PKG_VER}" "-DDESTDIR=" "-DSGX=${SGX}")
	if [ -n "${MODULES_SRC-}" ]; then
		_CMAKE_DEFS+=("-DMODULES_SRC=${MODULES_SRC}")
	fi
	cmake "${_CMAKE_DEFS[@]}" -H. -B"${WORKSPACE}" || die 'Generating project with cmake failed'
	(
		cd "${WORKSPACE}"
		# first try to build in parallel, but retry in single threaded mode (beacause some(times) build machines are flaky)
		make -j HOME="${WORKSPACE}" 2>/dev/null \
			|| make HOME="${WORKSPACE}" \
			|| die 'Executing make failed'
		cpack -DSGX="${SGX}" || die "Error making deb package"
	)
}
# main:
echo "Generate vca apps"
echo "PKG_VER: ${PKG_VER:=0.0.0}"
echo "OS: ${OS:=$(grep -o -e UBUNTU -e DEBIAN -m1 /etc/os-release || echo CENTOS)}"
echo "BOOST_ROOT: ${BOOST_ROOT}"
echo "SGX: ${SGX}"
: "${WORKSPACE=$(realpath ../output)}" # quickbuild starts in ValleyVistaApps so take parent dir; rpmbuild needs absolute path
echo "WORKSPACE: ${WORKSPACE}"
mkdir -p "${WORKSPACE}"
if [ "${SGX}" = n ] ; then
	export SGX=''
fi
gen_apps
EXIT_CODE=$?
echo "Build has ended with status: ${EXIT_CODE}"
exit ${EXIT_CODE}

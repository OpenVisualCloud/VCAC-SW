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

set -euEo pipefail

trap 'die "(cmd: $BASH_COMMAND)"' ERR

function print_usage {
	set +x
	echo "Synopsis:
	PKG_VER=<pkg version> KER_VER=<kernel version> \\
	[OS={CENTOS|DEBIAN|UBUNTU}] \\
	[WORKSPACE=<output dir>] \\
	[SRPM_DIR=<input dir with kernel src.rpm or nosrc.rpm>] \\
	[VCA_PATCH_DIR=<dir with patches to apply instead of using code from git repository>] \\
	[VCA_DOT_CONFIG_FILE=<file to use as .config instead of the one in git repo>] \\
	${SCRIPT_NAME}
where:
	WORKSPACE is ../ by default (currently: ${WORKSPACE})
	SRPM_DIR is used only for CENTOS 3.10.0 and 5.1.16. The name of the rpm is hardcoded in this script.
	VCA_PATCH_DIR is used when all kernel patches are provided by external means,
		on CentOS .config needs to be provided via VCA_DOT_CONFIG_FILE parameter.
		On Ubuntu application of patches from VCA_PATCH_DIR should also generate .config.
	VCA_DOT_CONFIG_FILE is used when .config file is provided by external means (only for CentOS).
Example usage:
OS=UBUNTU PKG_VER=1.2.3 KER_VER=4.19.0 ${SCRIPT_NAME}
OS=CENTOS PKG_VER=1.2.3 KER_VER=3.10.0-693 ${SCRIPT_NAME}
OS=CENTOS PKG_VER=1.2.3 KER_VER=3.10.0-693 [WORKSPACE=/tmp] SRPM_DIR=~/srcRpmPkgs ${SCRIPT_NAME}"
}

function die {
	EXIT_CODE=$?
	set +x
	EXIT_CODE=$((EXIT_CODE == 0 ? 99 : EXIT_CODE))
	echo "${SCRIPT_NAME}: $*" >&2
	echo "${SCRIPT_NAME}: exit code = ${EXIT_CODE}"  >&2
	exit ${EXIT_CODE}
}

function prepare_src_rpm {
	local _SPECFILE="$1"
	local _PKG_VER="$2"
	local _PATCH_NAME="$3"
	local _SOURCE_RPM_NAME="$4"

	local LAST_PATCH="Patch1002: debrand-rh-i686-cpu.patch"
	sed --in-place \
		-e "s/%define rheltarball %{rpmversion}-%{pkgrelease}/%define rheltarball %{rpmversion}-%{orig_pkgrelease}/g" \
		-e "s/Source0: linux-%{rpmversion}-%{pkgrelease}.tar.xz/Source0: linux-%{rpmversion}-%{orig_pkgrelease}.tar.xz/g" \
		-e "/${LAST_PATCH}/a Patch2000: ${_PATCH_NAME}" \
		-e "/ApplyOptionalPatch debrand-rh-i686-cpu.patch/a ApplyOptionalPatch ${_PATCH_NAME}" \
		"${_SPECFILE}"

	local _SUBVERSION; _SUBVERSION=$(echo "${_SOURCE_RPM_NAME}" | cut -d'-' -f 3 | cut -d'.' -f 1 )
	local _SUBSUBVERSION; _SUBSUBVERSION=$(echo "${_SOURCE_RPM_NAME}" | cut -d'.' -f 4,5 )
	sed --in-place \
		-e "/%global distro_build ${_SUBVERSION}/a %define vcarevision .${_PKG_VER}" \
		"${_SPECFILE}"
	# if _SUBSUBVERSION is a non-empty string of digits, or two such strings separated with dot:
	if [[ ${_SUBSUBVERSION} =~ ^[0-9]+([.][0-9]+)?$ ]]; then
		sed --in-place \
			-e "s/%define pkgrelease ${_SUBVERSION}.${_SUBSUBVERSION}.el7/%define orig_pkgrelease ${_SUBVERSION}.${_SUBSUBVERSION}.el7/g" \
			-e "/%define orig_pkgrelease ${_SUBVERSION}.${_SUBSUBVERSION}.el7/a %define pkgrelease ${_SUBVERSION}.${_SUBSUBVERSION}.el7%{vcarevision}" \
			-e "s/%define specrelease ${_SUBVERSION}.${_SUBSUBVERSION}%{?dist}/%define specrelease ${_SUBVERSION}%{?dist}.centos%{vcarevision}/g" \
			"${_SPECFILE}"
	else
		sed --in-place \
			-e "s/%define pkgrelease ${_SUBVERSION}.el7/%define orig_pkgrelease ${_SUBVERSION}.el7/g" \
			-e "/%define orig_pkgrelease ${_SUBVERSION}.el7/a %define pkgrelease ${_SUBVERSION}.el7%{vcarevision}" \
			-e "s/%define specrelease ${_SUBVERSION}%{?dist}/%define specrelease ${_SUBVERSION}%{?dist}%{vcarevision}/g" \
			"${_SPECFILE}"
	fi
}

function prepare_nosrc_rpm {
	local _SPECFILE="$1"
	local _PKG_VER="$2"

	local _SOURCE0; _SOURCE0="$(basename "$(rpmspec -P "${_SPECFILE}" | grep "^Source0: " | cut -d" " -f2)")"
	# modify %setup to not unarchive sources, and link src directory instead:
	touch "$(HOME="${WORKSPACE}" rpm --eval '%{_sourcedir}')/${_SOURCE0}" # listed source must exists for rpmbuild
	sed --in-place \
		-e "s|%setup -q -n %{name}-%{version} -c|%setup -q -n %{name}-%{version} -c -T -D\n\
		%{__cp} %{_sourcedir}/cpupower.* .\n\
		%{__ln_s} $(realpath "$(pwd)") %{_builddir}/%{name}-%{version}/linux-%{LKAver}|" \
		"${_SPECFILE}"
	if [ -z "${VCA_DOT_CONFIG_FILE-}" ]; then
		# extract .config despite unclean repo and ensure it will to be copied to its destination during build:
		local _CONFIG; _CONFIG=$(mktemp)
		git show :.config > "${_CONFIG}"
	fi
	sed --in-place \
		-e "s|%{__cp} config-%{version}-%{_target_cpu} .config|\n\
		%{__cp} ${VCA_DOT_CONFIG_FILE-${_CONFIG}} .config|" \
		"${_SPECFILE}"
	# workaround for the bug in rpmbuild/ELREPO's kernel-ml-headers-5.1.16-1.el7.x86_64: cpupower.lang not found during %install rpmbuild phase if HOME has been modified because the file has been created in wrong directory tree:
	sed --in-place \
		-e "s|mv cpupower.lang ../|\n\
		mv cpupower.lang %{_builddir}/%{name}-%{version}/|" \
		"${_SPECFILE}"
	# update package name & release:
	sed --in-place \
		-e "s/^Name: kernel-ml$/Name: kernel/" \
		-e "s/^%define pkg_release .*/&.${_PKG_VER}/" \
		"${_SPECFILE}"
}

# When _SRPM_NAME is a src rpm: use repo only to generate patches for the src rpm, then build the src rpm package
# When _SRPM_NAME is a nosrc rpm: build from repo's source code using rpm spec file from _SRPM_NAME
function generate_centos_from_repo_and_src_rpm {
	local _SRPM_NAME="$1"
	local _PKG_VER="$2"
	: "${SRPM_DIR:=/nfs/igk/disks/igk_valley_vista001/toolchain}"
	[ "$(realpath "${SRPM_DIR}/${_SRPM_NAME}")" != "$(realpath "${WORKSPACE}")" ] && cp "${SRPM_DIR}/${_SRPM_NAME}" "${WORKSPACE}"

	local _RPM_TOPDIR; _RPM_TOPDIR=$(HOME="${WORKSPACE}" rpm --eval '%{_topdir}') || die 'could not eval rpmbuild %{_topdir}'
	HOME="${WORKSPACE}" rpm -i "${WORKSPACE}/${_SRPM_NAME}" || die "could not install ${WORKSPACE}/${_SRPM_NAME}"

	local _SPECFILE _PATCH_NAME _VCA_PATCH
	# if patches are given, apply them before build
	if [ -n "${VCA_PATCH_DIR-}" ]; then
		_VCA_PATCH="$(find "${VCA_PATCH_DIR}" -type f -name '*.patch')"
		if [ "$(wc -l <<< "${_VCA_PATCH}")" != 1 ]; then
			die "${FUNCNAME[0]}: VCA_PATCH_DIR must be a directory with exactly one *.patch file, got: ${VCA_PATCH_DIR}/
				(If you need more patches just concatenate them in right order)"
		fi
		_PATCH_NAME="$(basename "${_VCA_PATCH}")"
		cp "${_VCA_PATCH}" "${_RPM_TOPDIR}"/SOURCES/
	fi
	if [[ ${_SRPM_NAME} = *.src.rpm ]] ; then
		_SPECFILE="${_RPM_TOPDIR}"/SPECS/kernel.spec
		# if patches were not given, generate them
		if [ -z "${VCA_PATCH_DIR-}" ]; then
			_PATCH_NAME="vca_patches.patch"
			# Operators || and && have the same precedence when used out of [[...]]:
			{ git diff --quiet --staged && git diff --quiet; } || die "git work tree $(pwd) has changes on top of HEAD (see 'git diff' / 'git status'). Commit, discard, or stash them"
			git format-patch "${KERNEL_BASE_COMMIT}" --stdout > "${_RPM_TOPDIR}/SOURCES/${_PATCH_NAME}" || die "git format-patch ${KERNEL_BASE_COMMIT} failed"
		fi

		# Create two new copies of .config named with _CONFIGPREFIX, prepending ARCH for rpmbuild standard %prep phase:
		local _KERNEL_VERSION; _KERNEL_VERSION=$( rpm -qp  --queryformat '%{VERSION}' "${WORKSPACE}/${_SRPM_NAME}" )
		local _CONFIGPREFIX="${_RPM_TOPDIR}"/SOURCES/kernel-"${_KERNEL_VERSION}"-x86_64
		sed '1 i\# x86_64' "${VCA_DOT_CONFIG_FILE-.config}" | tee "${_CONFIGPREFIX}".config > "${_CONFIGPREFIX}"-debug.config

		prepare_src_rpm "${_SPECFILE}" "${_PKG_VER}" "${_PATCH_NAME}" "${_SRPM_NAME}"
	else # so this is *.nosrc.rpm
		_SPECFILE=$(ls "${_RPM_TOPDIR}"/SPECS/*.spec)	# 'ls' to store the filename, not pattern
		prepare_nosrc_rpm "${_SPECFILE}" "${_PKG_VER}"
		export LOCALVERSION="" # for rpmbuild to compile in unclean git repository without appending '+' to kernel version
	fi
	HOME="${WORKSPACE}" rpmbuild -ba --without kabichk --without debug --without debuginfo "${_SPECFILE}" || die 'Build failed'
}

# this will just use Makefile provided with kernel and build following packages:
# kernel, kernel-devel, kernel-headers, kernel.src.rpm
#
# for simplicity (for us and perhaps also for client) patches are provided via zip
function generate_k3_centos_from_repo {
	git format-patch "${KERNEL_BASE_COMMIT}" || die 'could not format patches'
	zip "${WORKSPACE}/vca_patches.zip" ./*\.patch || die 'could not make zip'

	make -j "$(nproc)" rpm-pkg \
		HOME="${WORKSPACE}" \
		KERNELRELEASE="${KER_VER}-1.${PKG_VER}.VCA" \
		RPMVERSION="${PKG_VER}" \
		LOCALVERSION='' \
		|| die 'make rpm-pkg failed'
}

function generate_k4_centos {
	make -j "$(nproc)" rpm \
		HOME="${WORKSPACE}" \
		KERNELRELEASE="${KER_VER}-1.${PKG_VER}.VCA" \
		RPMVERSION="${PKG_VER}" \
		LOCALVERSION='' \
		INSTALL_MOD_STRIP=1 \
		|| die 'Make rpm failed'
}

# according to https://wiki.ith.intel.com/pages/viewpage.action?pageId=823271743
# we mark vanilla kernel in repo (eg. with commit containing "Base Kernel",
# but it is not always the case)
# all descendant commits are treated as patches
# and applied over base source rpm
function get_kernel_base {
	# _BASE_SHA1 should be commit marked as base kernel, so
	# it is the one with "Base Kernel" *if there is such commit*
	# otherwise it is last commit with word 'kernel'
	# (or some other special string for selected kernels)

	local _BASE_SHA1
	# shellcheck disable=SC1004
	# (backslashes in awk code are for awk, not bash)
	_BASE_SHA1=$(git log --oneline \
		| awk '
		/Base Kernel/ \
		|| /Source code of Debian Linux 3\.16/ \
		|| /Apply (ubuntu )?patch 4\./ \
		|| /Add 4.10-gvt-stable sources/ \
		|| /Added linux-4.14.20 source directly from MSS repos./ \
		|| /Merge tag .v4.4.155.+git.kernel.org.+ into prv-v4.4.155_vca/ \
		{
			print "Base commit which will be used: " $0 > "/dev/stderr"
			print $1 # just commit hash, no msg
			exit  0  # do not process any further commits, use this one as base
		}
		/[Kk]ernel/ { print $1 }' \
		| tail -n1)
	test -n "${_BASE_SHA1}" \
		|| die "base kernel not found, current branch: $(git rev-parse --abbrev-ref HEAD)"
	echo "${_BASE_SHA1}"
}

function git_rebase_autosquash() {
	local _BASE_SHA1="$1"

	# Note: GIT_SEQUENCE_EDITOR holds name of editor to be invoked
	# by git-rebase to edit history in-place
	GIT_SEQUENCE_EDITOR=true \
	git rebase --interactive --autosquash "${_BASE_SHA1}" || {
		# Note2: old git (eg 2.7.4 from Ubuntu 16.04) is unable to continue
		# with empty commit which results from squash of commit+its_revert, workaround:
		# (1) ensure that there is no conflict;
		# (2) remove commit and its revert;
		# (3) continue current rebase
		git diff --quiet --exit-code HEAD^ \
		&& git reset --hard HEAD^ \
		&& git rebase --continue
	}
}

# rewrite git history in order to get rid of pairs of commit-with-its-revert
# argument: base_sha1 to look up from (ie. from get_kernel_base())
function remove_reverts() {
	local _BASE_SHA1="$1"

	git reset --hard HEAD # clean up working dir, as QB machines happen not to be clean

	# If there are any fixup! commits already in history, lets apply them (if possible) first.
	# Also, make history flat (no noisy merges).
	git_rebase_autosquash "${_BASE_SHA1}"

	# There is need to "apply" reverts in reverse order.
	# --no-abbrev gives full (not abbreviated) commit hashes,
	# which could be easily compared to GIT_COMMIT var during 'git filter-branch'
	# Repeating 'git log', as all commits following the ones affected by  'git filter-branch' and 'git_rebase_autosquash' change hashes in each iteration.
	local _REVERTING_COMMIT
	while _REVERTING_COMMIT=$(
		git log --oneline --no-abbrev "${_BASE_SHA1}..HEAD" \
		| grep -Po '^(.+)(?= Revert ".+"$)' | tail -1
	); do
		# Reword _REVERTING_COMMIT form '^Revert "(...)' to 'fixup! "(...)'.
		# The sed command is: match beginning of line, some non-quotes, a quote, capture some characters, a quote possibly followed by spaces till end of line. Prepend the captured  characters with 'fixup! '
		# It will allow git to "interactively" reorder this commit to be just
		# after actual commit to revert, and mark it as "fixup" instead of "pick".
		git filter-branch --force --msg-filter \
			"if [ \${GIT_COMMIT} = ${_REVERTING_COMMIT} ] ; then
				sed 's/^[^\"]*\"\(.*\)\"\s*$/fixup! \1/'
			else
				cat
			fi " \
			"${_BASE_SHA1}..HEAD"
		# Do actual removal (it needs to remove commit + its-fixup pairs)
		git_rebase_autosquash "${_BASE_SHA1}"

		local ANY_REVERT=true
	done

	# Get rid of empty commits
	[ -z "${ANY_REVERT:-}" ] || git_rebase_autosquash "${_BASE_SHA1}"
}

SCRIPT_NAME=$(basename "$0")
: "${WORKSPACE="$(realpath ../output)"}"	# quickbuild starts in ValleyVistaKernel
echo "Starting kernel build..."
[ -n "${KER_VER-}" ] || { print_usage; echo -e '\nKernel version is mandatory' >&2; exit 1; }
echo "OS: ${OS:=$(grep -o -e UBUNTU -e DEBIAN -m1 /etc/os-release || echo CENTOS)}"
echo "KER_VER: ${KER_VER}"
echo "PKG_VER: ${PKG_VER:=0.0.0}"
echo "WORKSPACE: ${WORKSPACE}"
mkdir -p "${WORKSPACE}"

if [ -z "${VCA_PATCH_DIR-}" ]; then
	git rev-parse --is-inside-work-tree &> /dev/null || die "Directory $(pwd) is not part of any git work tree!"
	KERNEL_BASE_COMMIT=$(get_kernel_base)
	[ -z "${QB_REMOVE_REVERTS:-}" ] || remove_reverts "${KERNEL_BASE_COMMIT}"
elif [ "${OS}" = CENTOS ]; then
	if [ -z "${VCA_DOT_CONFIG_FILE-}" ]; then
		die "VCA_PATCH_DIR: ${VCA_PATCH_DIR} was provided, but VCA_DOT_CONFIG_FILE was not provided."
	elif [ -e .config ]; then
		echo "$(pwd)/.config would NOT be used, because VCA_DOT_CONFIG_FILE (=${VCA_DOT_CONFIG_FILE}) was provided" >&2
	fi
fi

if ! GZIP=$(command -v pigz); then
	GZIP=gzip; echo "Continuing with single-threaded gzip, as multi-threaded pigz is not installed" >&2
fi

case "${OS}" in
	UBUNTU|DEBIAN)
		if [ -z "${VCA_PATCH_DIR-}" ]; then
			# generate tar.gz with full source
			_SRC_ARCHIVE_SUFFIX="${KER_VER}_${PKG_VER}"_src.tar.gz
			tar --exclude './.*' -cf - . .config | ${GZIP} > "${WORKSPACE}"/vca_kernel_"${_SRC_ARCHIVE_SUFFIX}" || die 'Could not make sources archive'
		else
			_PATCHES=($(find "${VCA_PATCH_DIR}" -type f -name '*.patch' | sort))
			for _PATCH in "${_PATCHES[@]}"; do
				patch -p1 < "${_PATCH}" || die "Could not apply ${_PATCH}"
			done
		fi
		# actual build
		make -j "$(nproc)" deb-pkg \
			KERNELRELEASE="${KER_VER}-1.${PKG_VER}".vca \
			KDEB_PKGVERSION=1.0 \
			PKGVERSION="${PKG_VER}" \
			OS="${OS}" \
			INSTALL_MOD_STRIP=1 \
			LOCALVERSION='' \
			|| die 'Error while executing make cmd'
		# dpkg-buildpackage in 'make deb-pkg' target puts its output files to ../ (https://bugs.debian.org/cgi-bin/bugreport.cgi?bug=657401). Move them to "${WORKSPACE}":
		find .. -mindepth 1 -maxdepth 1 ! -name ValleyVistaKernel -newer "${WORKSPACE}" -exec cp {} "${WORKSPACE}" \; -exec /bin/rm {} \;
		if [ -z "${VCA_PATCH_DIR-}" ]; then
			# generate archive with our patches for kernel
			git format-patch "${KERNEL_BASE_COMMIT}" || die 'could not format patches'
			tar -cf - ./*\.patch | ${GZIP} > "${WORKSPACE}"/vca_patches_kernel_"${_SRC_ARCHIVE_SUFFIX}" || die 'Could not make patches archive'
		fi
	;;
	CENTOS)
		case "${KER_VER}" in
			3.10.0)
				generate_centos_from_repo_and_src_rpm kernel-3.10.0-514.26.2.el7.src.rpm "${PKG_VER}"
			;;
			3.10.0-693)
				generate_centos_from_repo_and_src_rpm kernel-3.10.0-693.17.1.el7.src.rpm "${PKG_VER}.VCA"
			;;
			3.10.0-957)
				generate_centos_from_repo_and_src_rpm kernel-3.10.0-957.12.2.el7.src.rpm "${PKG_VER}.VCA"
			;;
			3.10.107)
				generate_k3_centos_from_repo
			;;
			4.*)
				generate_k4_centos
			;;
			5.*)
				generate_centos_from_repo_and_src_rpm kernel-ml-5.1.16-1.el7.elrepo.nosrc.rpm "${PKG_VER}.VCA"
			;;
			*)
				die 'Unsupported kernel version'
			;;
		esac
	;;
	*)
		print_usage
		die 'Unsupported OS type'
	;;
esac

EXIT_CODE=$?
echo "Build has ended with status: ${EXIT_CODE}"
exit ${EXIT_CODE}

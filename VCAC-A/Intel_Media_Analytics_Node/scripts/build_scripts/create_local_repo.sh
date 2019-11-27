#!/bin/bash
#
# Intel VCA Software Stack (VCASS)
#
# Copyright(c) 2015 Intel Corporation.
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

if [ "$VCA_BUILD_REPO" == "" ]; then
	VCA_BUILD_REPO="/usr/lib/vca/repos/build_repo"
fi

if [[ "$VCA_BUILD_REPO" == "file:"* ]]; then
	VCA_BUILD_REPO=${VCA_BUILD_REPO#file:}
fi

file_list_loop () {
	echo Copying $1 to $VCA_BUILD_REPO
	cp $1 $VCA_BUILD_REPO
	if [ $? != 0 ] ; then
		echo "Error when copying $1"
		exit 1
	fi

	shift
}

file_recursive_loop () {
	echo "recursive find $2/ -name *.rpm"
	for package in `find $2/ -name "*.rpm"`; do	# triling slash enables searching in symlinked directories
		echo "Found file $package"
		cp $package $VCA_BUILD_REPO
		if [ $? != 0 ] ; then
			echo "Error when copying $package"
			exit 1
		fi
	done
}

if [ $# == 0 ] || [ $1 == "-h" ] || [ $1 == "--help" ]; then
	echo "Create repository at $VCA_BUILD_REPO, containing provided rpm packages."
	echo "Repository location may be overriden by setting \$VCA_BUILD_REPO variable before calling the script."
	echo "All files are copied from the original location to the repository."
	echo "If the repository already exists, it is cleared before copying new files."
	echo -e "\nUsage:"
	echo -e "\t$0"
	echo -e "\t$0 -h"
	echo -e "\t$0 --help"
	echo -e "\t\tPrint this help screen\n"
	echo -e "\t$0 <path/package1.rpm> [path/package2.rpm [path/package3.rpm [...]]]"
	echo -e "\t\tCreate repository, containing the exact list of rpm packages\n"
	echo -e "\t$0 -r <path1> [-r <path2> [-r <path3> [...]]]"
	echo -e "\t\tCreate repository, containing all packages from the path and from child directories (recursive)"
	echo -e "\tRecursive directories and direct packages lists may be mixed in a single script run"
	exit 0
fi


rm -rf $VCA_BUILD_REPO


mkdir -p $VCA_BUILD_REPO
if [ $? != 0 ] ; then
    echo "Error when creating directory; need to run the script as root (sudo)"
    exit 1
fi


while [ "$1" != "" ]
do
	if [ $# -gt 1 ] && [ $1 == "-r" ]; then
		file_recursive_loop $@
		shift
		shift
	else
		file_list_loop $@
		shift
	fi
done


echo "Create repository at $VCA_BUILD_REPO"

createrepo $VCA_BUILD_REPO


if [ $? != 0 ] ; then
    echo "Error when creating repository"
    exit 1
fi

echo "Repository at $VCA_BUILD_REPO created"

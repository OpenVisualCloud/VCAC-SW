#!/usr/bin/awk -f

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

# Basic script for conditional text inclusion.
# Values used to manipulate text content may be passed in 2 ways:
#	a) as #define directive inside the file e.g. #define ARG1
#	b) as DEFINE command line variable with values separated by colon e.g. -v DEFINE=ARG1:ARG2:ARGN
#
# Allowed commands: #define, #ifdef, #ifndef, #endif. Nesting is allowed.
#
# Usage:
# ./ifdef.awk -v DEFINE=ARG1:ARG2:ARGN

BEGIN {
	depth = 0
	printing = 1

	ifdef_count  = 0
	endif_count  = 0

	split(DEFINE, cmd_arg_defines, ":")

	for (itr in cmd_arg_defines) {
		global_defines[cmd_arg_defines[itr]]
	}
}

$1 == "#if" || $1 == "#elif" || $1 == "#else" {
	print "Recognized an unsuporrted keyword at line " NR ": '" $0 "'. Use #define, #ifdef, #ifndef, #else or #endif keywords." > "/dev/stderr"
	exit 1
}

$1 == "#define" {

	if ($2 == "") {
		print "Recognized #define without a value at line " NR ". Exit.." > "/dev/stderr"
		exit 2
	}

	global_defines[$2]

	next
}

$1 == "#ifdef" || $1 == "#ifndef" {

	if ($2 == "") {
		print "Recognized " $1 " without a value at line " NR ". Exit.." > "/dev/stderr"
		exit 3
	}

	ifdef_count++

	if (printing) {
		if ($1 == "#ifdef") {
			printing = ($2 in global_defines) ? 1 : 0
		}
		else {
			printing = ($2 in global_defines) ? 0 : 1
		}
	}

	if (!printing) {
		depth++
	}

	next
}

$1 == "#endif" {

	endif_count++

	if (ifdef_count == 0) {
		print "Recognized #endif without an opening #ifdef or #ifndef at line " NR ". Exit.." > "/dev/stderr"
		exit 5
	}

	if (ifdef_count < endif_count) {
		print "Recognized #endif not matching to #ifdef or #ifndef at line " NR ". Exit.." > "/dev/stderr"
		exit 6
	}

	if (depth > 0) {
		depth--
	}

	if (printing == 0 && depth < 1) {
		printing = 1
	}

	next
}

printing {
	print
}

END {
	if (ifdef_count > endif_count) {
		print "Unterminated directives. Check if each #ifdef or #ifndef has its own #endif. Exit.." > "/dev/stderr"
		exit 7
	}
}
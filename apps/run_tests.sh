#!/bin/bash

# Intel VCA Software Stack (VCASS)
#
# Copyright(c) 2018 Intel Corporation.
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

LOG_FILE="shellcheck.log"
RED='\033[0;31m'
GRN='\033[0;32m' #Green
NC='\033[0m' # No Color

stderr(){
        echo "*** $*" >&2
}

die(){
        local EXIT_CODE=$(( $? == 0 ? 99 : $? ))
        stderr "ERROR: $*"
        exit ${EXIT_CODE}
}

#Find and save to array all files with .sh extention
sh_files=($(find -name "*.sh" | grep -v '\.git'))

#Check all files and save info in log file
shellcheck "${sh_files[@]}" > "$LOG_FILE"

#Calculate issues from log file
issues_cnt=$(grep -c " ^--" "$LOG_FILE" || die "Cannot grep log file")

#Print summary
echo -e "Tests finished."
if [[ "$issues_cnt" -gt "0" ]]; then
	echo -e "${RED}Found issues: $issues_cnt. Please analyze all issues from log file: $LOG_FILE${NC}"
else
	echo -e "${GRN}Found issues: $issues_cnt.${NC}"
fi


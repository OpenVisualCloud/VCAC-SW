#!/bin/bash
#
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

# Params
SHOULD_PRINT_CPU=false
SHOULD_PRINT_GPU=false
MEASUREMENT_DURATION=1 #1s

stderr(){
        echo "*** $*" >&2
}

die(){
        local EXIT_CODE=$(( $? == 0 ? 99 : $? ))
        stderr "ERROR: $*"
        exit ${EXIT_CODE}
}

function print_help {
        # Keep parameters alphabetically sorted in help and in the 'case' switch which parses them.
        # Prefer single-character options, i.e. do not introduce long options without an apparent reason.
	echo -e "Help:
        vcanodeinfo.sh command [subcommand] [options]
        Commands:
                card-serial-nr                  Read serial number
                load [Subcommand]
			cpu [-loop] [-d <duration_time_in_s>]	CPU usage
                        gpu [-loop]				GPU usage (Required metrics_monitor. If not found, it should be automatically compiled)
        Options:
        -d                              Set measurement duration for cpu usage
	-loop				Run script in loop
        "
}

function print_CPU {
	#CPU usage is calculate by (system + user)/(system + user + idle)
	cpu_usage=$(awk -v a="$(awk '/cpu /{print $2+$4,$2+$4+$5}' /proc/stat; sleep $MEASUREMENT_DURATION)" '/cpu /{split(a,b," "); printf "%.3f", 100*($2+$4-b[1])/($2+$4+$5-b[2])}' /proc/stat)
	cpu_for_each_core=$(awk '{printf "%.2f ", $1/1e6} END {print "GHz    "}' /sys/devices/system/cpu/cpu*/cpufreq/scaling_cur_freq) # Space for better output formatting
	echo -ne "\rCPU usage: $cpu_usage% $cpu_for_each_core"
}

function print_GPU {
	# for MSS PV5 there is precompiled metrics_monitor
	[[ -e /opt/intel/mediasdk/samples/_bin/x64/metrics_monitor ]] || die 'Cannot find MSS metrics_monitor tool on node.'
	cd /opt/intel/mediasdk/samples/_bin/x64/
	LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/opt/intel/mediasdk/samples/_bin/x64/
	export LD_LIBRARY_PATH
	echo -ne "\rGPU usage: $(stdbuf -o0 ./metrics_monitor | head -n1)    " # Space for better output formatting
}

function parse_parameters () {
	# Keep parameters alphabetically sorted in help and in the 'case' switch which parses them.
        [ $# == 0 ] || [ "$1" == "-h" ] || [ "$1" == "--help" ] && print_help && exit 0

        while [ "$1" != "" ]
        do
                case "$1" in
			card-serial-nr)
                                CMD_NAME="card-serial-nr"
                                shift;;
                        -d)
                                MEASUREMENT_DURATION="$2"
                                shift; shift;;
                        load)
				CMD_NAME="load"
				case "$2" in
					cpu)
						SHOULD_PRINT_CPU=true
						shift;;
					gpu)
						SHOULD_PRINT_GPU=true
						shift;;
					*)
						die "unknown parameter '$2'"
						;;
				esac
				shift;;
			-loop)
                                SHOULD_RUN_IN_LOOP=true
                                shift;;
			*)
                                die "unknown parameter '$1'"
                                ;;
                esac
        done
}

install_pkgs () {
        case "$(get_os_name)" in
                CentOS)
			yum install -y gcc-c++
                        ;;
                Ubuntu|Debian)
                        apt-get install -y g++
                        ;;
                *)
			die "unknown parameter '$OS_NAME'"
                        ;;
        esac
}

compile_metrics_monitor () {
	echo "Cannot find metrics_monitor. It will be automatically installed."
	which g++ >/dev/null 2>&1 || install_pkgs || die 'Cannot install pkgs'
	cd /opt/intel/mediasdk/tools/metrics_monitor/sample
	./build.sh || die 'Build metrics monitor failed.'
}

get_os_name () {
	head -1 /etc/os-release | awk -v FS="(NAME=\"| |\")" '{print $2}'
}

run_load_cpu_gpu () {
	while : ; do
		[[ "$SHOULD_PRINT_CPU" = true ]] && print_CPU
		[[ "$SHOULD_PRINT_GPU" = true ]] && print_GPU
		[[ "$SHOULD_RUN_IN_LOOP" = true ]] || break
	done
	echo
}

get_serial_nr () {
	die 'Not yet implemented'
}

#Main
parse_parameters "$@"
[[ "$CMD_NAME" = "load" ]] && run_load_cpu_gpu
[[ "$CMD_NAME" = "card-serial-nr" ]] && get_serial_nr

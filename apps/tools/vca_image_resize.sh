#!/bin/bash -eu
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

# on CentOS requires: gdisk, coreutils (truncate, dd,tail, head, cut, cat, ls), findutils (find), e2fsprogs-1.42.9-9.el7.x86_64 (resize2fs, filefrag, dumpe2fs/tune2fs ), kpartx (kpartx), gawk (awk), util-linux (losetup)
# on Ubuntu requires: gdisk, coreutils (truncate, dd,tail, head, cut, cat, ls), findutils (find), e2fsprogs (resize2fs, filefrag, dumpe2fs/tune2fs ), kpartx (kpartx), gawk (awk), mount (losetup)

BLOCK_DEVICE_LOCK="$(mktemp --tmpdir block_device_lock.XXX)"	#internal communication & locking for extracting partition. Partition should be extracted anew after manipulating partition table or filesystem.
DEBUG_LEVEL=1	# 0 means no debug; positive numbers increase verbosity
readonly _GDISK_DELETE_PARTITION="d"
readonly _GDISK_NEW_PARTITION="n"
readonly _GDISK_PARTITION_TYPE="8300"
readonly _GDISK_PRINT_PARTITION_TABLE="p"
readonly _GDISK_EXPERT_MODE="x"
readonly _GDISK_RELOCATE_DATA_STRUCTURES="e"
readonly _GDISK_WRITE_AND_EXIT="w"
readonly _GDISK_CONFIRM="y"
readonly _GDISK_QUIT_NO_SAVE="q"
readonly _GDISK_RETURN_MAIN_MENU="m"
readonly _GDISK_DISPLAY_BOUNDARIES="d"
SAFETY_WARNINGS=TRUE		# empty means NO safety warnings
SIZE_UNITS=G	# defaut units of size for communication with the user; can be B, K, M G
SPARSE_OUTPUT=""	# empty means NO sparse output

stderr(){
	echo "*** $*" >&2
}
warn(){
	stderr "WARNING: $*"
}
die(){
	local EXIT_CODE=$(( $? == 0 ? 99 : $? ))
	stderr "ERROR: $*"
	cleanup
	exit ${EXIT_CODE}
}
debug(){
	local _LEVEL=1
	[ $# -gt 1 ] && { _LEVEL="$1" ; shift ;}
	[ "${_LEVEL}" -le "${DEBUG_LEVEL}" ] && stderr "DEBUG[${_LEVEL}]: $*"
	return 0
}
cleanup(){
	/bin/rm "${BLOCK_DEVICE_LOCK}"
}
show_help(){
	# Keep parameters alphabetically sorted in help and in the 'case' switch which parses them.
	# Prefer single-character options, i.e. do not introduce long options without an apparent reason.
	echo "Usage: $0 <options>
Resizes a Block IO-based (a.k.a. persistent) VCA bootable image by increasing its root partition. It is assumed that the image has been created with the VCA tools.
The image is resized in-place, so user should consider making a backup copy of the image first.
The image should not be mounted, nor booted, nor used in any other way during resize.
After growing with using option -s, the resulting image is a sparse file, so it consumes less disk space than its defined new size. Writing to a sparse file usually results in gradual increase of its storage consumption up to its defined size. Also copying a sparse file with 'cp' instead of 'cp --sparse=always' produces destination file being a regular file, i.e. consuming disk space amount equal to the defined size of the source file.
This script has been tested on CentOS. For running on CentOS the following software packages are required: gdisk, gnucoreutils, findutils, e2fsprogs, kpartx, gawk, util-linux.
Options:
-d <number>		Debug level. The number must be integer. Value 0 means no debug, higher values increase debug verbosity. The default debug level is ${DEBUG_LEVEL}
-i <path>	Image path. The image will be resized in-place. The image can not be used during resize.
-n <number>	The new size of the root partition in the image. Can be an integer or a real number. The default unit is ${SIZE_UNITS}.
-s		Creaty a sparse output file. Sparse files require less space initially as they are allowed to contain holes. With writes to the hole parts of the file during its latter usage, the file gradually grows to eventually occupy its size defined with option -n.
-u <letter>	A (lowercase or uppercase) letter describing the units of the new size. Supported values are: B for bytes, K for 1024 bytes, M for 1024 Kbytes, or G for 1024 Mbytes). This parameter is optional. The default unit is ${SIZE_UNITS}.
-y		Spare the usual safety warnings about not modifying a file concurrently.
" >&2
}
# Sets variables in its dynamic scope
parse_parameters(){
	while [ $# -gt 0 ] ; do
		case "$1" in
			-d)
				DEBUG_LEVEL="${2:-${DEBUG_LEVEL}}"
				shift; shift;;
			-i)
				_DISK_IMAGE="${2:-""}"
				shift; shift;;
			-n)
				_NEW_PART_SIZE="${2:-""}"
				shift; shift;;
			-s)
				SPARSE_OUTPUT=TRUE
				shift;;
			-u)
				_SIZE_UNITS="${2:-""}"
				_SIZE_UNITS="${_SIZE_UNITS^^}"	# uppercasing parameters does not work
				shift; shift;;
			-y)
				SAFETY_WARNINGS=""
				shift;;
			*)
				show_help && die "Unknown parameter '$1'"
		esac
	done
}
# Accesses variables in its dynamic scope
check_parameters(){
	[ -z "${_DISK_IMAGE}" ] && show_help && die "Path to the VCA boot image (option '-i') missing"
	[ -n "${_DISK_IMAGE}" ] && [ ! -f "${_DISK_IMAGE}" ] && die "The VCA boot image ${_DISK_IMAGE} not found" # the correctness of the partition table is checked in subsequent steps
	[ -z "${_NEW_PART_SIZE}" ] && show_help && die "New size of the partition (option '-n') missing"
	return 0
}
# update partition table to reflect current number of disk sectors and to allocate spare GPT table at the correct location
updatePartitionTable(){
	local _DISK_IMAGE="$1"

	debug 2 "Updating partition table on ${_DISK_IMAGE}"
	# redirecting stderr, as partition table is probably not intact initially:
	local _GDISK_OUTPUT; _GDISK_OUTPUT="$(gdisk "${_DISK_IMAGE}" 2>&1 <<< "
${_GDISK_PRINT_PARTITION_TABLE}
${_GDISK_EXPERT_MODE}
${_GDISK_RELOCATE_DATA_STRUCTURES}
${_GDISK_WRITE_AND_EXIT}
${_GDISK_CONFIRM}
")"
	debug 2 "Update of partition table on ${_DISK_IMAGE} finished"
	debug 3 "During update of partition table _GDISK_OUTPUT=${_GDISK_OUTPUT}"
}

# Resizes file by appending a sparse file part
resizeFile(){
	local _FILE="$1"
	local _NEW_SIZE="$2"

	debug 2 "Starting resizing file ${_FILE} to ${_NEW_SIZE}"
	#dd if=/dev/null of="${_FILE}" bs=1 seek=`stat -c%s testfile` count="${_NEW_SIZE}"M
	#dd if=/dev/zero of="${_FILE}" bs="${_NEW_SIZE}"M seek=1 count=0 >/dev/null 2>&1
	local _OLD_SIZE_B; _OLD_SIZE_B="$(wc -c < "${_FILE}")"
	[ -n "${_OLD_SIZE_B}" ] || die "Could not get the size of file: ${_FILE}"
	truncate --size="${_NEW_SIZE}" "${_FILE}" >/dev/null 2>&1 || die "Could not resize the file ${_FILE}"
	updatePartitionTable "${_FILE}"
	local _NEW_SIZE_B; _NEW_SIZE_B="$(wc -c < "${_FILE}")"
	debug 1 "Finished resizing the disk image ${_FILE} from $(printDftlUnitAndBytes "${_OLD_SIZE_B}"). Final size is $(printDftlUnitAndBytes "${_NEW_SIZE_B}")"
}

getPartitionTableEntry(){
	local _ENTRY_NUMBER="$1"
	local _GDISK_OUTPUT="$2"

	# The list of partitions starts after this line:
	# Number  Start (sector)    End (sector)  Size       Code  Name
	# There should be exactly 3 lines in _GDISK_OUTPUT starting with the above line. Trying up to 10:
	local _PART_TABLE; _PART_TABLE="$(grep -A10 -m1 "^Number \+" <<< "${_GDISK_OUTPUT}" )"
	[ "$(wc -l <<< "${_PART_TABLE}")" -ne 3 ] && die "Could not recognize partition list: ${_GDISK_OUTPUT}"
	grep "^ \+${_ENTRY_NUMBER} \+" <<< "${_PART_TABLE}"
}

# Returns size of the given partition in bytes
getPartitionSize(){
	local _PART_NUMBER="$1"
	local _GDISK_OUTPUT="$2"

	local _UNITS; _UNITS="$(awk '/^Logical sector size: / { print $4 }' <<< "${_GDISK_OUTPUT}")"
	local _PART_ENTRY; _PART_ENTRY="$(getPartitionTableEntry "${_PART_NUMBER}" "${_GDISK_OUTPUT}" )" || die "Could not get partition table entry for partition ${_PART_NUMBER}"
	# "Number  Start (sector)    End (sector)  Size       Code  Name":
	gawk -v UNITS="${_UNITS}" '{ print ($3 - $2 + 1)*UNITS }' <<< "${_PART_ENTRY}"
}

calculatePartitionLastSector(){
	local _PARTITION_SIZE="$1"
	local _PART_NUMBER="$2"
	local _GDISK_OUTPUT="$3"

	local _UNITS; _UNITS="$(awk '/^Logical sector size: / { print $4 }' <<< "${_GDISK_OUTPUT}")"
	local _PART_ENTRY; _PART_ENTRY="$(getPartitionTableEntry "${_PART_NUMBER}" "${_GDISK_OUTPUT}" )" || die "Could not get partition table entry for partition ${_PART_NUMBER}"
	# "Number  Start (sector)    End (sector)  Size       Code  Name":
	gawk -v PARTITION_SIZE="${_PARTITION_SIZE}" -v UNITS="${_UNITS}" '{ print $2 + int(PARTITION_SIZE/UNITS) - 1 }' <<< "${_PART_ENTRY}"
}

# Resizes partition no 2 to _SIZE, or to the maximum size possible
resizePartition(){
	local _DISK_IMAGE="$1"
	local _SIZE="${2:-""}"

	local _GDISK_OUTPUT; _GDISK_OUTPUT="$(gdisk -l "${_DISK_IMAGE}" 2>/dev/null )"
	_END_SECTOR=""
	[ -n "${_SIZE}" ] && _END_SECTOR="$( calculatePartitionLastSector "${_SIZE}" 2 "${_GDISK_OUTPUT}" )" && [ -z "${_END_SECTOR}" ] && die "Could not calculate the partition last sector needed for resizing partition on ${_DISK_IMAGE} to ${_SIZE}"

	debug 2 "Starting resizing partition on ${_DISK_IMAGE}, size is ${_SIZE:-default}, end sector is ${_END_SECTOR:-default}"
	local _OLD_SIZE_B; _OLD_SIZE_B="$(getPartitionSize 2 "${_GDISK_OUTPUT}" )"
	[ -z "${_OLD_SIZE_B}" ] && die "Could not get partition size. Is ${_DISK_IMAGE} correct?"

	local _GDISK_OUTPUT; _GDISK_OUTPUT="$(gdisk "${_DISK_IMAGE}" <<< "
${_GDISK_DELETE_PARTITION}
2
${_GDISK_NEW_PARTITION}
2

${_END_SECTOR}
${_GDISK_PARTITION_TYPE}
${_GDISK_WRITE_AND_EXIT}
${_GDISK_CONFIRM}
" )"
	debug 4 "During partition resize, _GDISK_OUTPUT=${_GDISK_OUTPUT}"
	local _NEW_SIZE_B; _NEW_SIZE_B="$(getPartitionSize 2 "$(gdisk -l "${_DISK_IMAGE}" 2>/dev/null)" )"

	debug 1 "Finished resizing the root partition on disk image ${_DISK_IMAGE} from $(printDftlUnitAndBytes "${_OLD_SIZE_B}"). Final size is $(printDftlUnitAndBytes "${_NEW_SIZE_B}")"
}

createPartitionDevice(){
	local _DISK_IMAGE="$1"

	[ -s "${BLOCK_DEVICE_LOCK}" ] && die "Previous block device $(cat "${BLOCK_DEVICE_LOCK}") not yet removed (should never happen)"
	local _BLOCK_DEVICE; _BLOCK_DEVICE="$(sudo losetup --find --show --partscan "${_DISK_IMAGE}")" || die "Could not set up a loop device for ${_DISK_IMAGE}"
	echo "${_BLOCK_DEVICE}" > "${BLOCK_DEVICE_LOCK}"
	local _PARTITION_DEVICE="${_BLOCK_DEVICE}"p2
	[ ! -e "${_PARTITION_DEVICE}" ] && warn "Partition table on ${_DISK_IMAGE} seems flawed impeding creation of temporary loop device... Trying harder" && { sudo kpartx -as "${_BLOCK_DEVICE}" || die "Could not create temporary loop partition" ; } # after resizing partitions, kernel seems to neglect all partitions at all; this forces re-read

	# The below failure means loopback device for the partition has not been created correctly. So the only thing to do is to try to remove the loopback device for the disk image, if exists.
	[ ! -e "${_PARTITION_DEVICE}" ] &&  { sudo losetup -d "${_BLOCK_DEVICE}" 2>/dev/null || true; die "Partition ${_PARTITION_DEVICE} not visible over loopback device" ; }
	echo "${_PARTITION_DEVICE}"
}

removePartitionDevice(){
	local _PARTITION_DEVICE="$1"

	local _BLOCK_DEVICE; _BLOCK_DEVICE="$( cat "${BLOCK_DEVICE_LOCK}" )"
	[ -n "${_BLOCK_DEVICE}" ] || die "No block device created yet (should never happen)"
	sudo losetup -d "${_BLOCK_DEVICE}" || true
	[ -e "${_PARTITION_DEVICE}" ] && warn "Could not remove temporary loop device. Trying harder..." && { sudo kpartx -ds "${_BLOCK_DEVICE}" && warn "Removing temporary loop device succeeded, but required addidional step" || die "Could not remove temporary loop device ${_PARTITION_DEVICE}" ; }	# losetup does not remove devices created with kpartx
	cat /dev/null > "${BLOCK_DEVICE_LOCK}"
}

# Converts _VALUE in _FROM_UNITS into Bytes
# _VALUE can be non-integral; output is always integral
toBytes(){
	local _VALUE="$1"
	local _FROM_UNITS="$2"

	# check if ${_VALUE} is an int/float number
	[[  -n "${_VALUE}" && ${_VALUE} =~ ^[0-9]*[.]?[0-9]*$ ]] || die "The value must be a non-negative number: ${_VALUE}"

	local _FACTOR
	case "${_FROM_UNITS}" in
		B)
			_FACTOR=""
		;;
		K)
			_FACTOR="*1024"
		;;
		M)
			_FACTOR="*1024*1024"
		;;
		G)
			_FACTOR="*1024*1024*1024"
		;;
		*)
			die "Unsuported unit: ${_FROM_UNITS}"
	esac
	awk -v VAL="${_VALUE}" "BEGIN { printf(\"%d\", VAL ${_FACTOR}) }"
}

# Converts _VALUE in Bytes into _TO_UNITS
# _VALUE must be integral; output can be integral or real
fromBytes(){
	local _VALUE="$1"
	local _TO_UNITS="$2"

	# check if ${_VALUE} is an int number
	[[ -n "${_VALUE}" && ${_VALUE} =~ ^[0-9]*$ ]] || die "The value must be a non-negative integer: ${_VALUE}"

	local _FACTOR
	case "${_TO_UNITS}" in
		B)
			_FACTOR=""
		;;
		K)
			_FACTOR="/1024"
		;;
		M)
			_FACTOR="/1024/1024"
		;;
		G)
			_FACTOR="/1024/1024/1024"
		;;
		*)
			die "Unsuported unit: ${_TO_UNITS}"
	esac
	awk -v VAL="${_VALUE}" "BEGIN { printf(\"%f\", VAL ${_FACTOR}) }"
}

# Convert from _FROM_UNITS to default units defined in SIZE_UNITS
toDfltUnits(){
	local _VALUE="$1"
	local _FROM_UNITS="${2:-B}"

	local _BYTES; _BYTES="$( toBytes "${_VALUE}" "${_FROM_UNITS}" )"
	fromBytes "${_BYTES}" "${SIZE_UNITS}"
}

printDftlUnitAndBytes(){
	local _VALUE="$1"

	local _IN_DEFAULT_UNITS; _IN_DEFAULT_UNITS="$( toDfltUnits "${_VALUE}" )"
	printf "%.2f %s (=%s bytes)" "${_IN_DEFAULT_UNITS}" "${SIZE_UNITS}" "${_VALUE}"
}

getFileSystemMaxSize(){
	local _PARTITION_DEVICE="$1"

	local _DUMPE2FS_OUTPUT; _DUMPE2FS_OUTPUT="$(sudo dumpe2fs -h "${_PARTITION_DEVICE}" 2>/dev/null)"
	[ -z "${_DUMPE2FS_OUTPUT}" ] && die "Could not get information on filesystem on ${_PARTITION_DEVICE}"

	gawk -F: '
		/^Block count:/	{ BC = $2 }
		/^First block:/	{ FB = $2 }
		/^Block size:/	{ BS = $2 }
		END { print (BC + FB) * BS }' <<< "${_DUMPE2FS_OUTPUT}"
}

getFileSystemCurrSize(){
	local _DISK_IMAGE="$1"

	local _PARTITION_DEVICE; _PARTITION_DEVICE="$(createPartitionDevice "${_DISK_IMAGE}")" || die ""
	local _DUMPE2FS_OUTPUT; _DUMPE2FS_OUTPUT="$(sudo dumpe2fs -h "${_PARTITION_DEVICE}" 2>/dev/null)"
	[ -z "${_DUMPE2FS_OUTPUT}" ] && die "Could not get information on filesystem on ${_PARTITION_DEVICE}"
	removePartitionDevice "${_PARTITION_DEVICE}"

	gawk -F: '
		/^Free blocks:/	{ FR = $2 }
		/^Block count:/	{ BC = $2 }
		/^First block:/	{ FB = $2 }
		/^Block size:/	{ BS = $2 }
		END { print (BC - FR + FB) * BS } ' <<< "${_DUMPE2FS_OUTPUT}"
}

isDiskFull(){
	local _FILE_PATH="$1"

	local _FILE_DIR; _FILE_DIR="$( dirname "${_FILE_PATH}" )"
	local _TEST="$( mktemp --tmpdir="${_FILE_DIR}" 2>/dev/null)" # assignement with declaration to mask return value
	[ -f "${_TEST}" ] && rm "${_TEST}" && return 1
	return 0	# disk full
}

# Fills holes in _SPARSE_FILE with zeros
fillHoles(){
	local _SPARSE_FILE="$1"

	local _FILEFRAG_OUTPUT; _FILEFRAG_OUTPUT="$( filefrag -e "${_SPARSE_FILE}" )"
	# expecting lines:" 0:        0.. 1048575:  187357696.. 188406271: 1048576:"
	local _NUM_OF_HOLES; _NUM_OF_HOLES="$( grep -c "^ *[0-9][0-9]*:" <<< "${_FILEFRAG_OUTPUT}" )"
	_NUM_OF_HOLES=$(( _NUM_OF_HOLES - 1 ))
	local _HOLE_FIRST_BLOCK _HOLE_LAST_BLOCK
	local -i _COUNT=0
	local -i ERROR_CODE=0
	debug 4  "_FILEFRAG_OUTPUT=${_FILEFRAG_OUTPUT}"
	# expecting extent line format: "ext:     logical_offset:        physical_offset: length:   expected: flags:"
	# expecting extent lines ('expected:' column may be empty as here; perhaps also 'flags:'):"    0:        0..       4:    5342746..   5342750:      5:             merged"
	# excluding last line: "{_SPARSE_FILE}: 1 extent(...)"
	local _HOLE_BLOCKS_LIST; _HOLE_BLOCKS_LIST="$( awk -F':|\\.\\.' '
		BEGIN { holeBeginBlock = -1 }
		NF>5 && $1 ~ /^ *[0-9]+$/ { if( holeBeginBlock != -1 ) { print $2 - 1; print holeBeginBlock } ; holeBeginBlock = $3 + 1 }
		' <<< "${_FILEFRAG_OUTPUT}" )"
	while [ "${ERROR_CODE}" -eq 0 ] && read _HOLE_LAST_BLOCK; do
		read _HOLE_FIRST_BLOCK
		_COUNT=$(( _COUNT + 1))
		local _BLOCK_COUNT=$(( _HOLE_LAST_BLOCK - _HOLE_FIRST_BLOCK + 1 ))

		if [ "${_BLOCK_COUNT}" -gt 0 ] ; then
			debug 4 "Removing hole: _COUNT=${_COUNT}/${_NUM_OF_HOLES} _HOLE_FIRST_BLOCK=${_HOLE_FIRST_BLOCK} _HOLE_LAST_BLOCK=${_HOLE_LAST_BLOCK}, _BLOCK_COUNT=${_BLOCK_COUNT}"
			dd if="${_SPARSE_FILE}" of="${_SPARSE_FILE}" conv=notrunc bs=4k seek="${_HOLE_FIRST_BLOCK}" skip="${_HOLE_FIRST_BLOCK}" count="${_BLOCK_COUNT}" 2>/dev/null || ERROR_CODE=$(( ERROR_CODE + $?))
		else
			debug 5 "Skipping hole of length ${_BLOCK_COUNT}: _COUNT=${_COUNT} _HOLE_FIRST_BLOCK=${_HOLE_FIRST_BLOCK} _HOLE_LAST_BLOCK=${_HOLE_LAST_BLOCK}"
		fi
		# only when stdout is a terminal:
		if [ "${ERROR_CODE}" -eq 0 ] ; then
			[ 1 -le "${DEBUG_LEVEL}" ] && [ -t 1 ] && awk "BEGIN { printf(\"\r%.2f%% of %d holes removed\", 100.0*${_COUNT}/${_NUM_OF_HOLES}, ${_NUM_OF_HOLES} ) }" >&2
		else
			warn "Error ${ERROR_CODE} when removing hole ${_COUNT}/${_NUM_OF_HOLES} in ${_SPARSE_FILE}, start sector: ${_HOLE_FIRST_BLOCK}, hole blocks: ${_BLOCK_COUNT} (disk full?)"
		fi
	done <<< "${_HOLE_BLOCKS_LIST}"
	[ 1 -le "${DEBUG_LEVEL}" ] && printf "\n" >&2
	[ "${ERROR_CODE}" -ne 0 ] && die "Could not remove holes in sparse file ${_SPARSE_FILE} (disk full?)"
}
resizeFileSystem(){
	local _DISK_IMAGE="$1"
	local _SIZE="${2:-""}"

	local _PARTITION_DEVICE; _PARTITION_DEVICE="$( createPartitionDevice "${_DISK_IMAGE}")" || die "Error"
	local _ERROR_CODE=0
	sudo fsck -y -f "${_PARTITION_DEVICE}" >/dev/null 2>&1 || _ERROR_CODE=$?
	[ ${_ERROR_CODE} -gt 1 ] && die "Could not fsck the file system before resizing (error code ${_ERROR_CODE}). Try fsck -y ${_PARTITION_DEVICE}"

	debug 2 "_PARTITION_DEVICE=${_PARTITION_DEVICE}"
	debug 2 "_SIZE=${_SIZE}"
	local _OLD_SIZE_B; _OLD_SIZE_B="$( getFileSystemMaxSize "${_PARTITION_DEVICE}" )" || die "Could not get the previous filesystem size on ${_PARTITION_DEVICE}"
	local _RESIZE2FS_OPTS=""
	[ -t 1 ] && [ 1 -le "${DEBUG_LEVEL}" ] && _RESIZE2FS_OPTS="-p"	# only when stdout is a terminal
	[ -n "${_SIZE}" ] && _SIZE="$(( _SIZE / 512 ))s"	# resize2fs assumes 512-byte sectors
	sudo resize2fs ${_RESIZE2FS_OPTS} "${_PARTITION_DEVICE}" ${_SIZE} || die "Resizing root file system in disk image ${_DISK_IMAGE} failed"
	isDiskFull "${_DISK_IMAGE}" && die "Could not resize file ${_DISK_IMAGE}. Disk full"
	[ -z "${SPARSE_OUTPUT}" ] && [ -z "${_SIZE}" ] && stderr "Converting file ${_DISK_IMAGE} which is currently sparse and has the size of $(du -hs "${_DISK_IMAGE}" | cut -f1 ) to regular image file of requested size. This disk-intensive operation may take a long time to complete..." && fillHoles "${_DISK_IMAGE}"
	local _NEW_SIZE_B; _NEW_SIZE_B="$( getFileSystemMaxSize "${_PARTITION_DEVICE}" )" || die "Could not get the updated filesystem size on ${_PARTITION_DEVICE}"

	removePartitionDevice "${_PARTITION_DEVICE}"

	debug 1 "Finished resizing the root file system from $(printDftlUnitAndBytes "${_OLD_SIZE_B}"). Final size is $(printDftlUnitAndBytes "${_NEW_SIZE_B}")"
}

verifyFileSystem(){
	local _DISK_IMAGE="$1"

	stderr "Verifying filesystem on ${_DISK_IMAGE} after resize"
	local _PARTITION_DEVICE; _PARTITION_DEVICE="$( createPartitionDevice "${_DISK_IMAGE}")" || die "Error"

	sudo fsck -y -f "${_PARTITION_DEVICE}" >/dev/null 2>&1 || die "Could not fsck the file system after resizing"
	sudo dumpe2fs "${_PARTITION_DEVICE}" >/dev/null 2>&1 || die "Could not verify the file system with dumpe2fs after resizing"
	local _MNT_POINT; _MNT_POINT="$(mktemp --directory --tmpdir)"
	sudo mount "${_PARTITION_DEVICE}" "${_MNT_POINT}"  || die "Could not test mount the file system after resizing"
	sudo umount "${_PARTITION_DEVICE}" || true

	# cleanup after verification:
	rmdir "${_MNT_POINT}"
	removePartitionDevice "${_PARTITION_DEVICE}"
	stderr "File system verified succesfully on ${_DISK_IMAGE} after resize"
}

getGdiskPartitionSectorAlignmentOutput(){
	local _DISK_IMAGE="$1"

	gdisk "${_DISK_IMAGE}" <<<"
${_GDISK_EXPERT_MODE}
${_GDISK_DISPLAY_BOUNDARIES}
${_GDISK_RETURN_MAIN_MENU}
${_GDISK_QUIT_NO_SAVE}
" 2>/dev/null
}

calculatePartitionSectors(){
	local _NEW_SIZE="$1"
	local _GDISK_OUTPUT="$2"
	local _GDISK_EXPERT_OUTPUT="$3"

	local _SECTOR_BYTES; _SECTOR_BYTES="$( awk '/^Logical sector size:/ { print $4 }' <<< "${_GDISK_OUTPUT}" )"
	# expecting line like: "Expert command (? for help): Partitions will begin on 2048-sector boundaries."
	local _ALIGNMENT_SECTORS; _ALIGNMENT_SECTORS="$( awk -F'[ -]' '/Partitions will begin on / { print $10 }' <<< "${_GDISK_EXPERT_OUTPUT}" )"
	echo "$(( ( (_NEW_SIZE / _SECTOR_BYTES -1)/_ALIGNMENT_SECTORS + 1 ) * _ALIGNMENT_SECTORS ))"
}

calculateDiskSize(){
	local _NEW_PARTITION_SECTORS="$1"
	local _GDISK_OUTPUT="$2"

	# "Number  Start (sector)    End (sector)  Size       Code  Name":
	local _BEGINNING_RESERVED_SECTORS; _BEGINNING_RESERVED_SECTORS="$( awk '/ + 1 +/ { print $2 }' <<< "${_GDISK_OUTPUT}" )"
	# "Disk /tmp/2.vcad: 430080 sectors, 210.0 MiB"; only match the first line:
	local _DISK_LAST_SECTOR; _DISK_LAST_SECTOR="$( awk '/^Disk / { print $3 ; exit}' <<< "${_GDISK_OUTPUT}" )"
	# "First usable sector is 34, last usable sector is 430046":
	local _GPT_SECONDARY_HEADER_SECTORS; _GPT_SECONDARY_HEADER_SECTORS="$( awk "/, last usable sector is / { print ${_DISK_LAST_SECTOR} - \$10  }" <<< "${_GDISK_OUTPUT}" )"
	# gdisk seems to like creating big GPT partirions when number of entries is not limited (limiting posssible in expert menu, option s)
	_GPT_SECONDARY_HEADER_SECTORS=$(( _GPT_SECONDARY_HEADER_SECTORS < 2048 ? 2048 : _GPT_SECONDARY_HEADER_SECTORS))
	local _SECTOR_BYTES; _SECTOR_BYTES="$( awk '/^Logical sector size:/ { print $4 }' <<< "${_GDISK_OUTPUT}" )"

	local _BOOT_PART_SIZE; _BOOT_PART_SIZE="$( getPartitionSize 1 "${_GDISK_OUTPUT}" )" || die "Could not get partition size from ${_GDISK_OUTPUT}"

	debug 4 "Calculating new disk size as (${_BEGINNING_RESERVED_SECTORS} + ${_NEW_PARTITION_SECTORS} + ${_GPT_SECONDARY_HEADER_SECTORS} ) * ${_SECTOR_BYTES} + ${_BOOT_PART_SIZE}"
	echo "$(( (_BEGINNING_RESERVED_SECTORS + _NEW_PARTITION_SECTORS + _GPT_SECONDARY_HEADER_SECTORS ) * _SECTOR_BYTES + _BOOT_PART_SIZE  ))"
}

# Grows image in steps, with error checking
growImage(){
	local _DISK_IMAGE="$1"
	local _NEW_PART_SIZE="$2"
	local _NEW_DISK_SIZE="$3"

	resizeFile "${_DISK_IMAGE}" "${_NEW_DISK_SIZE}"
	resizePartition "${_DISK_IMAGE}"	# will resize to the default
	resizeFileSystem "${_DISK_IMAGE}"
	verifyFileSystem "${_DISK_IMAGE}"
}

# Shrink  image in steps, with error checking
shrinkImage(){
	local _DISK_IMAGE="$1"
	local _NEW_PART_SIZE="$2"
	local _NEW_DISK_SIZE="$3"

	resizeFileSystem "${_DISK_IMAGE}" "${_NEW_PART_SIZE}"
	resizePartition "${_DISK_IMAGE}" "${_NEW_PART_SIZE}"
	resizeFile "${_DISK_IMAGE}" "${_NEW_DISK_SIZE}" # this also makes GPT intact with file size
	verifyFileSystem "${_DISK_IMAGE}"
}
show_disclaimer(){

	if [ -n "${SAFETY_WARNINGS}" ] ; then
		stderr "The image is resized in-place, so user should consider making a backup copy of the image first.
The image should not be mounted, nor booted, nor used in any other way during resize as concurrent modifications may have unpredictable effects both on such current use and on the correctness of the final image after resize.
(Use command line option '-y' to avoid this check and to allow the program to continue automatically)"
		local _ANSW="no"
		while [ "${_ANSW}" != yes ] ; do
			stderr "Answer 'yes' below to continue now, or Ctrl-C to bail out:"
			read -r _ANSW
			_ANSW="${_ANSW,,}"	# lowercase
		done
	fi
}
# _NEW_PART_SIZE sets _PARTITION_SIZE = _NEW_PART_SIZE
# +/-_NEW_PART_SIZE increases/decreases _PARTITION_SIZE = _PARTITION_SIZE+/-_NEW_PART_SIZE
# min+_NEW_PART_SIZE sets _PARTITION_SIZE = _CURR_FILESYTEM_SIZE+_NEW_PART_SIZE
# all the above if resulting partition >_CURR_FILESYTEM_SIZE
# _NEW_PART_SIZE must be non-negative integer/real number
# Final size may differ by +-2048 sectors (+-1MB) due to partition alignment in the image
resizeImage(){
	local _DISK_IMAGE=""
	local _NEW_PART_SIZE=""
	local _SIZE_UNITS=""
	parse_parameters "$@"
	check_parameters

	show_disclaimer

	SIZE_UNITS="${_SIZE_UNITS:-${SIZE_UNITS}}"	# store in global
	local _GDISK_OUTPUT; _GDISK_OUTPUT="$( gdisk -l "${_DISK_IMAGE}" 2>/dev/null )" || die "Could not get information from disk ${_DISK_IMAGE}"
	local _CURR_PARTITION_SIZE; _CURR_PARTITION_SIZE="$( getPartitionSize 2 "${_GDISK_OUTPUT}" )" || die "Could not get current partition size on ${_DISK_IMAGE}"
	debug 1 "Current partition size (bytes): ${_CURR_PARTITION_SIZE}"
	local _CURR_FILESYTEM_SIZE; _CURR_FILESYTEM_SIZE="$(getFileSystemCurrSize "${_DISK_IMAGE}")" || die "Error"
	debug 1 "Current filesystem usage (bytes): ${_CURR_FILESYTEM_SIZE}"

	case "${_NEW_PART_SIZE}" in
		+*)
			# strip off the first char of _NEW_PART_SIZE
			_NEW_PART_SIZE="$( toBytes "${_NEW_PART_SIZE#?}" "${SIZE_UNITS}" )"; [ -z "${_NEW_PART_SIZE}" ] && die ""
			_NEW_PART_SIZE=$(( _CURR_PARTITION_SIZE + _NEW_PART_SIZE ))	#
		;;
		-*)
			# strip off the first char of _NEW_PART_SIZE
			_NEW_PART_SIZE="$( toBytes "${_NEW_PART_SIZE#?}" "${SIZE_UNITS}" )"; [ -z "${_NEW_PART_SIZE}" ] && die ""
			_NEW_PART_SIZE=$(( _CURR_PARTITION_SIZE - _NEW_PART_SIZE ))
		;;
		min+*)
			# strip off the leading substring in _NEW_PART_SIZE
			_NEW_PART_SIZE="$( toBytes "${_NEW_PART_SIZE#min+}" "${SIZE_UNITS}" )"; [ -z "${_NEW_PART_SIZE}" ] && die ""
			_NEW_PART_SIZE=$(( _CURR_FILESYTEM_SIZE + _NEW_PART_SIZE ))
		;;
		*)
			_NEW_PART_SIZE="$( toBytes "${_NEW_PART_SIZE}" "${SIZE_UNITS}" )"; [ -z "${_NEW_PART_SIZE}" ] && die ""
			_NEW_PART_SIZE=$(( _NEW_PART_SIZE ))
		;;
	esac
	debug 1 "Requested partition size (bytes): ${_NEW_PART_SIZE}"
	# is there enough disk space at this moment?
	local _FREE_DISK_SPACE; _FREE_DISK_SPACE="$(df -k "${_DISK_IMAGE}" | gawk 'NR==2 { print $2*1024 }')"
	if [ "${_FREE_DISK_SPACE}" -le "${_NEW_PART_SIZE}" ] ; then
		local _MSG; _MSG="Disk space low. $(printDftlUnitAndBytes "${_FREE_DISK_SPACE}") currently available, $(printDftlUnitAndBytes "${_NEW_PART_SIZE}") image size requested."
		[ -n "${SPARSE_OUTPUT}" ] && warn "${_MSG} Continuing anyway with sparse output file..." || fail "${_MSG} Aborting because non-sparse output file has been requested!"
	fi
	[ "${_NEW_PART_SIZE}" -lt "${_CURR_FILESYTEM_SIZE}" ] && die "Requested size $(printDftlUnitAndBytes "${_NEW_PART_SIZE}") is too small. The current data in filesystem needs at least $(printDftlUnitAndBytes "${_CURR_FILESYTEM_SIZE}")"

	stderr "Requested to change root partition size from current $(printDftlUnitAndBytes "${_CURR_PARTITION_SIZE}") to $(printDftlUnitAndBytes "${_NEW_PART_SIZE}") on VCA disk image ${_DISK_IMAGE}"

	local _GDISK_EXPERT_OUTPUT; _GDISK_EXPERT_OUTPUT="$( getGdiskPartitionSectorAlignmentOutput "${_DISK_IMAGE}" )" || die "Could not get expert information from disk ${_DISK_IMAGE}"

	debug 2 "gdisk output before resizing=${_GDISK_OUTPUT}"
	local _NEW_PARTITION_SECTORS; _NEW_PARTITION_SECTORS="$( calculatePartitionSectors "${_NEW_PART_SIZE}" "${_GDISK_OUTPUT}" "${_GDISK_EXPERT_OUTPUT}" )" || die "Could not calculate partition sectors for disk ${_DISK_IMAGE}"
	debug 1 "Calculated new partition size (sectors): ${_NEW_PARTITION_SECTORS}"
	local _NEW_DISK_SIZE; _NEW_DISK_SIZE="$( calculateDiskSize "${_NEW_PARTITION_SECTORS}" "${_GDISK_OUTPUT}" )" || die "Could not calculate disk size for disk ${_DISK_IMAGE}"
	debug 1 "Calculated new disk size (bytes): ${_NEW_DISK_SIZE}"

	[ "${_NEW_PART_SIZE}" -gt "${_CURR_PARTITION_SIZE}" ] && debug 1 "Growing the image ${_DISK_IMAGE}" && growImage "${_DISK_IMAGE}" "${_NEW_PART_SIZE}" "${_NEW_DISK_SIZE}"
	[ "${_NEW_PART_SIZE}" -lt "${_CURR_PARTITION_SIZE}" ] && debug 1 "Shrinking the image ${_DISK_IMAGE}" && shrinkImage "${_DISK_IMAGE}" "${_NEW_PART_SIZE}" "${_NEW_DISK_SIZE}"
	[ "${_NEW_PART_SIZE}" -eq "${_CURR_PARTITION_SIZE}" ] && warn "New size and old size ( $(printDftlUnitAndBytes "${_CURR_PARTITION_SIZE}") are equal. Skipping resize without any modifications to the disk image ${_DISK_IMAGE}."
	debug 2 "final gdisk output =$( gdisk -l "${_DISK_IMAGE}" )"

	cleanup
}

debug 2 "Called as: $0 $*"
resizeImage "$@" && debug 2 "Finished: $0 $*"

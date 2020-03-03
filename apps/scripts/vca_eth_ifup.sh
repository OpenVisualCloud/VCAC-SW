#!/bin/bash
set -e
trap 'e=$? ; echo "ERROR $e $BASH_COMMAND" >&2 ; exit $e' err
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
exec 2> >( while read L ; do echo -e "$(date)\t${1##*/}\t${L}" ; done >>/var/log/vca/vca_ifup.log )


log(){
	$ECHO "`$DATE`$TAG: $1" >> /var/log/vca/vca_ifup.log
	$ECHO "$1"
}

config_path(){
	ECHO=/usr/bin/echo
	[ -f $ECHO ] || ECHO=/bin/echo
	[ -f $ECHO ] || exit 2

	DATE=/usr/bin/date
	[ -f $DATE ] || DATE=/bin/date
	[ -f $DATE ] || {
		DATE=""
		log "Not found date"
		exit 2
	}

	CAT=/usr/bin/cat
	[ -f $CAT ] || CAT=/bin/cat
	[ -f $CAT ] || {
		log "Not found cat"
		exit 2
	}

	VCACTRL=/usr/sbin/vcactl
	[ -f $VCACTRL ] || {
		log "Not found vcactl"
		exit 2
	}

	EGREP=/usr/bin/egrep
	[ -f $EGREP ] || EGREP=/bin/egrep
	[ -f $EGREP ] || {
		log "Not found egrep"
		exit 2
	}

	CUT=/usr/bin/cut
	[ -f $CUT ] || CUT=/bin/cut
	[ -f $CUT ] || {
		log "Not found cut"
		exit 2
	}

	IP=/usr/sbin/ip
	[ -f $IP ] || IP=/sbin/ip
	[ -f $IP ] || {
		log "Not found ip"
		exit 2
	}

	HEAD=/usr/bin/head
	[ -f $HEAD ] || {
		log "Not found head"
		exit 2
	}

	IPCALC=/usr/bin/ipcalc
	[ -f $IPCALC ] || IPCALC=/bin/ipcalc
	[ -f $IPCALC ] || {
		log "Not found ipcalc"
		exit 2
	}

	BRCTL=/usr/sbin/bridge
	[ -f $BRCTL ] || BRCTL=/sbin/bridge
	[ -f $BRCTL ] || {
		log "Not found brctl"
		exit 2
	}

	XARGS=/usr/bin/xargs
	[ -f $XARGS ] || XARGS=/bin/xargs
	[ -f $XARGS ] || {
		log "Not found xargs"
		exit 2
	}
}

ifup(){
	log "$IP addr add $HOST_IP/$HOST_MASK dev $CARD_DEV"
	$IP addr add $HOST_IP/$HOST_MASK dev $CARD_DEV
	log "$IP link set dev $CARD_DEV mtu 65535"
	$IP link set dev $CARD_DEV mtu 65535
	log "$IP link set dev $CARD_DEV up"
	$IP link set dev $CARD_DEV up
}


configure_routing(){
	CARD_IP=`$VCACTRL config-show $CARD_ID $CPU_ID | $EGREP '\sip' |$CUT -f4 -d" "`
	if [ -z $CARD_IP ] ; then
		log "Cannot determine card's IP"
		exit 1
	fi

	log "$IP route add $CARD_IP dev $CARD_DEV"
	$IP route add $CARD_IP dev $CARD_DEV

	CARD_MASK=`$VCACTRL config-show $CARD_ID $CPU_ID | $EGREP '\shost-mask' |$CUT -f4 -d" "`
	CARD_NET=`$IPCALC -n $CARD_IP/$CARD_MASK |$CUT -f2 -d'='`
	log "Card net: $CARD_NET/$CARD_MASK"


	if [ -n $CARD_NET ] ; then
		log "$IP route add $CARD_NET/$CARD_MASK via $CARD_IP"
		$IP route add $CARD_NET/$CARD_MASK via $CARD_IP
	fi

	#enable ARP proxy
	echo 1 > /proc/sys/net/ipv4/conf/$CARD_DEV/proxy_arp
}


get_card_dev(){
	MACADDR="fe:00:00:00:0$(( $CARD_ID + 1)):0$(( $CPU_ID + 1))"
	CARD_DEV=`$IP addr |$EGREP $MACADDR -B 1|$HEAD -n 1| $CUT -f2 -d' '| $CUT -f1 -d':'`
}

preconfig(){
	HOST_IP=`$VCACTRL config-show $CARD_ID $CPU_ID | $EGREP '\shost-ip' |$CUT -f4 -d" "`
	if [ -z $HOST_IP ] ; then
		log "Cannot determine host IP"
		exit 1
	fi

	HOST_MASK=`$VCACTRL config-show $CARD_ID $CPU_ID | $EGREP '\shost-mask' |$CUT -f4 -d" "`
	if [ -z $HOST_MASK ] ; then
		log "Cannot determine host mask"
		exit 1
	fi

	get_card_dev
	if [ -z $CARD_DEV ] ; then
		log "Cannot determine card's dev"
		exit 1
	fi

}

check_bridging(){
	BRIDGE_DEV=`$VCACTRL config-show $CARD_ID $CPU_ID | $EGREP '\sbridge-interface' | $CUT -f4 -d " "`
	if [ -z $BRIDGE_DEV ] ; then
		log "Bridging not enabled"
		return 1
	fi

	BRIDGE_STATUS=`$BRCTL link show $BRIDGE_DEV |$EGREP $BRIDGE_DEV | $XARGS |$CUT -f2 -d" "`
	if [ "$BRIDGE_STAUS" == "$BRIDGE_DEV" ]; then
		log "Cannot find bridge $BRIDGE_DEV"
		exit 1
	fi

	return 0
}

add_to_bridge(){
	get_card_dev
	if [ -z $CARD_DEV ] ; then
		log "Cannot determine card's dev"
		exit 1
	fi

	log "$IP link set dev $CARD_DEV mtu 65535"
	$IP link set dev $CARD_DEV mtu 65535
	log "$IP link set dev $CARD_DEV up"
	$IP link set dev $CARD_DEV up

	log "Adding $CARD_DEV device to bridge $BRIDGE_DEV"
	$BRCTL add $BRIDGE_DEV $CARD_DEV
}

PARAM1=$1
PARAM2=$2
TAG=""

config_path

if [ -z $PARAM1 ] ;then
	log "No parameters given"
	exit 1
fi

if [ ! -z $PARAM2 ] ; then
	CARD_ID=$PARAM1
	CPU_ID=$PARAM2
	TAG=" [$CARD_ID:$CPU_ID]"
else
	TAG=" [$PARAM1]"
	FILE_PATH="/sys$PARAM1/address"
	log "Path to device: $FILE_PATH"

	if [ -f $FILE_PATH ] ;then
		MAC="`$CAT $FILE_PATH`"
		log "MAC: $MAC"
		if [ -z `$ECHO $MAC | grep fe:00:00:00:` ] ;then
			log "incompatible address format (MAC)"
			exit 3
		fi
		CARD_ID=`$ECHO $MAC | $CUT -c13-14`
		CPU_ID=`$ECHO $MAC | $CUT -c16-17`
		CARD_ID=`printf '%d' 0x$CARD_ID`
		CPU_ID=`printf '%d' 0x$CPU_ID`
		CARD_ID=$(($CARD_ID-1))
		CPU_ID=$(($CPU_ID-1))
		TAG=" [$CARD_ID:$CPU_ID]"
	else
		log "Path to device not exist"
		exit 1
	fi
fi

log "CARD_ID=$CARD_ID CPU_ID=$CPU_ID"

if check_bridging ; then
	log "Need to do bridging"
	add_to_bridge
else
	log "Standard routing config"
	preconfig
	ifup
	configure_routing
fi


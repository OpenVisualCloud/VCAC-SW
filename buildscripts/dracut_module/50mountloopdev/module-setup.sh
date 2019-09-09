#!/bin/bash

check() {
	return 0
}

install() {
	inst_hook cmdline 20 "$moddir/mount_loop_device.sh"
}

depends () {
	return 0
}

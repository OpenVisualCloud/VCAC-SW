#!/bin/bash

modprobe loop
losetup /dev/loop0 /root/root_partition.img

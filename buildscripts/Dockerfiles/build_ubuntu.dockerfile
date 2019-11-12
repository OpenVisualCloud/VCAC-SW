# can be overwritten via '--build-arg' to docker (via master-build.sh)
ARG BASE_IMG_VER=16.04

FROM ubuntu:${BASE_IMG_VER}

RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y \
	bc \
	bison \
	build-essential \
	checkinstall \
	cmake \
	cpio \
	debootstrap \
	dosfstools \
	dracut \
	flex \
	gcc \
	kmod \
	keyutils \
	libelf-dev \
	libssl-dev \
	lsb-release \
	openssl \
	pigz \
	make \
	sudo \
	udev

COPY build.sh library_fs.sh /root/
ENTRYPOINT ["/root/build.sh"]

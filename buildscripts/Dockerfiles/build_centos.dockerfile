# can be overwritten via '--build-arg' to docker (via master-build.sh)
ARG BASE_IMG_VER=7.4.1708

FROM centos:${BASE_IMG_VER}

RUN yum -y install \
	asciidoc \
	audit-libs-devel \
	bc \
	binutils-devel \
	bison \
	cmake \
	createrepo \
	elfutils-devel \
	epel-release \
	gcc-c++ \
	gettext \
	hmaccalc \
	java-devel \
	kbd \
	keyutils \
	livecd-tools \
	m4 \
	make \
	ncurses-devel \
	net-tools \
	newt-devel \
	numactl-devel \
	openssl \
	openssl-devel \
	pciutils-devel \
	pesign \
	'perl(ExtUtils::Embed)' \
	python-devel \
	python-docutils \
	rpm-build \
	sudo \
	wget \
	which \
	xmlto \
	zlib-devel

# install packages requiring epel-release
RUN yum -y install \
	fakeroot

COPY build.sh library_fs.sh /root/
ENTRYPOINT ["/root/build.sh"]

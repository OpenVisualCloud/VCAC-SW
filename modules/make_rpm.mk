name = vcass-modules
arch = $(shell uname -p)
#version = 0.0.0
ifdef RPMVERSION
	version=$(RPMVERSION)
else
	version = 0.0.0
endif

ifndef USE_CURRENT_KERNEL
USE_CURRENT_KERNEL=0
endif

ifndef KERNEL_VERSION
ifndef KERNEL_SRC
#use recently modified kernel-devel as default
KERNEL_SRC=`ls -1dt /usr/src/kernels/* | head -1`
endif
ifdef KERNEL_SRC
KERNEL_VERSION=$(shell cat $(KERNEL_SRC)/include/config/kernel.release)
endif
endif

release = 0

#  RPM build files and directories
topdir = $(HOME)/rpmbuild
rpm = $(topdir)/RPMS/$(arch)/$(name)-$(version)-$(release).$(arch).rpm
rpmdevel = $(topdir)/RPMS/$(arch)/$(name)-devel-$(version)-$(release).$(arch).rpm
rpmheaders = $(topdir)/RPMS/$(arch)/$(name)-headers-$(version)-$(release).$(arch).rpm
specfile = $(topdir)/SPECS/$(name).spec
source_tar = $(topdir)/SOURCES/$(name)-$(version).tar.gz
src  = $(shell cat MANIFEST)
rpmdirs = $(addprefix $(topdir)/, BUILD BUILDROOT BUILT RPMS SOURCES SPECS SRPM)
record = $(topdir)/BUILT/$(name)

#  RPM build flags
rpmbuild_flags = -E '%define _topdir $(topdir)'
rpmbuild_flags += -E '%define kernel_src $(KERNEL_SRC)'
rpmbuild_flags += -E '%define kversion $(KERNEL_VERSION)'
rpmbuild_flags += -E '%define usecurrentkernel $(USE_CURRENT_KERNEL)'
rpmclean_flags = $(rpmbuild_flags) --clean --rmsource --rmspec
rpmbuild_flags += -E '%define _version $(version)'
rpmbuild_flags += --define 'debug_package %{nil}'

include make.spec

$(record): $(rpmdirs) $(rpm)
	@echo $(rpm) > $@
	@echo $(rpmheaders) > $@

$(rpm): $(specfile) $(source_tar)
	rpmbuild $(rpmbuild_flags) $(specfile) -ba

$(source_tar): $(src) MANIFEST
	tar czvf $@ -T MANIFEST

$(specfile): make.spec
	@echo "$$make_spec" > $@

$(topdir)/%:
	@mkdir -p $@

clean:
	@rm -f $(record)
	-rpmbuild $(rpmclean_flags) $(specfile)

.PHONY: all clean

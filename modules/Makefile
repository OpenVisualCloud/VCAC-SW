# FILE LICENSE TAG: GPL

VCASS_COMMIT ?= 0
VCASS_VERSION ?= 0
VCASS_BUILDNO ?= 0
export VCASS_COMMIT := $(VCASS_COMMIT)
export VCASS_VERSION := $(VCASS_VERSION)
export VCASS_BUILDNO := $(VCASS_BUILDNO)
export VCASS_BUILTBY := $(shell echo "`whoami`@`uname -n`")
export VCASS_BUILTON := $(shell date +'%F %T %z')

export KERNEL_VERSION
export KERNEL_SRC

CP = cp
CP_t = $(CP) -r
INSTALL = install
INSTALL_d = $(INSTALL) -d
INSTALL_x = $(INSTALL)
INSTALL_f = $(INSTALL) -m644

prefix = 
sysconfdir = $(prefix)/etc
includedir = $(prefix)/include
base_sbindir = $(prefix)/sbin

kmodinstalldir = /lib/modules/$(KERNEL_VERSION)
kmodincludedir = $(realpath $(KERNEL_SRC))/include/modules

systemd_dir = $(prefix)/lib/systemd
services_dir = $(systemd_dir)/system

# If building the host's driver for a VCA co-processor card, which card
# $(ARCH) it should support
export VCA_CARD_ARCH

.PHONY: all install modules
.PHONY: modules_install conf_install dev_install kdev_install

all: modules

install: modules_install conf_install kdev_install

modules modules_install: %:
	$(MAKE) -j $(PARALLEL) -C $(KERNEL_SRC) M=$(CURDIR) KERNELRELEASE=$(KERNELRELEASE) $* \
		INSTALL_MOD_PATH=$(DESTDIR)


conf_install:
ifneq ($(VCA_CARD_ARCH),)
	$(INSTALL_d) $(DESTDIR)$(sysconfdir)/sysconfig
	$(INSTALL_d) $(DESTDIR)$(sysconfdir)/sysconfig/modules
	$(INSTALL_d) $(DESTDIR)$(sysconfdir)/modprobe.d

endif
	$(INSTALL_d) $(DESTDIR)$(sysconfdir)/udev/rules.d
	$(INSTALL_f) scripts/vca_config $(DESTDIR)$(sysconfdir)/vca_config
	$(INSTALL_d) $(DESTDIR)$(base_sbindir)
	$(INSTALL_x) scripts/vca_eth_cfg.sh $(DESTDIR)$(base_sbindir)/vca_eth_cfg.sh
	$(INSTALL_x) scripts/vca_setup.sh $(DESTDIR)$(base_sbindir)/vca_setup.sh
	$(INSTALL_x) scripts/vca_uninstall.sh $(DESTDIR)$(base_sbindir)/vca_uninstall.sh
	$(INSTALL_d) $(DESTDIR)$(systemd_dir)
	$(INSTALL_d) $(DESTDIR)$(services_dir)
	$(INSTALL_f) scripts/vca_agent.service $(DESTDIR)$(services_dir)
	$(INSTALL_x) scripts/vca_agent.sh $(DESTDIR)$(base_sbindir)
	$(INSTALL_x) scripts/vca_elog.py $(DESTDIR)$(base_sbindir)/vca_elog.py
	$(INSTALL_x) scripts/vca_pci_errors $(DESTDIR)$(base_sbindir)/vca_pci_errors
dev_install:
	$(INSTALL_d) $(DESTDIR)$(includedir)/vca
	$(INSTALL_f) vca/vop/vca_ioctl.h $(DESTDIR)$(includedir)
	$(INSTALL_f) vca/common/vca_dev_common.h $(DESTDIR)$(includedir)
	$(INSTALL_f) vca/common/vca_common.h $(DESTDIR)$(includedir)
	$(INSTALL_f) vca/vca_csm/vca_csm_ioctl.h $(DESTDIR)$(includedir)
	$(INSTALL_f) vca/vca_mgr/vca_mgr_ioctl.h $(DESTDIR)$(includedir)
	$(INSTALL_f) vca/vca_mgr_extd/vca_mgr_extd_ioctl.h $(DESTDIR)$(includedir)

kdev_install:
	$(INSTALL_d) $(DESTDIR)$(kmodinstalldir)
	$(INSTALL_f) Module.symvers $(DESTDIR)$(kmodinstalldir)/scif.symvers
	$(INSTALL_d) $(DESTDIR)$(kmodincludedir)

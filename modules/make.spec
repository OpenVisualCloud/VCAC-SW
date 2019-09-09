#
#  Copyright (2015) Intel Corporation All Rights Reserved.
#
#  This software is supplied under the terms of a license
#  agreement or nondisclosure agreement with Intel Corp.
#  and may not be copied or disclosed except in accordance
#  with the terms of that agreement.
#

define make_spec
%{!?kversion: %define kversion %(uname -r)}
%{!?kernel_src: %define kernel_src /lib/modules/%{kversion}/build}
%{!?kernel_base_version:  %define kernel_base_version  %(echo %kversion | awk -F '-' '{print $$1}')}
%define kernel_stock_build %(echo %kversion | grep '.el7' | grep '.x86_64')
%if "%{kernel_stock_build}" != ""
%{!?kernel_extra_version: %define kernel_extra_version %(echo %kversion | awk -F '-' '{print $$2}' | sed 's/.x86_64//')}
%else
%{!?kernel_extra_version: %define kernel_extra_version %(echo %kversion | awk -F '-' '{print $$2}')}
%endif

Summary:  Host drivers for Intel® VCA cards
Name: $(name)
Version: %{_version}
Release: $(release)
License: GPLv2
Group: kernel
Vendor: Intel Corporation
URL: http://www.intel.com
Source0: %{name}-%{version}.tar.gz
%if %{_vendor} == suse
BuildRequires: kernel-headers kernel-default-devel
%else
BuildRequires: kernel-headers
%endif
%description
Provides host drivers for Intel® VCA cards

%if "%{kernel_stock_build}" != ""
	# a dirty hack to cater for kernels not conforming to distinction between software version and RPM version
	# (tested only for 693+ of the 3.10.0 line)
	%define is_kernel_3p10p0 %(awk -F'[-.]' '/^3\.10\.0-/ && $$4 >= 693' <<< %kversion)
	%define is_kernel_5p1p16 %(awk -F'[-.]' '/^5\.1\.16-/' <<< %kversion)
	%if "%{is_kernel_3p10p0}%{is_kernel_5p1p16}" != ""
		%define __kernelrelease %(echo %{kversion})
	%endif
%else
	# to mimic the changes in RPM PACKAGE name and in RPM FILE name made during kernel build in scripts/package/mkspec using __KERNELRELEASE
	%define __kernelrelease %(echo %{kversion} | sed -e "s/-/_/")
%endif
%define rpmname $(name)-%{__kernelrelease}
%define rpmdevelname $(name)-devel-%{__kernelrelease}

%define kreleaseversion $(KERNELRELEASE)

%package -n %{rpmname}
Group: kernel
Summary: Host driver for Intel® VCA cards

# Dependent kernel version name shouldn't contain ARCH (ex. x86_64)
# In case kernel name doesn't have ARCH in name nothing will change
Requires: gawk
%if "%{usecurrentkernel}" == "1"
%define kname $(shell rpm -qa | grep `uname -r | sed "s|-|_|g"` | grep -v "devel" | sed "s|.`uname -i`||g" | sed "s|kernel-||g")
Requires: kernel = %{kname}
%else
Requires: kernel = %(echo %__kernelrelease | sed 's/\(.*\).%(uname -i)/\1/')
%endif

%description -n %{rpmname}
Provides host driver for Intel® VCA cards

%package -n %{rpmdevelname}
Group:   kernel
Summary: Header and symbol version file for driver development
%description -n %{rpmdevelname}
Provides header and symbol version file for driver development

# Don't include %{kversion} in headers. It's simpler for others
# who depend on module-headers
%package -n $(name)-headers
Group:   kernel
Summary: Development header files specific to Intel VCA
%description -n $(name)-headers
Development header files specific to Intel VCA

%prep
%setup -D -q -c -T -a 0

%build
$(make_prefix)%{__make} VCA_CARD_ARCH=k1om KERNEL_SRC=%{kernel_src} \
	KERNEL_VERSION=%{kversion} VCASS_BUILDNO=%{version} $(make_postfix)

%install
%{__make} KERNEL_VERSION=%{kversion} KERNEL_SRC=%{kernel_src} \
	kmodincludedir=/usr/src/kernels/%{kversion}/include/modules \
	DESTDIR=%{buildroot} VCA_CARD_ARCH=k1om prefix="" install \
	VCASS_BUILDNO=%{version}
%{__make} KERNEL_VERSION=%{kversion} KERNEL_SRC=%{kernel_src} \
	kmodincludedir=/usr/src/kernels/%{kversion}/include/modules \
	DESTDIR=%{buildroot}/usr VCA_CARD_ARCH=k1om prefix="" dev_install
rm -f %{buildroot}/lib/modules/%{kreleaseversion}/modules.[abds]*
$(extra_install)

%clean

%post -n %{rpmname}
/sbin/depmod -a %{kversion}
if [ $$1 == 1 ]; then
	/sbin/vca_setup.sh
fi

%preun -n %{rpmname}
if [ $$1 == 0 ]; then
	if [ -e /sbin/vca_uninstall.sh ]; then
		/sbin/vca_uninstall.sh
	fi
fi

%postun -n %{rpmname}
/sbin/depmod -a %{kversion}

%files -n %{rpmname}
%defattr(-,root,root,-)
/etc/modprobe.d/
/lib/modules/%{kreleaseversion}
/lib/modules/%{kreleaseversion}/extra/plx87xx.ko
/lib/modules/%{kreleaseversion}/extra/vca/plx87xx_dma/plx87xx_dma.ko
/lib/modules/%{kreleaseversion}/extra/vca/bus/vop_bus.ko
/lib/modules/%{kreleaseversion}/extra/vca/vop/vop.ko
/lib/modules/%{kreleaseversion}/extra/vca/bus/vca_csm_bus.ko
/lib/modules/%{kreleaseversion}/extra/vca/vca_csm/vca_csm.ko
/lib/modules/%{kreleaseversion}/extra/vca/bus/vca_mgr_bus.ko
/lib/modules/%{kreleaseversion}/extra/vca/vca_mgr/vca_mgr.ko
/lib/modules/%{kreleaseversion}/extra/vca/bus/vca_mgr_extd_bus.ko
/lib/modules/%{kreleaseversion}/extra/vca/vca_mgr_extd/vca_mgr_extd.ko
/lib/modules/%{kreleaseversion}/extra/vca/bus/vca_csa_bus.ko
/lib/modules/%{kreleaseversion}/extra/vca/vca_csa/vca_csa.ko
/lib/modules/%{kreleaseversion}/extra/vca/blockio/vcablk_bckend.ko
/lib/modules/%{kreleaseversion}/extra/vca/blockio/vcablkfe.ko
/lib/modules/%{kreleaseversion}/extra/vca/pxe/vcapxe.ko
/lib/modules/%{kreleaseversion}/extra/vca/vca_virtio/vca_virtio.ko
/lib/modules/%{kreleaseversion}/extra/vca/vca_virtio/vca_virtio_net.ko
/lib/modules/%{kreleaseversion}/extra/vca/vca_virtio/vca_virtio_ring.ko
/lib/modules/%{kreleaseversion}/extra/vca/vca_virtio/vca_vringh.ko

/etc/vca_config
$(extra_files)
%defattr(700, root, root, -)
/sbin/vca_pci_errors
/sbin/vca_eth_cfg.sh
/sbin/vca_setup.sh
/sbin/vca_uninstall.sh
/sbin/vca_agent.sh
/sbin/vca_elog.py
/lib/systemd/system/vca_agent.service

%files -n %{rpmdevelname}
%defattr (-,root,root)
/lib/modules/%{kversion}/scif.symvers

%files -n $(name)-headers
%defattr(-,-,-,-)
"/usr/include/vca_dev_common.h"
"/usr/include/vca_common.h"
"/usr/include/vca_ioctl.h"
"/usr/include/vca_mgr_ioctl.h"
"/usr/include/vca_mgr_extd_ioctl.h"
"/usr/include/vca_csm_ioctl.h"

%changelog

endef

export make_spec

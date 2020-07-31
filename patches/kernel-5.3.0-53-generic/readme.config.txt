Please find differences to default config below:

commit ...
Author Przemek Kitszel
Date Fri Jul 31 2020

	Enable CONFIG_OVERLAY_FS=m

	Select other OVERLAY_FS params according to your needs, in our case it is:
	CONFIG_OVERLAY_FS=m
	CONFIG_OVERLAY_FS_REDIRECT_DIR=y
	CONFIG_OVERLAY_FS_REDIRECT_ALWAYS_FOLLOW=y
	+# CONFIG_OVERLAY_FS_INDEX is not set
	+# CONFIG_OVERLAY_FS_XINO_AUTO is not set
	CONFIG_OVERLAY_FS_METACOPY=y


commit b123d3b9586856edb2e68c2234e8a924f5b3c82b
Author: Artur Opalinski <ArturX.Opalinski@intel.com>
Date:   Fri Jul 10 11:09:53 2020 +0200

    Enable coretemp kernel driver
    
    Required for Linux command 'sensors coretemp-isa-0000' (from lm-sensors package)
    Required for 'vcactl temp'
    Set 'CONFIG_SENSORS_CORETEMP=y' in .config

commit 9fd0dd78894fe567f3b3a02ad533e36bb3ad29f4
Author: Artur Opalinski <ArturX.Opalinski@intel.com>
Date:   Wed Jun 10 09:53:01 2020 +0000

    Build EXT4, LOOP, NFS, NFSV3, NFSV4, PL2303 as modules
    
    Current methd of building&booting images requires this

commit 12289c1785795a5c8a9347888b6f2e1a41182c53
Author: Artur Opalinski <ArturX.Opalinski@intel.com>
Date:   Wed Jun 17 12:01:20 2020 +0200

    Enable serial console (flags to emulate PCH-uart in PCH-SKL as legacy device 16550A)
    
        CONFIG_SERIAL_8250_DW=y
        CONFIG_MFD_CORE=y (to enable MFD_INTEL_LPSS below)
        CONFIG_MFD_INTEL_LPSS=y (to enable MFD_INTEL_LPSS_PCI and MFD_INTEL_LPSS_ACPI below)
        CONFIG_MFD_INTEL_LPSS_PCI=y
        CONFIG_MFD_INTEL_LPSS_ACPI=y

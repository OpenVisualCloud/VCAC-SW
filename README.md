# VCAC-SW

## Notice
BIOS files necessary to run end-to-end solution are not provided here yet.
User Guide / documentation describing end-to-end solution is not provided here yet.
For now, intended audience is limited. 

## Purpose
This repository contains:
* user-space application and kernel drivers for Intel VCAC pcie-extension cards;
* kernel patches adding support for VCAC cards;
* build scripts necessary to build binary artifacts;
* scripts to build OS images for VCAC cards (both Linux and Windows images).

## EEPROM and VCAgent files
Please refer to User Guide for more information and usage suggestions.

VCAC-R, normal EEPROM:
https://openvisualcloud.github.io/VCAC-SW/eeprom/VCAC-R

VCAC-R, DMA-disabled EEPROM:
https://openvisualcloud.github.io/VCAC-SW/eeprom/VCAC-R/DMA_disabled

VCAC-A, normal EEPROM:
https://openvisualcloud.github.io/VCAC-SW/eeprom/VCAC-A

VCA Agent:
https://openvisualcloud.github.io/VCAC-SW/windows/vcagent


## Building
Note: Detailed instruction is distributed via User Guide. (Especially for Windows images).

### Linux:
Note: Docker in version at least 17.05 must be installed and configured, script will
run hello-world container to check if this prerequisite is met.
(Building without docker could be achieved by somewhat experienced Linux user,
edit master_build.sh or run build.sh manually).

All scripts mentioned in this README print help when invoked without arguments.
Single dot (.) in beginning of paths (ie: ./master_build.sh) refers to
*buildscripts* directory of cloned git repository of VCA SW source.

#### Downloading third party sources
Some third party sources must be present before actual build, to download them
look for URLs in top-level download_dependencies.sh file.
E.g. sources for Ubuntu Linux kernel 5.3.0 can be found in two parts:
	https://launchpad.net/ubuntu/+archive/primary/+sourcefiles/linux/5.3.0-53.47/linux_5.3.0.orig.tar.gz
	https://launchpad.net/ubuntu/+archive/primary/+sourcefiles/linux/5.3.0-53.47/linux_5.3.0-53.47.diff.gz
After manual inspection and eventual URL customization, invoke script:
./download_dependencies.sh \<path-to-store-downloads\>
usual proxy settings will be taken into account (similarly for docker).

Docker would also download base images and current version of standard software
from docker site and Linux distribution sites.


#### Actual build
Invoke:
./master_build.sh --vca-src-dir \<vca-sw-input-dir\> --out-dir \<output-dir\> --downloads-dir \<downloaded-dependencies-dir\>
where:
\<vca-sw-input-dir\> is path to cloned repository
\<downloaded-dependencies-dir\> is path to downloaded 3rd party binaries
    (the same as used for ./download_dependencies.sh)
\<output-dir\> is path to place resulting binaries, will be automatically created

Additional optional parameter could be passed to build only part of binaries:
--only-pattern \<pattern\> : build only configurations which match \<pattern\>
(match is done by grep -E)
(for list of available configurations look in ./master_build.sh::main())

Build would take a while.
Currently Linux kernel with VCA support, drivers for such kernel and user space
application are build.

Resulting binaries could be installed on server machines with VCA pci extension
cards, it could be different from building machine.


## Products
VCAC-A release locates at branch VCAC-A.
https://github.com/OpenVisualCloud/VCAC-SW/tree/VCAC-A

## Contributing
Use GitHub's "issues" or "pull-request" features,
or reach via e-mail directly developers: Masłowski, Karol or Kitszel, Przemysław (for e-mail addresses, look in git log).
Ongoing development currenlty is outside of GitHub, so your potential contribution would be cherry-picked into our internal repository.
Keep in mind that project has long history and this is fifth place to host code / building instructions.

## Licence
GNU GENERAL PUBLIC LICENSE version 2

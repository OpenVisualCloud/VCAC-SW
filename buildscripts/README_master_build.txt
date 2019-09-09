# Building VCA software binaries script notes #

Zip archive downloaded from Intel site (called later as VCA SW source package)
should be extracted, and will be used as input for binaries generation script
(this README.txt file is also in this Zip archive).

Docker in version at least 17.05 must be installed and configured, script will
run hello-world container to check if this prerequisite is met.

All scripts mentioned in this README print help when invoked without arguments.
Single dot (.) in beginning of paths (ie: ./master_build.sh) refers to top-level
of extracted VCA SW source package Zip archive.

After zip extraction scripts should be given executable bit via command:
chmod +x ./master_build.sh ./download_dependencies.sh

## Downloading third party sources ##

Some third party sources must be present before actual build, to download them
look for URLs in top-level download_dependencies.sh file.
After manual inspection and eventual URL customization, invoke script:
./download_dependencies.sh <path-to-store-downloads>
usual proxy settings will be taken into account (similarly for docker).

Docker would also download base images and current version of standard software
from docker site and Linux distribution sites.

## Actual build ##

Invoke (2 lines here):
./master_build.sh --vca-src-dir <vca-sw-input-dir> --out-dir <output-dir> \
                  --downloads-dir <downloaded-dependencies-dir>
where:
<vca-sw-input-dir> is path to extracted VCA SW source release package
<downloaded-dependencies-dir> is path to downloaded 3rd party binaries
    (the same as used for ./download_dependencies.sh)
<output-dir> is path to place resulting binaries, will be automatically created

Additional optional parameter could be passed to build only part of binaries:
--only-pattern <pattern> : build only configurations which match <pattern>
(match is done by grep -E)
(for list of available configurations look in ./master_build.sh::main())

Build would take a while.
Currently Linux kernel with VCA support, drivers for such kernel and user space
application are build.

Resulting binaries could be installed on server machines with VCA pci extension
cards, it could be different from building machine.


## Generation of OS-images with support for VCA card ##

Generation instructions are covered by User Guide.
Scripts for generation are contained in top-level archive:
./build_scripts*.tar.gz - for Linux images,
./windows_image_generation_files.zip - for Windows images.

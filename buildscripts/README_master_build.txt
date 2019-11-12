(Working example for host side is in last section)

# Docker installation and configuration

Install Docker Community Edition:
	https://docs.docker.com/install/linux/docker-ce/centos/
	https://docs.docker.com/install/linux/docker-ce/ubuntu/

Configure it properly
	for HTTP proxy configuration see:
		https://docs.docker.com/config/daemon/systemd/
	for granting permission to docker deamon see:
		https://docs.docker.com/install/linux/linux-postinstall/


# Building VCA software binaries script notes

Zip archive downloaded from Intel site (called 'VCA SW source package' below)
should be extracted, and will be used as input for binaries generation script.
This README.txt file is part of the Zip archive.

Docker version at least 17.05 must be installed and configured, the script will
run 'hello-world container' to check if this prerequisite is met.

All scripts mentioned in this README print help when invoked without arguments.
Paths beginning with a single dot (.) (e.g.: ./master_build.sh) refer to
top-level directory of the extracted VCA SW source package Zip archive.

Unzipped scripts should be made executable by running:
  chmod +x ./master_build.sh ./download_dependencies.sh

## Downloading third party software ##

Some third party software and source files must be present before actual build.
Currently, they include CentOS and Ubuntu kernel sources, and Boost library
kernel sources. To enable automatic download into proper destination location,
modify  URLs in the marked section of the download_dependencies.sh file.
After inspection and optional customization of the URLs, invoke the script:
  ./download_dependencies.sh <path-to-store-downloads>
The above script requires HTTP, HTTPS, and FTP access to some public Internet
sites. Also Docker needs access to its base images and current version of some
standard software from docker site and Linux distribution sites. Ensure your
firewalls and proxies allow such traffic.

## Actual build ##

Invoke (note the below command consists of two lines of text):
  ./master_build.sh --vca-src-dir <vca-sw-input-dir> --out-dir <output-dir> \
                    --downloads-dir <downloaded-dependencies-dir>
where:
	<vca-sw-input-dir> is the path to the extracted VCA SW source release package
	<downloaded-dependencies-dir> is the path to 3rd party files downloaded
	by ./download_dependencies.sh in the previous step)
	<output-dir> indicates the location to place resulting files. The
	path will be created automatically.
Optionally, to build only selected binaries:
	--only-pattern <pattern> : build only configurations which match <pattern>
	Matching is done by 'grep -E'. See ./master_build.sh::main() for a list of
	available configurations to match.

Allow significant time to complete the process as downloads as well as building
of large sets of source files may take a considerable amount of time (e.g. a
single Linux kernel may take hours to build on a desktop-class PC).
Currently Linux kernel(s) with VCA support, VCA drivers for the kernel(s), and
user space VCA application are build.

Resulting binaries can be installed on VCA hosts which are separate and
which differ in configuration from the building machine.

## Generation of OS images for VCA card ##

Instructions on generating OS images for VCA card are covered by VCA User Guide.
Scripts for generation are contained in top-level directory of the archive:
./build_scripts*.tar.gz - for Linux images,
./windows_image_generation_files.zip - for Windows images.

## Example flow for VCAC-R card ##

Let's assume that release Zip archive is extracted to ~/vca-src
Invoke following commands to build VCAC-R artifacts for host:
$ chmod +x ~/vca-src/master_build.sh ~/vca-src/download_dependencies.sh
$ ~/vca-src/download_dependencies.sh --downloads-dir ~/downloads --only-pattern vcac-r
$ ~/vca-src/master_build.sh --downloads-dir ~/downloads --only-pattern vcac-r --vca-src-dir ~/vca-src --out-dir ~/vca-out-vcac-r

Please refer to User Guide for Windows images generation.

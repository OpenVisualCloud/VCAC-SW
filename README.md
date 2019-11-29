# Contents
## This branch contains:
- Build script and patches to build host kernel/module/app (CentOS 7.4)
- Build scripts to build OS images for VCAC-A card (Ubuntu 16.04).

# Building
### Note: 
> Detailed instruction is distributed via User Guide.

## Prerequisite
Before being able to build and run docker, you will need install docker on your system. Please use https://docs.docker.com/install  to install docker-ce.

## Actual build
### Build kernel and driver module for host:   
```
cd VCAC-SW/VCAC-A/Intel_Media_Analytics_Host/scripts/
./build.sh
```

If the docker images used for compiling have been generated in an earlier execution, user can save time required to build them again by the following executions:
```
./build.sh -s
```
	
### Build system image to be loaded on VCAC-A card:
```
cd VCAC-SW/VCAC-A/Intel_Media_Analytics_Node/scripts/
run: vcad_build.sh -o <BASIC/FULL>  <options>
	         BASIC: basic OS image only with modules
	         FULL: OS image with MSS/OpenVINO installed
```
	
Check more options via 'vcad_build.sh -h', and below are some examples:

- Skip downloading source code and dependencies, put the files under /PATH/TO/PACKAGE/cache, then pass "-c" flag:
```
vcad_build.sh -o <BASIC/FULL>  -c
```

- If the docker images used for compiling have been generated in an earlier execution, user can save time required to build them again by the following executions:
```
vcad_build.sh -o <BASIC/FULL>  -s
```

- The size of the vcad system image is set to 48GB by default. And the system image size is configurable through passing flag "-e" followed by the image size measured in GB
```
vcad_build.sh -o <BASIC/FULL>  -e 24
```
 
# Contributing
Use GitHub's "issues" or "pull-request" features, or reach via e-mail directly developers: Zhao, Ping or Liu, Yi (for e-mail addresses, look in git log). 
# License
GNU GENERAL PUBLIC LICENSE version 2

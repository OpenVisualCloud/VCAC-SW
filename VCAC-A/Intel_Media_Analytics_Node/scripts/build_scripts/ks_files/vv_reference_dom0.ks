lang en_GB.UTF-8
keyboard us
timezone Europe/Brussels --isUtc
auth --useshadow --enablemd5
selinux --disabled
firewall --enabled --service=mdns
xconfig --startxonboot
part / --size 2500 --fstype ext4
services --enabled=network,sshd

# root password
rootpw --iscrypted $6$0f2rNPn2oON$lO5Dh/Cb7s0H1GR./PZPPYT.xjJPTdIA.M.quAtA8fpMjRo.0rsGJmm8Zq83rOY0xnkzoWQAPhCtu5sj8ztgw1

# repository name and location

repo --name=vca_os_repo --baseurl=VCA_OS_REPO
# uncomment if going to use additional repository for non-standard and non-VCA-build packages
#repo --name=vca_extras_repo --baseurl=VCA_EXTRAS_REPO
repo --name=vca_build_repo --baseurl=VCA_BUILD_REPO

%packages

# packages from vca_os_repo

firewalld
lrzsz
net-tools
pciutils
iptraf-ng
tcpdump
redhat-lsb-core
vim
mc
wget
mcelog
openssh-server
dhclient
yum
openssh-clients
rpm
nmap
lsscsi
lm_sensors
sysstat
dos2unix
grub2-efi
grub2-tools
grub2-efi-modules
grub2
nfs-utils
rsyslog        #dependency for GSS Proxy API

# packages from vca_extras_repo

# packages from vca_build_repo

mrouted
atk
cairo
cairo-gobject
gdk-pixbuf2
gnutls
gtk3
libaio
libcacard
libjpeg-turbo
libpng
libusbx
libX11
libXext
mesa-libGL
nettle
numactl-libs
pango
pixman
SDL
spice-server
yajl
#xen-VCA
xen
xen-hypervisor
xen-runtime
# xen-net* commented since CentOS 7.4
# alternative globbing to encompass various kerne  versions: kernel-?.*
kernel-[0-9]*
vcass-modules-[0-9]*
#kernel-3.10.0*
#vcass-modules-3*

%end

%post

%end

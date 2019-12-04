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
repo --name=vca_extras_repo --baseurl=VCA_EXTRAS_REPO
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
libpciaccess
libX11
mesa-dri-drivers
libXdamage
libXext
libXfixes
libXxf86vm
mesa-libGL
mesa-libglapi
grub2-efi
grub2-tools
grub2-efi-modules
grub2
numactl-libs
nfs-utils
rsyslog        #dependency for GSS Proxy API

# for KVM
libvirt
virt-install
bridge-utils
qemu-kvm
edk2.git-ovmf-x64-0-* # edk2 is needed for kvm to boot images with uefi bios
edk2.git-0-*

# packages from vca_extras_repo

# packages from vca_build_repo
# alternative globbing to encompass various kerne  versions: kernel-?.*
kernel-[0-9]*
vcass-modules-[0-9]*

%end

%post

# Advertising UEFI<-> NVRAM config file mapping
cat >> /etc/libvirt/qemu.conf << EOF
nvram = [
   "/usr/share/edk2.git/ovmf-x64/OVMF_CODE-pure-efi.fd:/usr/share/edk2.git/ovmf-x64/OVMF_VARS-pure-efi.fd",
   "/usr/share/edk2.git/aarch64/QEMU_EFI-pflash.raw:/usr/share/edk2.git/aarch64/vars-template-pflash.raw",
]
EOF
%end

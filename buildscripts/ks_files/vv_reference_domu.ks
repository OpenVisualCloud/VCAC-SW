lang en_GB.UTF-8
keyboard us
timezone Europe/Brussels --isUtc
auth --useshadow --enablemd5
selinux --disabled
firewall --disabled --service=mdns
xconfig --startxonboot
part / --size 8192 --fstype ext4
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
grub2
grub2-tools
avahi
avahi-tools
libpciaccess
libX11
mesa-dri-drivers
libXdamage
libXext
libXfixes
libXxf86vm
mesa-libGL
mesa-libglapi
nfs-utils
rsyslog        #dependency for GSS Proxy API

# packages from vca_extras_repo

# packages from vca_build_repo

# alternative globbing to encompass various kernel versions: kernel-?.*
kernel-[0-9]*
kernel-devel-[0-9]*
kernel-headers-[0-9]*
vcass-modules-[0-9]*
# kernel-3.10.0*
# kernel-devel-3.10.0*
# kernel-headers-3.10.0*
# vcass-modules-3*
# xen-net*Commented out by A.O. at CentOS 7.4, Xen 4.7

# packages for mss

redhat-lsb-core
rpm-build
expect
gcc
compat-gcc-44
libgcc

%end

%post

echo "Custom domU image initialization"

cat > /etc/sysconfig/network-scripts/ifcfg-eth0 << EOL
HWADDR=c0:ff:ee:00:01:01
TYPE=Ethernet
NETMASK=255.255.255.255
BOOTPROTO=static
IPADDR=172.31.1.2
ONBOOT=yes
NM_CONTROLLED="no"
MTU=65232
DEFROUTE=yes
EOL

cat > /etc/sysconfig/network-scripts/ifcfg-eth1 << EOL
HWADDR=c0:ff:ee:00:01:02
TYPE=Ethernet
NETMASK=255.255.255.255
BOOTPROTO=static
IPADDR=172.31.2.2
ONBOOT=yes
NM_CONTROLLED="no"
MTU=65232
DEFROUTE=yes
EOL

cat > /etc/sysconfig/network-scripts/ifcfg-eth2 << EOL
HWADDR=c0:ff:ee:00:01:03
TYPE=Ethernet
NETMASK=255.255.255.255
BOOTPROTO=static
IPADDR=172.31.3.2
ONBOOT=yes
NM_CONTROLLED="no"
MTU=65232
DEFROUTE=yes
EOL

cat > /etc/sysconfig/network-scripts/ifcfg-eth3 << EOL
HWADDR=c0:ff:ee:00:02:01
TYPE=Ethernet
NETMASK=255.255.255.255
BOOTPROTO=static
IPADDR=172.31.4.2
ONBOOT=yes
NM_CONTROLLED="no"
MTU=65232
DEFROUTE=yes
EOL

cat > /etc/sysconfig/network-scripts/ifcfg-eth4 << EOL
HWADDR=c0:ff:ee:00:02:02
TYPE=Ethernet
NETMASK=255.255.255.255
BOOTPROTO=static
IPADDR=172.31.5.2
ONBOOT=yes
NM_CONTROLLED="no"
MTU=65232
DEFROUTE=yes
EOL

cat > /etc/sysconfig/network-scripts/ifcfg-eth5 << EOL
HWADDR=c0:ff:ee:00:02:03
TYPE=Ethernet
NETMASK=255.255.255.255
BOOTPROTO=static
IPADDR=172.31.6.2
ONBOOT=yes
NM_CONTROLLED="no"
MTU=65232
DEFROUTE=yes
EOL

cat > /etc/sysconfig/network-scripts/ifcfg-eth6 << EOL
HWADDR=c0:ff:ee:00:03:01
TYPE=Ethernet
NETMASK=255.255.255.255
BOOTPROTO=static
IPADDR=172.31.7.2
ONBOOT=yes
NM_CONTROLLED="no"
MTU=65232
DEFROUTE=yes
EOL

cat > /etc/sysconfig/network-scripts/ifcfg-eth7 << EOL
HWADDR=c0:ff:ee:00:03:02
TYPE=Ethernet
NETMASK=255.255.255.255
BOOTPROTO=static
IPADDR=172.31.8.2
ONBOOT=yes
NM_CONTROLLED="no"
MTU=65232
DEFROUTE=yes
EOL

cat > /etc/sysconfig/network-scripts/ifcfg-eth8 << EOL
HWADDR=c0:ff:ee:00:03:03
TYPE=Ethernet
NETMASK=255.255.255.255
BOOTPROTO=static
IPADDR=172.31.9.2
ONBOOT=yes
NM_CONTROLLED="no"
MTU=65232
DEFROUTE=yes
EOL

cat > /etc/sysconfig/network-scripts/ifcfg-eth9 << EOL
HWADDR=c0:ff:ee:00:04:01
TYPE=Ethernet
NETMASK=255.255.255.255
BOOTPROTO=static
IPADDR=172.31.10.2
ONBOOT=yes
NM_CONTROLLED="no"
MTU=65232
DEFROUTE=yes
EOL

cat > /etc/sysconfig/network-scripts/ifcfg-eth10 << EOL
HWADDR=c0:ff:ee:00:04:02
TYPE=Ethernet
NETMASK=255.255.255.255
BOOTPROTO=static
IPADDR=172.31.11.2
ONBOOT=yes
NM_CONTROLLED="no"
MTU=65232
DEFROUTE=yes
EOL

cat > /etc/sysconfig/network-scripts/ifcfg-eth11 << EOL
HWADDR=c0:ff:ee:00:04:03
TYPE=Ethernet
NETMASK=255.255.255.255
BOOTPROTO=static
IPADDR=172.31.12.2
ONBOOT=yes
NM_CONTROLLED="no"
MTU=65232
DEFROUTE=yes
EOL

cat > /etc/sysconfig/network-scripts/ifcfg-eth12 << EOL
HWADDR=c0:ff:ee:01:01:01
TYPE=Ethernet
NETMASK=255.255.255.255
BOOTPROTO=static
IPADDR=172.31.1.253
ONBOOT=yes
NM_CONTROLLED="no"
MTU=65232
DEFROUTE=yes
EOL

cat > /etc/sysconfig/network-scripts/ifcfg-eth13 << EOL
HWADDR=c0:ff:ee:01:01:02
TYPE=Ethernet
NETMASK=255.255.255.255
BOOTPROTO=static
IPADDR=172.31.2.253
ONBOOT=yes
NM_CONTROLLED="no"
MTU=65232
DEFROUTE=yes
EOL

cat > /etc/sysconfig/network-scripts/ifcfg-eth14 << EOL
HWADDR=c0:ff:ee:01:01:03
TYPE=Ethernet
NETMASK=255.255.255.255
BOOTPROTO=static
IPADDR=172.31.3.253
ONBOOT=yes
NM_CONTROLLED="no"
MTU=65232
DEFROUTE=yes
EOL

cat > /etc/sysconfig/network-scripts/ifcfg-eth15 << EOL
HWADDR=c0:ff:ee:01:02:01
TYPE=Ethernet
NETMASK=255.255.255.255
BOOTPROTO=static
IPADDR=172.31.4.253
ONBOOT=yes
NM_CONTROLLED="no"
MTU=65232
DEFROUTE=yes
EOL

cat > /etc/sysconfig/network-scripts/ifcfg-eth16 << EOL
HWADDR=c0:ff:ee:01:02:02
TYPE=Ethernet
NETMASK=255.255.255.255
BOOTPROTO=static
IPADDR=172.31.5.253
ONBOOT=yes
NM_CONTROLLED="no"
MTU=65232
DEFROUTE=yes
EOL

cat > /etc/sysconfig/network-scripts/ifcfg-eth17 << EOL
HWADDR=c0:ff:ee:01:02:03
TYPE=Ethernet
NETMASK=255.255.255.255
BOOTPROTO=static
IPADDR=172.31.6.253
ONBOOT=yes
NM_CONTROLLED="no"
MTU=65232
DEFROUTE=yes
EOL

cat > /etc/sysconfig/network-scripts/ifcfg-eth18 << EOL
HWADDR=c0:ff:ee:01:03:01
TYPE=Ethernet
NETMASK=255.255.255.255
BOOTPROTO=static
IPADDR=172.31.7.253
ONBOOT=yes
NM_CONTROLLED="no"
MTU=65232
DEFROUTE=yes
EOL

cat > /etc/sysconfig/network-scripts/ifcfg-eth19 << EOL
HWADDR=c0:ff:ee:01:03:02
TYPE=Ethernet
NETMASK=255.255.255.255
BOOTPROTO=static
IPADDR=172.31.8.253
ONBOOT=yes
NM_CONTROLLED="no"
MTU=65232
DEFROUTE=yes
EOL

cat > /etc/sysconfig/network-scripts/ifcfg-eth20 << EOL
HWADDR=c0:ff:ee:01:03:03
TYPE=Ethernet
NETMASK=255.255.255.255
BOOTPROTO=static
IPADDR=172.31.9.253
ONBOOT=yes
NM_CONTROLLED="no"
MTU=65232
DEFROUTE=yes
EOL

cat > /etc/sysconfig/network-scripts/ifcfg-eth21 << EOL
HWADDR=c0:ff:ee:01:04:01
TYPE=Ethernet
NETMASK=255.255.255.255
BOOTPROTO=static
IPADDR=172.31.10.253
ONBOOT=yes
NM_CONTROLLED="no"
MTU=65232
DEFROUTE=yes
EOL

cat > /etc/sysconfig/network-scripts/ifcfg-eth22 << EOL
HWADDR=c0:ff:ee:01:04:02
TYPE=Ethernet
NETMASK=255.255.255.255
BOOTPROTO=static
IPADDR=172.31.11.253
ONBOOT=yes
NM_CONTROLLED="no"
MTU=65232
DEFROUTE=yes
EOL

cat > /etc/sysconfig/network-scripts/ifcfg-eth23 << EOL
HWADDR=c0:ff:ee:01:04:03
TYPE=Ethernet
NETMASK=255.255.255.255
BOOTPROTO=static
IPADDR=172.31.12.253
ONBOOT=yes
NM_CONTROLLED="no"
MTU=65232
DEFROUTE=yes
EOL

cat > /etc/sysconfig/network-scripts/ifcfg-eth99 << EOL
DEVICE=eth0
TYPE=Ethernet
BOOTPROTO=dhcp
ONBOOT=yes
NM_CONTROLLED="no"
MTU=65232
DEFROUTE=yes
EOL

echo "default dev eth0" > /etc/sysconfig/network-scripts/route-eth0
echo "default dev eth1" > /etc/sysconfig/network-scripts/route-eth1

echo "avahi-set-host-name vca_\`ip addr show eth0 |grep link/ether |cut -f6 -d' '|tr -d :\`" >> /etc/rc.local
chmod +x /etc/rc.local

echo "SUBSYSTEM==\"net\", ACTION==\"add\", ATTR{address}==\"c0:ff:ee:0?:0?:0?\", KERNEL==\"eth*\", NAME=\"eth1\"" > /etc/udev/rules.d/70-network.rules

%end

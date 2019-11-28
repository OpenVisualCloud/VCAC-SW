# !/bin/bash

image_proxy="http://child-prc.intel.com:913"
image_proxy_cmd="RUN echo 'Acquire::http::proxy "\"$image_proxy"\";Acquire::https::proxy "\"$image_proxy"\";' > /etc/apt/apt.conf;"
openvino_name="l_openvino_toolkit_p_2019.3.334"
ov_link="http://registrationcenter-download.intel.com/akdlm/irc_nas/15944/$openvino_name.tgz"
py_ver='3.7.2'
py_link="https://www.python.org/ftp/python/$py_ver/Python-$py_ver.tgz"
pack_dir="$( cd "$(dirname "$0")" && pwd )"

print_help()
{
	echo "OVC HDDL Install Release shell script usage:"
	echo "  --proxy=proxy  		set proxy for docker"
	echo "  --ov_link=ov_link           openvino download link default is $ov_link "
}

parse_arg()
{
	rls_vsn=`echo ${item##*=} | tr 'A'-'Z' 'a'-'z'`
	for param in $@; do
		if [[ $param == --image_proxy=* ]]; then
			image_proxy=${param##*=}
		elif [[ $param == --ov_url=* ]]; then
			ov_url=${param##*=}
		elif [[ $param == --py_ver=* ]]; then
			py_ver=${param##*=}
		elif [[ $param == --docker_proxy=* ]]; then
			docker_proxy=${param##*=}
		elif [[ $param == --dns=* ]]; then
			dns=${param##*=}
		else
			echo "[Error], Unknown parameter $param."
			print_help
			exit 
		fi
	done
}
install_depen()
{
    export LC_ALL=C
    apt-get update && apt-get install -y libjson-c2 cmake libelf-dev libpython2.7 libboost-filesystem1.58 nasm libboost-thread1.58 libboost-program-options1.58 libusb-dev cron python-pip build-essential curl wget libssl-dev ca-certificates git libboost-all-dev gcc-multilib g++-multilib libgtk2.0-dev pkg-config libpng12-dev libcairo2-dev libpango1.0-dev libglib2.0-dev libgstreamer0.10-dev libusb-1.0-0-dev i2c-tools libgstreamer-plugins-base1.0-dev libavformat-dev libavcodec-dev libswscale-dev libgstreamer1.0-dev libusb-1.0-0-dev i2c-tools libjson-c-dev usbutils ocl-icd-libopencl*  ocl-icd-opencl-dev libsm6-dbg/xenial libxrender-dev/xenial 

}
install_docker_engine()
{
	check_docker=`docker images`
	if [ $? != 0 ];then
		echo "Installing docker......" 
		echo "You can manually install docker, refer to https://docs.docker.com/install/linux/docker-ce/ubuntu/ for detials....." 
		apt-get install -y apt-transport-https ca-certificates curl gnupg-agent software-properties-common
		if [ $? != 0 ];then
			echo "[Error] Failed to install apt-transport-https ca-certificates curl gnupg-agent software-properties-common......" 
			echo "You can manually install docker, refer to https://docs.docker.com/install/linux/docker-ce/ubuntu/ for detials....." 
			exit 1
		fi
		curl -fsSL https://download.docker.com/linux/ubuntu/gpg | apt-key add -
		if [ $? !=0 ];then
			echo "[Error] Failed to add Docker’s official GPG key..." 
			echo "You can manually install docker, refer to https://docs.docker.com/install/linux/docker-ce/ubuntu/ for detials....." 
			exit 1
		fi
		add-apt-repository "deb [arch=amd64] https://download.docker.com/linux/ubuntu $(lsb_release -cs) stable" 
		if [ $? !=0 ];then
			echo "[Error] Failed to add repository..." 
			echo "You can manually install docker, refer to https://docs.docker.com/install/linux/docker-ce/ubuntu/ for detials....." 
			exit 1
		fi
		apt-get install -y docker-ce docker-ce-cli containerd.io
		if [ $? !=0 ];then
			echo "[Error] Failed to install docker ce..." 
			echo "You can manually install docker, refer to https://docs.docker.com/install/linux/docker-ce/ubuntu/ for detials....." 
			exit 1
		fi
	else
		echo "Docker has been installed. Skip docker installation....." 
	fi
	
	local docker_service_dir=/etc/systemd/system/docker.service.d
	local docker_service_proxy=$docker_service_dir/http-proxy.conf
	mkdir -p $docker_service_dir
	
	echo "Start to create http-proxy.conf" 
	if [ -f $docker_service_proxy ]; then 
		mv $docker_service_proxy $docker_service_dir/http-proxy_bk.conf
	else
		mkdir ~/.docker -p
	fi
	echo "[Service]" >> $docker_service_proxy
	echo "Environment=\"HTTP_PROXY=$image_proxy\" \"HTTPS_PROXY=$image_proxy\"" >> $docker_service_proxy
	systemctl daemon-reload

	systemctl restart docker


	echo "Start to create config.json" 
	local file=~/.docker/config.json
	if [ -f $file ]; then 
		mv ~/.docker/config.json ~/.docker/config_bk.json
	else
		mkdir ~/.docker -p
	fi
	
	echo "{" >> $file
	echo " \"proxies\":" >> $file
	echo "  {" >> $file
	echo "   \"default\":" >> $file
	echo "   {" >> $file
	echo "     \"httpProxy\": \"$image_proxy\", " >> $file 
	echo "     \"httpsProxy\": \"$image_proxy\", " >> $file
	echo "     \"noProxy\": \"$image_proxy\"" >> $file
	echo "   }" >> $file
	echo " }" >> $file
	echo "}" >> $file
	local dnss=`nmcli dev show | grep IP4.DNS`
        if [ $? == 0 ];then
	   local dns_file=/etc/docker/daemon.json
	   if [ -f $file ]; then 
		mv $dns_file /etc/docker/daemon_bk.json
	   fi
	   local dns_write=''
	   for dns in $dnss;
	   do
		for dn in $dns;
		do
			if  [[ $dn =~ ^[0-9] ]];then
			dns_write+="\"$dn\","
			fi
		done
	   done
	   dns_write=${dns_write%,*}
	   echo "{" >> $dns_file
	   echo "	\"dns\": [ $dns_write ]" >> $dns_file
	   echo "}" >> $dns_file
	   service docker restart
        fi
	
}
install_hddl_package()
{
	gen_Dockerfile

	docker build -t ov_hddl:v1 .
	docker run ov_hddl:v1
	local container_id=`docker ps -a |grep ov_hddl:v1 | awk '{ print $1 }'`
	for cont_id in $container_id;
	do
		container_id=$cont_id
		break
	done
	docker cp $container_id:/root/package/intel-vcaa-hddl.tar.gz $pack_dir
	docker cp  $container_id:/root/package/ov_ver.log $pack_dir
	ov_ver=`cat $pack_dir/ov_ver.log`
	rm -rf $pack_dir/ov_ver.log
	mkdir -p /opt/intel/
	tar zxf $pack_dir/intel-vcaa-hddl.tar.gz -C /opt/intel/
	mv /opt/intel/openvino /opt/intel/$ov_ver
	cd /opt/intel/$ov_ver/deployment_tools/inference_engine/external/hddl/drivers/drv_ion 
	make && make install
	cd /opt/intel/$ov_ver/deployment_tools/inference_engine/external/hddl/drivers/drv_vsc 
	make && make install
	rm_docker_image
}
gen_Dockerfile()
{
	cd $pack_dir
	echo "FROM ubuntu:16.04" > Dockerfile
	echo "$image_proxy_cmd" >> Dockerfile
	echo "RUN apt-get update &&  apt-get install -y -q  apt-utils lsb-release vim net-tools bzip2 wget curl git gcc g++ automake libtool pkg-config autoconf make cmake cmake-curses-gui gcc-multilib g++-multilib libboost-dev libssl-dev build-essential" >> Dockerfile
	echo "RUN apt-get install -y  build-essential libncursesw5-dev libgdbm-dev libc6-dev zlib1g-dev libsqlite3-dev tk-dev libssl-dev openssl libffi-dev" >> Dockerfile
	echo "RUN mkdir -p /root/package" >> Dockerfile
	echo "RUN wget -P /root/package $py_link" >> Dockerfile
	echo "RUN wget -P /root/package $ov_link" >> Dockerfile
	
	echo "#install python3.7" >> Dockerfile
	echo "RUN cd /root/package && \\" >> Dockerfile
	echo "  tar xzf Python-$py_ver.tgz && \\" >> Dockerfile
	echo "  cd Python-$py_ver && \\" >> Dockerfile
	echo "  ./configure && \\" >> Dockerfile
	echo "  make && make install" >> Dockerfile
	echo "#install openvino" >> Dockerfile
	echo "RUN cd /root/package && \\" >> Dockerfile
	echo "  tar xzf $openvino_name.tgz && \\" >> Dockerfile
	echo "  apt-get install -y cpio && \\" >> Dockerfile
	echo "  cd $openvino_name && \\" >> Dockerfile
	echo "  sed -i "s/ACCEPT_EULA=decline/ACCEPT_EULA=accept/" silent.cfg && \\" >> Dockerfile
	echo "  bash install.sh --ignore-signature --cli-mode -s silent.cfg" >> Dockerfile
	echo "RUN cd /opt/intel/openvino/deployment_tools/tools/deployment_manager && \\" >> Dockerfile
	echo "  python3 deployment_manager.py --targets=hddl --output_dir=/root/package --archive_name=intel-vcaa-hddl" >> Dockerfile
	echo "RUN ls /opt/intel | grep  openvino_ > /root/package/ov_ver.log" >> Dockerfile
	chmod 777 Dockerfile

}
rm_docker_image()
{

	docker_imag_id=`docker images -a |grep ov_hddl | awk '{ print $3 }'`
	docker rmi -f $docker_imag_id

}
main()
{
	parse_arg $@
        install_depen
	install_docker_engine
	install_hddl_package
	
}

main $@


FROM ubuntu:16.04

RUN if [ -n "$HTTP_PROXY" ] && [ -n "$HTTPS_PROXY" ]; then echo "Acquire {HTTP::proxy \"$HTTP_PROXY\";HTTPS::proxy \"$HTTPS_PROXY\";}" > /etc/apt/apt.conf; fi

RUN apt-get update && apt-get install -y \
    bison \
    build-essential \
    ccache \
    checkinstall \
    debootstrap \
    dosfstools \
    fakeroot \
    flex \
    git \
    libelf-dev \
    libncurses5-dev \
    libssl-dev \
    keyutils \
    kernel-package \
    sudo \
    wget

RUN if [ -n "$HTTP_PROXY" ] && [ -n "$HTTPS_PROXY" ]; then echo "http_proxy=$HTTP_PROXY" > /etc/wgetrc && echo "https_proxy=$HTTPS_PROXY" >> /etc/wgetrc; fi

RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y \
    dracut

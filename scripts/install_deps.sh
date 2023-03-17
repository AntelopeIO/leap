#!/bin/bash
apt-get update
apt-get update --fix-missing
DEBIAN_FRONTEND='noninteractive'
TZ='Etc/UTC'
apt-get install -y tzdata
apt-get install -y \
    build-essential \
    bzip2 \
    cmake \
    curl \
    file \
    git \
    libbz2-dev \
    libcurl4-openssl-dev \
    libgmp-dev \
    libncurses5 \
    libssl-dev \
    libtinfo-dev \
    libusb-1.0.0-dev \
    libzstd-dev \
    pkg-config \
    python3 \
    time \
    unzip \
    wget \
    zip \
    zlib1g-dev

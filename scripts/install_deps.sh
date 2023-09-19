#!/bin/bash
apt-get update
apt-get update --fix-missing
export DEBIAN_FRONTEND='noninteractive'
export TZ='Etc/UTC'
apt-get install -y \
    build-essential \
    bzip2 \
    cmake \
    curl \
    file \
    git \
    libbz2-dev \
    libcurl4-openssl-dev \
    libncurses5 \
    libtinfo-dev \
    libzstd-dev \
    python3 \
    python3-numpy \
    time \
    tzdata \
    unzip \
    wget \
    zip \
    zlib1g-dev

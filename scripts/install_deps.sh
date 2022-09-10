#!/usr/bin/env bash

apt-get update
apt-get update --fix-missing
DEBIAN_FRONTEND=noninteractive TZ=Etc/UTC apt-get -y install tzdata
apt-get -y install zip unzip libncurses5 wget git build-essential cmake curl libcurl4-openssl-dev libgmp-dev libssl-dev libusb-1.0.0-dev libzstd-dev time pkg-config zlib1g-dev libtinfo-dev bzip2 libbz2-dev python3 file

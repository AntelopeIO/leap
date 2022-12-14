#!/usr/bin/env bash

apt-get update
apt-get update --fix-missing
DEBIAN_FRONTEND=noninteractive TZ=Etc/UTC apt-get -y install tzdata
apt-get -y install zip unzip libncurses5 wget git build-essential cmake curl libgmp-dev libssl-dev libzstd-dev time zlib1g-dev libtinfo-dev bzip2 libbz2-dev python3 file

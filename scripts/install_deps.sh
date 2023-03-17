#!/bin/bash
apt-get update
apt-get update --fix-missing
DEBIAN_FRONTEND='noninteractive'
TZ='Etc/UTC'
apt-get install -y tzdata
apt-get install -y zip unzip libncurses5 wget git build-essential cmake curl libgmp-dev libssl-dev libzstd-dev time zlib1g-dev libtinfo-dev bzip2 libbz2-dev python3 python3-numpy file

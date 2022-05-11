#! /usr/bin/bash

apt-get update
apt-get update --fix-missing
DEBIAN_FRONTEND=noninteractive TZ=Etc/UTC apt-get -y install tzdata
apt-get -y install zip unzip libncurses5 wget git build-essential cmake curl libcurl4-openssl-dev libgmp-dev libssl-dev libusb-1.0.0-dev libzstd-dev time pkg-config

# download libpq
if [ ! -d /usr/include/postgresql ]; then
   bash -c 'source /etc/os-release; echo "deb http://apt.postgresql.org/pub/repos/apt ${VERSION_CODENAME}-pgdg main" > /etc/apt/sources.list.d/pgdg.list && \
	   curl -sL https://www.postgresql.org/media/keys/ACCC4CF8.asc | apt-key add - && \
	   apt-get update && apt-get -y install libpq-dev'
fi

FROM ubuntu:focal
ENV TZ="America/New_York"
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get upgrade -y && \
    apt-get install -y build-essential      \
                       g++-10               \
                       cmake                \
                       git                  \
                       jq                   \
                       libcurl4-openssl-dev \
                       libgmp-dev           \
                       llvm-11-dev          \
                       ninja-build          \
                       python3-numpy        \
                       file                 \
                       zlib1g-dev           \
                       zstd &&              \
     update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-10 100 --slave \
                                   /usr/bin/g++ g++ /usr/bin/g++-10 --slave \
                                   /usr/bin/gcov gcov /usr/bin/gcov-10 

         

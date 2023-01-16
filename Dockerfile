FROM ubuntu:20.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && \
    apt-get install -y  \
        build-essential             \
        git                         \
        cmake                       \
        curl                        \
        libboost-all-dev            \
        libcurl4-openssl-dev        \
        libgmp-dev                  \
        libssl-dev                  \
        libusb-1.0-0-dev            \
        llvm-11-dev                 \
        pkg-config

WORKDIR /root

COPY . /root/leap

WORKDIR /root/leap

RUN git submodule update --init --recursive

RUN mkdir build

WORKDIR /root/leap/build

RUN cmake -DCMAKE_BUILD_TYPE=Release ..

RUN make -j 6 package

 RUN curl -L -O https://github.com/AntelopeIO/cdt/releases/download/v3.1.0/cdt_3.1.0_amd64.deb && \
     dpkg -i \
         cdt_3.1.0_amd64.deb \
         leap_3.2.0-ubuntu20.04_amd64.deb \
         leap-dev_3.2.0-ubuntu20.04_amd64.deb


RUN mkdir /root/target

WORKDIR /root/target

ENV PATH $PATH:/usr/opt/cdt/3.0.1/bin

WORKDIR /root/leap/tests/wasm_subst

RUN cmake . && make

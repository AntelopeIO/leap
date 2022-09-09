FROM ubuntu:jammy

RUN apt-get update && apt-get upgrade -y && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y build-essential      \
                                                      cmake                \
                                                      curl                 \
                                                      git                  \
                                                      jq                   \
                                                      libboost-all-dev     \
                                                      libcurl4-openssl-dev \
                                                      libgmp-dev           \
                                                      libssl-dev           \
                                                      libusb-1.0-0-dev     \
                                                      llvm-11-dev          \
                                                      ninja-build          \
                                                      pkg-config           \
                                                      zstd

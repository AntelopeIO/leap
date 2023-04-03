FROM ubuntu:jammy

RUN apt-get update && apt-get upgrade -y && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y build-essential      \
                                                      cmake                \
                                                      git                  \
                                                      jq                   \
                                                      libboost-all-dev     \
                                                      libcurl4-openssl-dev \
                                                      libgmp-dev           \
                                                      libssl-dev           \
                                                      llvm-11-dev          \
                                                      ninja-build          \
                                                      python3-numpy        \
                                                      zstd

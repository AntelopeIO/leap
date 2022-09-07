FROM ubuntu:focal

RUN apt-get update && apt-get upgrade -y && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y build-essential      \
                                                      cmake                \
                                                      curl                 \
                                                      git                  \
                                                      jq                   \
                                                      libboost-all-dev     \
                                                      libgmp-dev           \
                                                      libssl-dev           \
                                                      llvm-11-dev          \
                                                      ninja-build          \
                                                      zstd

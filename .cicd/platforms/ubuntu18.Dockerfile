FROM ubuntu:bionic

RUN apt-get update && apt-get upgrade -y && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y build-essential             \
                                                      cmake                       \
                                                      curl                        \
                                                      g++-8                       \
                                                      git                         \
                                                      jq                          \
                                                      libcurl4-openssl-dev        \
                                                      libgmp-dev                  \
                                                      libssl-dev                  \
                                                      libusb-1.0-0-dev            \
                                                      llvm-7-dev                  \
                                                      ninja-build                 \
                                                      pkg-config                  \
                                                      python3                     \
                                                      software-properties-common  \
                                                      zlib1g-dev                  \
                                                      zstd

# GitHub's actions/checkout requires git 2.18+ but Ubuntu 18 only provides 2.17
RUN add-apt-repository ppa:git-core/ppa && apt update && apt install -y git

# Leap requires boost 1.67+ but Ubuntu 18 only provides 1.65
# Probably need 1.70+ to work properly with old cmake provided in Ubuntu 18
RUN curl -L https://boostorg.jfrog.io/artifactory/main/release/1.79.0/source/boost_1_79_0.tar.bz2 | tar jx && \
   cd boost_* &&                                                                                              \
   ./bootstrap.sh --prefix=/boost &&                                                                          \
   ./b2 --with-iostreams --with-date_time --with-filesystem --with-system                                     \
        --with-program_options --with-chrono --with-test -j$(nproc) install &&                                \
   cd .. &&                                                                                                   \
   rm -rf boost_*

ENV CC=gcc-8
ENV CXX=g++-8
ENV BOOST_ROOT=/boost
ENV LLVM_DIR=/usr/lib/llvm-7/
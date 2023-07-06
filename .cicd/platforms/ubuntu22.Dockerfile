FROM ubuntu:jammy
ENV TZ="America/New_York"
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get upgrade -y && \
    apt-get install -y build-essential      \
                       cmake                \
                       git                  \
                       jq                   \
                       libcurl4-openssl-dev \
                       libgmp-dev           \
                       libssl-dev           \
                       llvm-11-dev          \
                       ninja-build          \
                       python3-numpy        \
                       zlib1g-dev           \
                       zstd

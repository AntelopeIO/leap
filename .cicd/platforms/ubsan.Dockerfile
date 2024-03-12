# syntax=docker/dockerfile:1
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
                       llvm-11-dev          \
                       lsb-release          \
                       ninja-build          \
                       python3-numpy        \
                       software-properties-common \
                       file                 \
                       wget                 \
                       zlib1g-dev           \
                       zstd

RUN yes | bash -c "$(wget -O - https://apt.llvm.org/llvm.sh)" llvm.sh 18

#make sure no confusion on what llvm library leap's cmake should pick up on
RUN rm -rf /usr/lib/llvm-18/lib/cmake

COPY <<-EOF /ubsan.supp
  vptr:wasm_eosio_validation.hpp
  vptr:wasm_eosio_injection.hpp
EOF

ENV LEAP_PLATFORM_HAS_EXTRAS_CMAKE=1
COPY <<-EOF /extras.cmake
  set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "" FORCE)

  set(CMAKE_C_COMPILER "clang-18" CACHE STRING "")
  set(CMAKE_CXX_COMPILER "clang++-18" CACHE STRING "")
  set(CMAKE_C_FLAGS "-fsanitize=undefined -fno-sanitize-recover=all -fno-omit-frame-pointer" CACHE STRING "")
  set(CMAKE_CXX_FLAGS "-fsanitize=undefined -fno-sanitize-recover=all -fno-omit-frame-pointer" CACHE STRING "")
EOF

ENV UBSAN_OPTIONS=print_stacktrace=1,suppressions=/ubsan.supp

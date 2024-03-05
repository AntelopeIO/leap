# syntax=docker/dockerfile:1
#the exact version of Ubuntu doesn't matter for the purpose of asserton builds. Feel free to upgrade in future
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
                       ninja-build          \
                       python3-numpy        \
                       file                 \
                       zlib1g-dev           \
                       zstd

ENV LEAP_PLATFORM_HAS_EXTRAS_CMAKE=1
COPY <<-EOF /extras.cmake
  # reset the build type to empty to disable any cmake default flags
  set(CMAKE_BUILD_TYPE "" CACHE STRING "" FORCE)

  set(CMAKE_C_FLAGS "-O3" CACHE STRING "")
  set(CMAKE_CXX_FLAGS "-O3" CACHE STRING "")

  set(LEAP_ENABLE_RELEASE_BUILD_TEST "Off" CACHE BOOL "")
EOF

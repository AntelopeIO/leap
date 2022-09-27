---
content_title: Build Antelope from Source
---

The shell scripts previously recommended for building the software have been removed in favor of a build process entirely driven by CMake. Those wishing to build from source are now responsible for installing the necessary dependencies. The list of dependencies and the recommended build procedure are in the README.md file. Instructions are also included for efficiently running the tests.

### Using DUNE

As an alternative to building from source try [Docker Utilities for Node Execution](https://github.com/AntelopeIO/DUNE) for the easiest way to get started, and for multi-platform support.  

### Building From Source

Recent Ubuntu LTS releases are the only Linux distributions that we fully support. Other Linux distros and other POSIX operating systems (such as macOS) are tended to on a best-effort basis and may not be full featured. Notable requirements to build are:
* C++17 compiler and standard library
* boost 1.67+
* CMake 3.8+
* (for Linux only) LLVM 7 - 11 (newer versions do not work)

A few other common libraries are tools also required such as openssl 1.1+, libcurl, curl, libusb, GMP, Python 3, and zlib.

**A Warning On Parallel Compilation Jobs (`-j` flag)**: When building C/C++ software often the build is performed in parallel via a command such as `make -j $(nproc)` which uses the number of CPU cores as the number of compilation jobs to perform simultaneously. However, be aware that some compilation units (.cpp files) in mandel are extremely complex and will consume nearly 4GB of memory to compile. You may need to reduce the level of parallelization depending on the amount of memory on your build host. e.g. instead of `make -j $(nproc)` run `make -j2`. Failures due to memory exhaustion will typically but not always manifest as compiler crashes.

Generally we recommend performing what we refer to as a "pinned build" which ensures the compiler and boost version remain the same between builds of different mandel versions (mandel requires these versions remain the same otherwise its state needs to be repopulated from a portable snapshot).

#### Building Pinned Build Binary Packages
In the directory `<mandel src>/scripts` you will find the two scripts `install_deps.sh` and `pinned_build.sh`. If you haven't installed build dependencies then run `install_deps.sh`. Then run `pinned_build.sh <dependencies directory> <mandel build directory> <number of jobs>`.

The dependencies directory is where the script will pull the C++ dependencies that need to be built with the pinned compiler for building the pinned binaries for binary packaging.

The binary package will be produced in the mandel build directory that was supplied.

#### Manual (non "pinned") Build Instructions

<details>
  <summary>Ubuntu 20.04 & 22.04 Build Instructions</summary>

Install required dependencies:
```
apt-get update && apt-get install   \
        build-essential             \
        cmake                       \
        curl                        \
        git                         \
        libboost-all-dev            \
        libcurl4-openssl-dev        \
        libgmp-dev                  \
        libssl-dev                  \
        libusb-1.0-0-dev            \
        llvm-11-dev                 \
        pkg-config
```
and perform the build:
```
git submodule update --init --recursive
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j $(nproc) package
```
</details>

<details>
  <summary>Ubuntu 18.04 Build Instructions</summary>

Install required dependencies. You will need to build Boost from source on this distribution.
```
apt-get update && apt-get install   \
        build-essential             \
        cmake                       \
        curl                        \
        g++-8                       \
        git                         \
        libcurl4-openssl-dev        \
        libgmp-dev                  \
        libssl-dev                  \
        libusb-1.0-0-dev            \
        llvm-7-dev                  \
        pkg-config                  \
        python3                     \
        zlib1g-dev

curl -L https://boostorg.jfrog.io/artifactory/main/release/1.79.0/source/boost_1_79_0.tar.bz2 | tar jx && \
   cd boost_1_79_0 &&                                                                                     \
   ./bootstrap.sh --prefix=$HOME/boost1.79 &&                                                             \
   ./b2 --with-iostreams --with-date_time --with-filesystem --with-system                                 \
        --with-program_options --with-chrono --with-test -j$(nproc) install &&                            \
   cd ..
```
and perform the build:
```
git submodule update --init --recursive
mkdir build
cd build
cmake -DCMAKE_C_COMPILER=gcc-8 -DCMAKE_CXX_COMPILER=g++-8 \
      -DCMAKE_PREFIX_PATH="$HOME/boost1.79;/usr/lib/llvm-7/"  -DCMAKE_BUILD_TYPE=Release .. \
make -j $(nproc) package
```
After building you may remove the `$HOME/boost1.79` directory, or you may keep it around until next time building the software.
</details>

### Running Tests

When building from source it's recommended to run at least what we refer to as the "parallelizable tests". Not included by default in the "parallelizable tests" are the WASM spec tests which can add additional coverage and can also be run in parallel.

```
cd build

# "parallelizable tests": the minimum test set that should be run
ctest -j $(nproc) -LE _tests

# Also consider running the WASM spec tests for more coverage
ctest -j $(nproc) -L wasm_spec_tests
```

Some other tests are available and recommended but be aware they can be sensitive to other software running on the same host and they may **SIGKILL** other nodeos instances running on the host.
```
cd build

# These tests can't run in parallel but are recommended.
ctest -L "nonparallelizable_tests"

# These tests can't run in parallel. They also take a long time to run.
ctest -L "long_running_tests"
```

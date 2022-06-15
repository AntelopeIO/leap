# Mandel

Home of the official [EOS Network Foundation](https://eosnetwork.com/) blockchain node software.

## Repo organization

| branch                | description |
| ------                | ----------- |
| `main`                | Development for future releases |
| `release/3.0.x`       | 3.0.x-* series of pre-releases before 3.1.x |
| `release/3.1.x`       | 3.1.x* series of 3.1.0 releases |

## Supported Operating Systems

To speed up development, we're starting with **Ubuntu 20.04**. We'll support additional operating systems as time and personnel allow. In the mean time, they may break.

## Building

To speed up development and reduce support overhead, we're initially only supporting the following build approach. CMake options not listed here may break. Build scripts may break. Dockerfiles may break.

### Ubuntu 20.04 dependencies

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
        libtinfo5                   \
        libusb-1.0-0-dev            \
        libzstd-dev                 \
        llvm-11-dev                 \
        ninja-build                 \
        pkg-config                  \
        time
```

### Building

```
git submodule update --init --recursive
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j $(nproc)
```

We support the following CMake options:
```
-DCMAKE_CXX_COMPILER_LAUNCHER=ccache    Speed up builds
-DCMAKE_C_COMPILER_LAUNCHER=ccache      Speed up builds
-DCMAKE_BUILD_TYPE=DEBUG                Debug builds
-DDISABLE_WASM_SPEC_TESTS=yes           Speed up builds and skip many tests
-DCMAKE_INSTALL_PREFIX=/foo/bar         Where to install to
-DENABLE_OC=no                          Disable OC support; useful when this repo is used
                                        as a library
-GNinja                                 Use ninja instead of make
                                        (faster on high-core-count machines)
```

I highly recommend the ccache options. They don't speed up the first clean build, but they speed up future clean builds after the first build.

### Running tests

```
cd build
npm install

# Runs parallelizable tests in parallel. This runs much faster when
# -DDISABLE_WASM_SPEC_TESTS=yes is used.
ctest -j $(nproc) -LE "nonparallelizable_tests|long_running_tests" -E "full-version-label-test|release-build-test|print-build-info-test"

# These tests can't run in parallel.
ctest -L "nonparallelizable_tests"

# These tests can't run in parallel. They also take a long time to run.
ctest -L "long_running_tests"
```

### Building Pinned Build Binary Packages
In the directory `<mandel src>/scripts` you will find the two scripts `install_deps.sh` and `pinned_build.sh`.  These are designed currently to run on Ubuntu 18/20/22.

If you haven't installed build dependencies then run `install_deps.sh`.
Then run `pinned_build.sh <dependencies directory> <mandel build directory> <number of jobs>`.

The dependencies directory is where the script will pull the C++ dependencies that need to be built with the pinned compiler for building the pinned binaries for binary packaging.

The binary package will be produced in the mandel build directory that was supplied.

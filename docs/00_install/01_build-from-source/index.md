---
content_title: Build Mandel from Source
---

The shell scripts previously recommended for building the software have been removed in favor of a build process entirely driven by CMake. Those wishing to build from source are now responsible for installing the necessary dependencies. The list of dependencies and the recommended build procedure are in the README.md file. Instructions are also included for efficiently running the tests.

## Building Mandel

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

# Runs parallelizable tests in parallel. This runs much faster when
# -DDISABLE_WASM_SPEC_TESTS=yes is used.
ctest -j $(nproc) -LE "nonparallelizable_tests|long_running_tests" -E "full-version-label-test|release-build-test|print-build-info-test"

# These tests can't run in parallel.
ctest -L "nonparallelizable_tests"

# These tests can't run in parallel. They also take a long time to run.
ctest -L "long_running_tests"
```

## Other Compilers

To override `clang`'s default compiler toolchain, add these flags to the `cmake` command within the above instructions:

`-DCMAKE_CXX_COMPILER=/path/to/c++ -DCMAKE_C_COMPILER=/path/to/cc`

## Debug Builds

For a debug build, add `-DCMAKE_BUILD_TYPE=Debug`. Other common build types include `Release` and `RelWithDebInfo`.

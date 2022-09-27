---
content_title: Build Antelope from Source on Other Unix-based OS
---

**Please keep in mind that instructions for building from source on other  unsupported operating systems provided here should be considered experimental and provided AS-IS on a best-effort basis and may not be fully featured.**

### Using DUNE

For the official multi-platform support try [Docker Utilities for Node Execution](https://github.com/AntelopeIO/DUNE) which runs in an ubuntu image via a docker container.  

**A Warning On Parallel Compilation Jobs (`-j` flag)**: When building C/C++ software often the build is performed in parallel via a command such as `make -j $(nproc)` which uses the number of CPU cores as the number of compilation jobs to perform simultaneously. However, be aware that some compilation units (.cpp files) in mandel are extremely complex and will consume nearly 4GB of memory to compile. You may need to reduce the level of parallelization depending on the amount of memory on your build host. e.g. instead of `make -j $(nproc)` run `make -j2`. Failures due to memory exhaustion will typically but not always manifest as compiler crashes.

Generally we recommend performing what we refer to as a "pinned build" which ensures the compiler and boost version remain the same between builds of different mandel versions (mandel requires these versions remain the same otherwise its state needs to be repopulated from a portable snapshot).

<details>
  <summary>FreeBSD 13.1 Build Instructions</summary>

Install required dependencies:
```
pkg update && pkg install   \
    git                     \
    cmake                   \
    curl                    \
    boost-all               \
    python3                 \
    openssl                 \
    llvm11                  \
    pkgconf
```
and perform the build (please note that FreeBSD 13.1 comes with llvm13 by default so you should provide clang11 options to cmake):
```
git submodule update --init --recursive
mkdir build
cd build
cmake -DCMAKE_CXX_COMPILER=clang++11 -DCMAKE_C_COMPILER=clang11 -DCMAKE_BUILD_TYPE=Release ..
make -j $(nproc) package
```
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

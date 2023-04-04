# Leap
Leap is a C++ implementation of the [Antelope](https://github.com/AntelopeIO) protocol. It contains blockchain node software and supporting tools for developers and node operators.

## Branches
The `main` branch is the development branch; do not use it for production. Refer to the [release page](https://github.com/AntelopeIO/leap/releases) for current information on releases, pre-releases, and obsolete releases, as well as the corresponding tags for those releases.

## Supported Operating Systems
We currently support the following operating systems.
- Ubuntu 22.04 Jammy
- Ubuntu 20.04 Focal

Other Unix derivatives such as macOS are tended to on a best-effort basis and may not be full featured. If you aren't using Ubuntu, please visit the "[Build Unsupported OS](./docs/00_install/01_build-from-source/00_build-unsupported-os.md)" page to explore your options.

If you are running an unsupported Ubuntu derivative, such as Linux Mint, you can find the version of Ubuntu your distribution was based on by using this command:
```bash
cat /etc/upstream-release/lsb-release
```
Your best bet is to follow the instructions for your Ubuntu base, but we make no guarantees.

## Binary Installation
This is the fastest way to get started. From the [latest release](https://github.com/AntelopeIO/leap/releases/latest) page, download a binary for one of our [supported operating systems](#supported-operating-systems), or visit the [release tags](https://github.com/AntelopeIO/leap/releases) page to download a binary for a specific version of Leap.

Once you have a `*.deb` file downloaded for your version of Ubuntu, you can install it as follows:
```bash
sudo apt-get update
sudo apt-get install -y ~/Downloads/leap*.deb
```
Your download path may vary. If you are in an Ubuntu docker container, omit `sudo` because you run as `root` by default.

Finally, verify Leap was installed correctly:
```bash
nodeos --full-version
```
You should see a [semantic version](https://semver.org) string followed by a `git` commit hash with no errors. For example:
```
v3.1.2-0b64f879e3ebe2e4df09d2e62f1fc164cc1125d1
```

## Build and Install from Source
You can also build and install Leap from source.

### Prerequisites
You will need to build on a [supported operating system](#supported-operating-systems).

Requirements to build:
- C++17 compiler and standard library
- boost 1.67+
- CMake 3.16+
- LLVM 7 - 11 - for Linux only
  - newer versions do not work
- openssl 1.1+
- libcurl 7.40.0+
- git
- GMP
- Python 3
- python3-numpy
- zlib

### Step 1 - Clone
If you don't have the Leap repo cloned to your computer yet, [open a terminal](https://itsfoss.com/open-terminal-ubuntu) and navigate to the folder where you want to clone the Leap repository:
```bash
cd ~/Downloads
```
Clone Leap using either HTTPS...
```bash
git clone --recursive https://github.com/AntelopeIO/leap.git
```
...or SSH:
```bash
git clone --recursive git@github.com:AntelopeIO/leap.git
```

> ℹ️ **HTTPS vs. SSH Clone** ℹ️  
Both an HTTPS or SSH git clone will yield the same result - a folder named `leap` containing our source code. It doesn't matter which type you use.

Navigate into that folder:
```bash
cd leap
```

### Step 2 - Checkout Release Tag or Branch
Choose which [release](https://github.com/AntelopeIO/leap/releases) or [branch](#branches) you would like to build, then check it out. If you are not sure, use the [latest release](https://github.com/AntelopeIO/leap/releases/latest). For example, if you want to build release 3.1.2 then you would check it out using its tag, `v3.1.2`. In the example below, replace `v0.0.0` with your selected release tag accordingly:
```bash
git fetch --all --tags
git checkout v0.0.0
```

Once you are on the branch or release tag you want to build, make sure everything is up-to-date:
```bash
git pull
git submodule update --init --recursive
```

### Step 3 - Build
Select build instructions below for a [pinned build](#pinned-build) (preferred) or an [unpinned build](#unpinned-build).

> ℹ️ **Pinned vs. Unpinned Build** ℹ️  
We have two types of builds for Leap: "pinned" and "unpinned." The only difference is that pinned builds use specific versions for some dependencies hand-picked by the Leap engineers - they are "pinned" to those versions. In contrast, unpinned builds use the default dependency versions available on the build system at the time. We recommend performing a "pinned" build to ensure the compiler and boost versions remain the same between builds of different Leap versions. Leap requires these versions to remain the same, otherwise its state might need to be recovered from a portable snapshot or the chain needs to be replayed.

> ⚠️ **A Warning On Parallel Compilation Jobs (`-j` flag)** ⚠️  
When building C/C++ software, often the build is performed in parallel via a command such as `make -j "$(nproc)"` which uses all available CPU threads. However, be aware that some compilation units (`*.cpp` files) in Leap will consume nearly 4GB of memory. Failures due to memory exhaustion will typically, but not always, manifest as compiler crashes. Using all available CPU threads may also prevent you from doing other things on your computer during compilation. For these reasons, consider reducing this value.

> 🐋 **Docker and `sudo`** 🐋  
If you are in an Ubuntu docker container, omit `sudo` from all commands because you run as `root` by default. Most other docker containers also exclude `sudo`, especially Debian-family containers. If your shell prompt is a hash tag (`#`), omit `sudo`.

#### Pinned Build
Make sure you are in the root of the `leap` repo, then run the `install_depts.sh` script to install dependencies:
```bash
sudo scripts/install_deps.sh
```

Next, run the pinned build script. You have to give it three arguments in the following order:
1. A temporary folder, for all dependencies that need to be built from source.
1. A build folder, where the binaries you need to install will be built to.
1. The number of jobs or CPU cores/threads to use (note the [jobs flag](#step-3---build) warning above).

> 🔒 You do not need to run this script with `sudo` or as root.

For example, the following command runs the `pinned_build.sh` script, specifies a `deps` and `build` folder in the root of the Leap repo for the first two arguments, then builds the packages using all of your computer's CPU threads:
```bash
scripts/pinned_build.sh deps build "$(nproc)"
```
Now you can optionally [test](#step-4---test) your build, or [install](#step-5---install) the `*.deb` binary packages, which will be in the root of your build directory.

#### Unpinned Build
The following instructions are valid for this branch. Other release branches may have different requirements, so ensure you follow the directions in the branch or release you intend to build. If you are in an Ubuntu docker container, omit `sudo` because you run as `root` by default.

Install dependencies:
```bash
sudo apt-get update
sudo apt-get install -y \
        build-essential \
        cmake \
        git \
        libboost-all-dev \
        libcurl4-openssl-dev \
        libgmp-dev \
        libssl-dev \
        llvm-11-dev \
        python3-numpy
```
To build, make sure you are in the root of the `leap` repo, then run the following command:
```bash
mkdir -p build
cd build
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/usr/lib/llvm-11 ..
make -j "$(nproc)" package
```

Now you can optionally [test](#step-4---test) your build, or [install](#step-5---install) the `*.deb` binary packages, which will be in the root of your build directory.

### Step 4 - Test
Leap supports the following test suites:

Test Suite | Test Type | [Test Size](https://testing.googleblog.com/2010/12/test-sizes.html) | Notes
---|:---:|:---:|---
[Parallelizable tests](#parallelizable-tests) | Unit tests | Small
[WASM spec tests](#wasm-spec-tests) | Unit tests | Small | Unit tests for our WASM runtime, each short but _very_ CPU-intensive
[Serial tests](#serial-tests) | Component/Integration | Medium
[Long-running tests](#long-running-tests) | Integration | Medium-to-Large | Tests which take an extraordinarily long amount of time to run

When building from source, we recommended running at least the [parallelizable tests](#parallelizable-tests).

#### Parallelizable Tests
This test suite consists of any test that does not require shared resources, such as file descriptors, specific folders, or ports, and can therefore be run concurrently in different threads without side effects (hence, easily parallelized). These are mostly unit tests and [small tests](https://testing.googleblog.com/2010/12/test-sizes.html) which complete in a short amount of time.

You can invoke them by running `ctest` from a terminal in your Leap build directory and specifying the following arguments:
```bash
ctest -j "$(nproc)" -LE _tests
```

#### WASM Spec Tests
The WASM spec tests verify that our WASM execution engine is compliant with the web assembly standard. These are very [small](https://testing.googleblog.com/2010/12/test-sizes.html), very fast unit tests. However, there are over a thousand of them so the suite can take a little time to run. These tests are extremely CPU-intensive.

You can invoke them by running `ctest` from a terminal in your Leap build directory and specifying the following arguments:
```bash
ctest -j "$(nproc)" -L wasm_spec_tests
```
We have observed severe performance issues when multiple virtual machines are running this test suite on the same physical host at the same time, for example in a CICD system. This can be resolved by disabling hyperthreading on the host.

#### Serial Tests
The serial test suite consists of [medium](https://testing.googleblog.com/2010/12/test-sizes.html) component or integration tests that use specific paths, ports, rely on process names, or similar, and cannot be run concurrently with other tests. Serial tests can be sensitive to other software running on the same host and they may `SIGKILL` other `nodeos` processes. These tests take a moderate amount of time to complete, but we recommend running them.

You can invoke them by running `ctest` from a terminal in your Leap build directory and specifying the following arguments:
```bash
ctest -L "nonparallelizable_tests"
```

#### Long-Running Tests
The long-running tests are [medium-to-large](https://testing.googleblog.com/2010/12/test-sizes.html) integration tests that rely on shared resources and take a very long time to run.

You can invoke them by running `ctest` from a terminal in your Leap build directory and specifying the following arguments:
```bash
ctest -L "long_running_tests"
```

### Step 5 - Install
Once you have [built](#step-3---build-the-source-code) Leap and [tested](#step-4---test) your build, you can install Leap on your system. Don't forget to omit `sudo` if you are running in a docker container.

We recommend installing the binary package you just built. Navigate to your Leap build directory in a terminal and run this command:
```bash
sudo apt-get update
sudo apt-get install -y ./leap[-_][0-9]*.deb
```

It is also possible to install using `make` instead:
```bash
sudo make install
```

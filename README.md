# Leap

Leap is blockchain node software and supporting tools that implements the [Antelope](https://github.com/AntelopeIO) protocol.

## Branches
The `main` branch is the development branch, do not use this for production. Refer to the [release page](https://github.com/AntelopeIO/leap/releases) for current information on releases, pre-releases, and obsolete releases, as well as the corresponding tags for those releases.

## Binary Installation
This is the fastest way to get started. Download a binary for the [latest](https://github.com/AntelopeIO/leap/releases/latest) release, or visit the [releases](https://github.com/AntelopeIO/leap/releases) page to download a binary for a specific version of Leap.

We currently only support Ubuntu. If you aren't using Ubuntu, please visit [this page](./docs/00_install/01_build-from-source/00_build-unsupported-os.md) to explore your options.

You will need to know your version of Ubuntu to download the right binary. You can find this by going to Start/Windows/Super key > Settings > About > OS Name, or by running the following in your terminal.
```bash
lsb_release -a
```
Once you have a `*.deb` file downloaded for your version of Ubuntu, you can install it as follows.
```bash
sudo apt-get install -y ~/Downloads/leap*.deb
```
Your download path may vary.
- If you are running in docker, omit `sudo` as Ubuntu docker containers run as `root` by default.

Finally, verify Leap was installed correctly.
```bash
nodeos --full-version
```
You should see a [semantic version](https://semver.org) string print out followed by a `git` commit hash with no errors. For example:
```
v3.1.2-0b64f879e3ebe2e4df09d2e62f1fc164cc1125d1
```

## Source Installation
You can also build and install Leap from source.

### Prerequisites
Recent Ubuntu LTS releases are the only operating systems that we support. Other Unix derivatives such as macOS are tended to on a best-effort basis and may not be full featured. If you aren't using Ubuntu, please visit [this page](./docs/00_install/01_build-from-source/00_build-unsupported-os.md) to explore your options.

Notable requirements to build are:
- C++17 compiler and standard library
- boost 1.67+
- CMake 3.8+
- LLVM 7 - 11 - for Linux only
  - newer versions do not work
- openssl 1.1+
- libcurl
- curl
- libusb
- git
- GMP
- Python 3
- zlib

If you don't already have the Leap repo cloned to your computer, [open a terminal](https://itsfoss.com/open-terminal-ubuntu) and navigate to the folder you want the Leap source code to be in using `cd`. Clone Leap using HTTPS...
```bash
git clone --recursive https://github.com/AntelopeIO/leap.git
```
...or SSH.
```bash
git clone --recursive git@github.com:AntelopeIO/leap.git
```
They both result in the same thing, a folder named `leap` containg our source code. Navigate into that folder.
```bash
cd leap
```
Choose which [release tag](https://github.com/AntelopeIO/leap/releases) or [branch](#branches) you would like to build and check it out. If you don't know, use the [latest release](https://github.com/AntelopeIO/leap/releases/latest). For example, if you want to build release 3.1.2, you would check it out using its tag.
```bash
git checkout v3.1.2
```

Once you are on the branch or tag (release) you want to build, make sure everything is up-to-date.
```bash
git pull
git submodule update --init --recursive
```

#### Jobs Flag
⚠️ **A Warning On Parallel Compilation Jobs (`-j` flag)** ⚠️
When building C/C++ software, often the build is performed in parallel via a command such as `make -j "$(nproc)"` which uses all available CPU threads. However, be aware that some compilation units (`*.cpp` files) in Leap will consume nearly 4GB of memory to compile. Failures due to memory exhaustion will typically, but not always, manifest as compiler crashes. Using all available CPU threads may also prevent you from doing other things on your computer during compilation. Consider adjusting parallelization for these reasons.

### Build
We have two types of builds for Leap, "pinned" and "unpinned." The only difference is that pinned builds use specific versions for some dependencies hand-picked by Leap engineers, whereas unpinned builds use the default dependency versions available on the build system at the time. We recommend performing a pinned build to ensure the compiler and boost version remain the same between builds of different Leap versions. Leap requires these versions to remain the same, otherwise its state needs to be recovered from a portable snapshot or the chain needs to be replayed.

For the curious, the "pinned" terminology comes from our days developing on macOS where you would run `brew pin` to lock one of the Leap dependencies to a specific version despite available upgrades.

#### Pinned Build
From a terminal in the root of the `leap` repo, run our script to install dependencies.
```bash
sudo scripts/install_deps.sh
```
- If you are in a docker container, omit `sudo` because you run as `root` by default in Ubuntu.

Next, call our pinned build script. You have to give it three arguments, a folder for dependencies, a build folder, and [the number of jobs or CPU cores/threads to use](#jobs-flag). This command uses a `deps` and `build` folder in the root of the Leap repo for the first two arguments, then builds using all but two of your computer's CPU threads.
```bash
scripts/pinned_build.sh deps build "$(( $(nproc) - 2 ))"
```
- You don't need `sudo` for this command.

The binaries will be in the Leap build directory you passed to the build script. For example, the command above leaves them in `build/bin` from the root of the Leap repository.

#### Unpinned Build
These instructions are valid for this branch. Other release branches may have different requirements so ensure you follow the directions in the branch or release you intend to build.

<details> <summary>Ubuntu 22.04 & 20.04</summary>

Install required dependencies: 
```bash
sudo apt-get update
sudo apt-get install \
        build-essential \
        cmake \
        curl \
        git \
        libboost-all-dev \
        libcurl4-openssl-dev \
        libgmp-dev \
        libssl-dev \
        libusb-1.0-0-dev \
        llvm-11-dev \
        pkg-config
```
and perform the build:
```bash
mkdir -p build
cd build
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/usr/lib/llvm-11 ..
make -j "$(( $(nproc) - 2 ))" package
```
</details>

<details> <summary>Ubuntu 18.04</summary>

Install required dependencies.
```bash
sudo apt-get update
sudo apt-get install \
        build-essential \
        cmake \
        curl \
        g++-8 \
        git \
        libcurl4-openssl-dev \
        libgmp-dev \
        libssl-dev \
        libusb-1.0-0-dev \
        llvm-7-dev \
        pkg-config \
        python3 \
        zlib1g-dev
```
You need to build Boost from source on this distribution.
```bash
curl -L https://boostorg.jfrog.io/artifactory/main/release/1.79.0/source/boost_1_79_0.tar.bz2 | tar jx
pushd boost_1_79_0
./bootstrap.sh --prefix="$HOME/boost1.79"
./b2 --with-iostreams --with-date_time --with-filesystem --with-system --with-program_options --with-chrono --with-test -j "$(( $(nproc) - 2 ))" install
popd
```
and perform the build:
```bash
mkdir -p build
cd build
cmake -DCMAKE_C_COMPILER=gcc-8 -DCMAKE_CXX_COMPILER=g++-8 -DCMAKE_PREFIX_PATH="$HOME/boost1.79;/usr/lib/llvm-7/" -DCMAKE_BUILD_TYPE=Release ..
make -j "$(( $(nproc) - 2 ))" package
```
After building, you may remove the `~/boost1.79` directory or you may keep it around for your next build.
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

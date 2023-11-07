# Bios Boot Tutorial

The `bios-boot-tutorial.py` script simulates the bios boot sequence.

## Prerequisites

1. Python 3.x
2. CMake
3. git
4. curl
5. libcurl4-gnutls-dev

## Steps

1. Install the latest [Leap binaries](https://github.com/AntelopeIO/leap/releases) by following the steps provided in the README.

2. Install the latest [CDT binaries](https://github.com/AntelopeIO/cdt/releases) by following the steps provided in the README.

3. Compile the latest [EOS System Contracts](https://github.com/eosnetworkfoundation/eos-system-contracts/releases). Replaces `release/*latest*` with the latest release branch.

```bash
$ cd ~
$ git clone https://github.com/eosnetworkfoundation/eos-system-contracts
$ cd ./eos-system-contracts/
$ git checkout release/*latest*
$ mkdir build
$ cd ./build
$ cmake -DCMAKE_BUILD_TYPE=Release ..
$ make -j $(nproc)
$ cd ./contracts/
$ pwd
```

4. Make note of the path where the contracts were compiled
The last command in the previous step printed the contracts directory. Make note of it; we will reference it from now on as the environment variable `CONTRACTS_DIRECTORY`.

5. Launch the `bios-boot-tutorial.py` script:

```bash
$ pip install numpy
$ cd ~
$ git clone -b release/*latest* https://github.com/AntelopeIO/leap
$ cd ./leap/tutorials/bios-boot-tutorial/
$ python3 bios-boot-tutorial.py --cleos=cleos --nodeos=nodeos --keosd=keosd --contracts-dir="${CONTRACTS_DIRECTORY}" -w -a
```

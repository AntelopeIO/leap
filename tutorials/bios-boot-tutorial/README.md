# Bios Boot Tutorial

The `bios-boot-tutorial.py` script simulates the bios boot sequence.

## Prerequisites

1. Python 3.x
2. CMake
3. git
4. g++
5. build-essentials
6. pip3
7. openssl
8. curl
9. jq
10. psmisc

## Steps

1. Install Leap 5.0 binaries by following the steps provided in the [Leap README](https://github.com/AntelopeIO/leap/tree/release/5.0#software-installation).

2. Install CDT 4.0 binaries by following the steps provided in the [CDT README](https://github.com/AntelopeIO/cdt/tree/release/4.0#binary-releases).

3. Compile EOS System Contracts 3.2:

```bash
$ cd ~
$ git clone https://github.com/eosnetworkfoundation/eos-system-contracts system-contracts-3.2
$ cd ./system-contracts-3.2/
$ git checkout release/3.2
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
$ cd ~
$ git clone https://github.com/AntelopeIO/leap
$ cd ./leap/tutorials/bios-boot-tutorial/
$ python3 bios-boot-tutorial.py --cleos=cleos --nodeos=nodeos --keosd=keosd --contracts-dir="${CONTRACTS_DIRECTORY}" -w -a
```

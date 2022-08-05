# Bios Boot Tutorial

The `bios-boot-tutorial.py` script simulates the bios boot sequence.

``Prerequisites``:

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


``Steps``:

1. Install mandel binaries by following the steps outlined in below tutorial
[Install mandel binaries](https://github.com/eosnetworkfoundation/mandel/releases).

2. Install mandel.cdt version 3.0 binaries by following the steps outlined in below tutorial
[Install mandel.cdt binaries](https://github.com/eosnetworkfoundation/mandel.cdt/releases).

3. [Compile mandel-contracts](https://github.com/eosnetworkfoundation/mandel-contracts#build-system-contracts)

4. Launch the `bios-boot-tutorial.py` script.
The command line to launch the script, make sure you replace `CONTRACTS_DIRECTORY` with the actual directory path.

```bash
$ cd ~
$ git clone https://github.com/eosnetworkfoundation/mandel
$ cd ./mandel/tutorials/bios-boot-tutorial/
$ python3 bios-boot-tutorial.py --cleos=cleos --nodeos=nodeos --keosd=keosd --contracts-dir="CONTRACTS_DIRECTORY" -w -a

```

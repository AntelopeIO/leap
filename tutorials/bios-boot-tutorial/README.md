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

1. Install Leap binaries by following the steps outlined in below tutorial
[Install Leap binaries](https://github.com/AntelopeIO/leap/tree/release/3.0.x#Building).

2. Install cdt version 1.8.1 binaries by following the steps outlined in below tutorial
[Install cdt binaries](https://github.com/AntelopeIO/cdt/tree/v1.8.1#binary-releases).

3. Compile `mandel-contracts` version 3.0.x

```bash
$ cd ~
$ git clone https://github.com/eosnetworkfoundation/mandel-contracts mandel.contracts-3.0.x
$ cd ./mandel.contracts-3.0.x/
$ git checkout release/3.0.x
$ ./build.sh
$ cd ./build/contracts/
$ pwd

```

4. Make note of the directory where the contracts were compiled
The last command in the previous step printed on the bash console the contracts' directory, make note of it, we'll reference it from now on as `CONTRACTS_DIRECTORY`

5. Launch the `bios-boot-tutorial.py` script.
The command line to launch the script, make sure you replace `CONTRACTS_DIRECTORY` with the actual directory path.

```bash
$ cd ~
$ git clone https://github.com/AntelopeIO/leap
$ cd ./leap/tutorials/bios-boot-tutorial/
$ python3 bios-boot-tutorial.py --cleos=cleos --nodeos=nodeos --keosd=keosd --contracts-dir="CONTRACTS_DIRECTORY" -w -a

```

See [Developer Portal: Bios Boot Sequence](https://developers.eos.io/welcome/latest/tutorials/bios-boot-sequence) for additional information.

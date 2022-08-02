#!/bin/bash
# Run from containing folder
# If mandel is made with -DEOSIO_COMPILE_TEST_CONTRACTS=true, the contracts will be compiled with the installed cdt.
# This script will bring back the updated wasms and abi into the source tree, so that they can be added to git,
# in the case of a contract being updated.

for d in */ ; do
   cp ../../build/unittests/test-contracts/$d*.wasm $d
   if test -f ../../build/unittests/test-contracts/$d*.abi; then
      cp ../../build/unittests/test-contracts/$d*.abi $d
   fi
done

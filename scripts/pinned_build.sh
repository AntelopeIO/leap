#! /usr/bin/bash

echo "Mandel Pinned Build"

if [[ $NAME != "Ubuntu" ]]
   then
      echo "Currently only supporting Ubuntu based builds. Proceed at your own risk."
fi

if [ $# -eq 0 ] || [ -z "$1" ]
   then
      echo "Please supply a directory for the build dependencies to be placed and a value for the number of jobs to use for building."
      echo "./pinned_build.sh <directory> 1-100"
      exit -1
fi

CORE_SYM=EOS
DEP_DIR=$1
JOBS=$2
CLANG_VER=11.0.1
BOOST_VER=1.70.0
LLVM_VER=7.1.0
LIBPQXX_VER=7.2.1

pushd $DEP_DIR &> /dev/null
mkdir -p $DEP_DIR

# build Mandel
MANDEL_DIR=${DEP_DIR}/mandel

echo "Building Mandel"
pushd ..
mkdir -p build
pushd build &> /dev/null
cmake -DCMAKE_INSTALL_PREFIX=/usr/local -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=${LLVM_DIR}/lib/cmake -DENABLE_OC=Off..
make -j${JOBS}
cpack

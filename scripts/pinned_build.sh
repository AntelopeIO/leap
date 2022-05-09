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

# install dependencies

# download clang 12.0.1 for usage
CLANG_DIR=${DEP_DIR}/clang-${CLANG_VER}

if [ ! -d "${CLANG_DIR}" ]; then
   echo "Installing Clang ${CLANG_VER} @ ${CLANG_DIR}"
   mkdir -p ${CLANG_DIR}
   wget https://github.com/llvm/llvm-project/releases/download/llvmorg-${CLANG_VER}/clang+llvm-${CLANG_VER}-x86_64-linux-gnu-ubuntu-16.04.tar.xz
   tar -xvf clang+llvm-${CLANG_VER}-x86_64-linux-gnu-ubuntu-16.04.tar.xz -C ${CLANG_DIR}
   pushd ${CLANG_DIR} &> /dev/null
   mv clang+*/* .
   popd &> /dev/null
   rm clang+llvm-${CLANG_VER}-x86_64-linux-gnu-ubuntu-16.04.tar.xz
fi

export PATH=${CLANG_DIR}/bin:$PATH

# download boost and build boost
BOOST_DIR=${DEP_DIR}/boost_${BOOST_VER//\./_}

if [ ! -d "${BOOST_DIR}" ]; then
   echo "Installing Boost ${BOOST_VER} @ ${BOOST_DIR}"
   wget https://boostorg.jfrog.io/artifactory/main/release/${BOOST_VER}/source/boost_${BOOST_VER//\./_}.tar.gz
   tar -xvzf boost_${BOOST_VER//\./_}.tar.gz -C ${DEP_DIR}
   pushd ${BOOST_DIR} &> /dev/null
   ./bootstrap.sh -with-toolset=clang --prefix=${BOOST_DIR}/bin
   ./b2 toolset=clang cxxflags="-stdlib=libc++ -D__STRICT_ANSI__ -nostdinc++ -I${CLANG_DIR}/include/c++/v1" linkflags='-stdlib=libc++' link=static threading=multi --with-iostreams --with-date_time --with-filesystem --with-system --with-program_options --with-chrono --with-test -q -j${JOBS} install
   popd &> /dev/null
   rm boost_${BOOST_VER//\./_}.tar.gz
fi

# download LLVM and install
LLVM_DIR=${DEP_DIR}/llvm-${LLVM_VER}

if [ ! -d "${LLVM_DIR}" ]; then
   echo "Installing LLVM ${LLVM_VER} @ ${LLVM_DIR}"
   mkdir -p ${LLVM_DIR}
   wget https://github.com/llvm/llvm-project/releases/download/llvmorg-${LLVM_VER}/llvm-${LLVM_VER}.src.tar.xz
   tar -xvf llvm-${LLVM_VER}.src.tar.xz
   pushd "${LLVM_DIR}.src"
   mkdir build
   pushd build &> /dev/null
   cmake -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_INSTALL_PREFIX=${LLVM_DIR} -DLLVM_TARGETS_TO_BUILD=host -DLLVM_BUILD_TOOLS=false -DLLVM_ENABLE_RTTI=1 ..
   make -j${JOBS} && make -j${JOBS} install
   popd &> /dev/null
   rm -rf ${LLVM_DIR}.src 
   rm llvm-${LLVM_VER}.src.tar.xz
fi

# download libpqxx
LIBPQXX_DIR=${DEP_DIR}/libpqxx-${LIBPQXX_VER}

if [ ! -d "${LIBPQXX_DIR}" ]; then
   echo "Installing libpqxx ${LIBPQXX_VER} @ ${LIBPQXX_DIR}"
   mkdir -p ${LIBPQXX_DIR}
   wget https://github.com/jtv/libpqxx/archive/${LIBPQXX_VER}.tar.gz
   tar -xvzf ${LIBPQXX_VER}.tar.gz
   pushd ${LIBPQXX_DIR} &> /dev/null
   mkdir -p build
   pushd build &> /dev/null
   cmake -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_INSTALL_PREFIX=${LIBPQXX_DIR} -DSKIP_BUILD_TEST=On -DCMAKE_BUILD_TYPE=Release -DPostgreSQL_TYPE_INCLUDE_DIR=/usr/include/postgresql ..
   make -j${JOBS} && make -j${JOBS} install
   popd &> /dev/null
   popd &> /dev/null
   rm ${LIBPQXX_VER}.tar.gz
fi

popd

# build Mandel
MANDEL_DIR=${DEP_DIR}/mandel

echo "Building Mandel"
pushd ..
mkdir -p build
pushd build &> /dev/null
cmake -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_INSTALL_PREFIX=/usr/local -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=${LLVM_DIR}/lib/cmake .. -DENABLE_OC=Off
make -j${JOBS}
cpack

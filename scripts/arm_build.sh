#! /usr/bin/bash

echo "Mandel ARM Build"

if [[ $NAME != "Ubuntu" ]]
   then
      echo "Currently only supporting Ubuntu based builds. Proceed at your own risk."
fi

if [ $# -eq 0 ] || [ -z "$1" ]
   then
      echo "Please supply a value for the number of jobs to use for building."
      echo "./pinned_build.sh 1-100"
      exit -1
fi

CORE_SYM=EOS
JOBS=$1
SCRIPT_DIR="$( cd -- "$( dirname -- "${BASH_SOURCE[0]:-$0}"; )" &> /dev/null && pwd 2> /dev/null; )";


pushdir() {
   DIR=$1
   mkdir -p ${DIR}
   pushd ${DIR} &> /dev/null
}

popdir() {
   EXPECTED=$1
   D=`popd`
   popd &> /dev/null
   echo ${D}
   D=`eval echo $D | head -n1 | cut -d " " -f1`

   if [[ ${D} != ${EXPECTED} ]]; then
     echo "Directory is not where expected EXPECTED=${EXPECTED} at ${D}"
     exit 1 
   fi
}

try(){
   output=$($@)
   res=$?
   if [[ ${res} -ne 0 ]]; then
      exit -1
   fi
}

install_boost() {
   # TODO when we get better ARM support support full pinned compilations
   # Currently Boost on aarch64-linux fails because of CMake issues and correctly detecting the toolchain
   try apt-get install libboost-all-dev
}

install_boost


# build Mandel
MANDEL_DIR=${DEP_DIR}/mandel

echo "Building Mandel"
pushdir ../build

try cmake ${CMAKE_ARG} -DCMAKE_BUILD_TYPE=Release -DENABLE_OC=Off ..

try make -j${JOBS}
try cpack

                                                                            
echo "                                                                            ";
echo "                                     _______                          .---. ";
echo " __  __   ___                _..._   \  ___ \`'.         __.....__     |   | ";
echo "|  |/  \`.'   \`.            .'     '.  ' |--.\  \    .-''         '.   |   | ";
echo "|   .-.  .-.   '          .   .-.   . | |    \  '  /     .-''\"'-.  \`. |   | ";
echo "|  |  |  |  |  |    __    |  '   '  | | |     |  '/     /________\   \|   | ";
echo "|  |  |  |  |  | .:--.'.  |  |   |  | | |     |  ||                  ||   | ";
echo "|  |  |  |  |  |/ |   \ | |  |   |  | | |     ' .'\    .-------------'|   | ";
echo "|  |  |  |  |  |\`\" __ | | |  |   |  | | |___.' /'  \    '-.____...---.|   | ";
echo "|__|  |__|  |__| .'.''| | |  |   |  |/_______.'/    \`.             .' |   | ";
echo "                / /   | |_|  |   |  |\_______|/       \`''-...... -'   '---' ";
echo "                \ \._,\ '/|  |   |  |                                       ";
echo "                 \`--'  \`\" '--'   '--'                                       ";

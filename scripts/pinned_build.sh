#!/bin/bash
set -eo pipefail

echo "Leap Pinned Build"

if [[ "$(uname)" == "Linux" ]]; then
    if [[ -e /etc/os-release ]]; then
        # obtain NAME and other information
        . /etc/os-release
        if [[ "${NAME}" != "Ubuntu" ]]; then
            echo "Currently only supporting Ubuntu based builds. Proceed at your own risk."
        fi
    else
        echo "Currently only supporting Ubuntu based builds. /etc/os-release not found. Your Linux distribution is not supported. Proceed at your own risk."
    fi
else
    echo "Currently only supporting Ubuntu based builds. Your architecture is not supported. Proceed at your own risk."
fi

if [ $# -eq 0 ] || [ -z "$1" ]; then
    echo "Please supply a directory for the build dependencies to be placed and a directory for leap build and a value for the number of jobs to use for building."
    echo "The binary packages will be created and placed into the leap build directory."
    echo "./pinned_build.sh <dependencies directory> <leap build directory> <1-100>"
    exit 255
fi

export CORE_SYM='EOS'
# CMAKE_C_COMPILER requires absolute path
DEP_DIR="$(realpath "$1")"
LEAP_DIR="$2"
JOBS="$3"
CLANG_VER=11.0.1
BOOST_VER=1.82.0
LLVM_VER=11.0.1
SCRIPT_DIR="$( cd -- "$( dirname -- "${BASH_SOURCE[0]:-$0}"; )" &> /dev/null && pwd 2> /dev/null; )";
START_DIR="$(pwd)"


pushdir() {
    DIR="$1"
    mkdir -p "${DIR}"
    pushd "${DIR}" &> /dev/null
}

popdir() {
    EXPECTED="$1"
    D="$(popd)"
    popd &> /dev/null
    echo "${D}"
    D="$(eval echo "$D" | head -n1 | cut -d " " -f1)"

    # -ef compares absolute paths
    if ! [[ "${D}" -ef "${EXPECTED}" ]]; then
        echo "Directory is not where expected EXPECTED=${EXPECTED} at ${D}"
        exit 1
    fi
}

try(){
    "$@"
    res=$?
    if [[ ${res} -ne 0 ]]; then
        exit 255
    fi
}

install_clang() {
    CLANG_DIR="$1"
    if [ ! -d "${CLANG_DIR}" ]; then
        echo "Installing Clang ${CLANG_VER} @ ${CLANG_DIR}"
        mkdir -p "${CLANG_DIR}"
        CLANG_FN="clang+llvm-${CLANG_VER}-x86_64-linux-gnu-ubuntu-16.04.tar.xz"
        try wget -O "${CLANG_FN}" "https://github.com/llvm/llvm-project/releases/download/llvmorg-${CLANG_VER}/${CLANG_FN}"
        try tar -xvf "${CLANG_FN}" -C "${CLANG_DIR}"
        pushdir "${CLANG_DIR}"
        mv clang+*/* .
        popdir "${DEP_DIR}"
        rm "${CLANG_FN}"
    fi
    export PATH="${CLANG_DIR}/bin:$PATH"
    export CLANG_DIR="${CLANG_DIR}"
}

install_llvm() {
    LLVM_DIR="$1"
    if [ ! -d "${LLVM_DIR}" ]; then
        echo "Installing LLVM ${LLVM_VER} @ ${LLVM_DIR}"
        mkdir -p "${LLVM_DIR}"
        try wget -O "llvm-${LLVM_VER}.src.tar.xz" "https://github.com/llvm/llvm-project/releases/download/llvmorg-${LLVM_VER}/llvm-${LLVM_VER}.src.tar.xz"
        try tar -xvf "llvm-${LLVM_VER}.src.tar.xz"
        pushdir "${LLVM_DIR}.src"
        pushdir build
        try cmake -DCMAKE_TOOLCHAIN_FILE="${SCRIPT_DIR}/pinned_toolchain.cmake" -DCMAKE_INSTALL_PREFIX="${LLVM_DIR}" -DCMAKE_BUILD_TYPE=Release -DLLVM_TARGETS_TO_BUILD=host -DLLVM_BUILD_TOOLS=Off -DLLVM_ENABLE_RTTI=On -DLLVM_ENABLE_TERMINFO=Off -DCMAKE_EXE_LINKER_FLAGS=-pthread -DCMAKE_SHARED_LINKER_FLAGS=-pthread -DLLVM_ENABLE_PIC=NO ..
        try make -j "${JOBS}"
        try make -j "${JOBS}" install
        popdir "${LLVM_DIR}.src"
        popdir "${DEP_DIR}"
        rm -rf "${LLVM_DIR}.src"
        rm "llvm-${LLVM_VER}.src.tar.xz"
    fi
    export LLVM_DIR="${LLVM_DIR}"
}

pushdir "${DEP_DIR}"

install_clang "${DEP_DIR}/clang-${CLANG_VER}"
install_llvm "${DEP_DIR}/llvm-${LLVM_VER}"

# go back to the directory where the script starts
popdir "${START_DIR}"

pushdir "${LEAP_DIR}"

# build Leap
echo "Building Leap ${SCRIPT_DIR}"
try cmake -DCMAKE_TOOLCHAIN_FILE="${SCRIPT_DIR}/pinned_toolchain.cmake" -DCMAKE_INSTALL_PREFIX=${LEAP_PINNED_INSTALL_PREFIX:-/usr/local} -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="${LLVM_DIR}/lib/cmake" "${SCRIPT_DIR}/.."

try make -j "${JOBS}"
try cpack

# art generated with DALL-E (https://openai.com/blog/dall-e), then fed through ASCIIart.club (https://asciiart.club) with permission
cat <<'TXT' # BASH interpolation must remain disabled for the ASCII art to print correctly

                                ,▄▄A`
                             _╓▄██`
                          ╓▄▓▓▀▀`
                        ╓▓█▀╓▄▓
                        ▓▌▓▓▓▀
                      ,▄▓███▓H
                    _╨╫▀╚▀╠▌`╙¥,
                  ╓«    _╟▄▄   `½,                              ╓▄▄╦▄≥_
                 ╙▓╫╬▒R▀▀╙▀▀▓φ_ «_╙Y╥▄mmMM#╦▄,_          ,╓╦mM╩╨╙╙╙\`║═
                   ``       `▀▄__╫▓▓╨`    _```"""*ⁿⁿ^`````Ω,        `╟∩
                              ╙▌▓▓"`    ,«ñ`            ╔╬▓▌⌂        ╔▌
                               ╙█▌,,╔╗M╨,░             `  "╫▓m_      ╟H      _
                        _,,,,__,╠█▓▒`  .╣▌µ _       _.╓╔▄▄▓█▓▓N_     ╙▀╩KKM╙╟▓N
                      ,▄▓█▓████▀▀▀╙▀╓╔φ»█▓▓Ñ╦«, :»»µ╦▓▓█▀└╙▀███▓╥__   _,╓▄▓▓▓M▓`,
                  __╓Φ▓█╫▓▓▓▓▓▓▓▓▓▓▓▓▓▀K▀▀███▓▓▓▓▓▓▀▀╙       `▀▀▀▓▄▄K╨╙└   `▀▌╙█▄*.
                ,╓Φ▓▓▀▄▓▀`         ╙▀╙                                       ╙▓╙╙▓▄*
              .▄▓╫▀╦▄▀`                                                       ╙▓╙µ╙▀
             ▄▓▀╨▓▓╨_                                                          `▀▄▄M
            _█▌▄▌╙`                                                               `╙
             ╙└`


                Ñ▓▓▓▓          ¢▄▄▄▄▄▄▄▄▄▄         ,           ,,,,,,,,,,,_
               _╫▓▓█▌          ╟▓████████▀       ╓╣▓▌_         ╠╫▓█▓▓▓▓█▓▓▓▓▄
               _▓▓▓█▌          ╟╫██▄,,,,_       æ▄███▓▄        ▐║████▀╨╨▀▓███▌
               :╫▓▓█▌          ╟╢████████▓    ,╬███████▓,      ▐║████▓▄▄▓▓███▀
               :╫▓▓█▌_        _╟║███▀▀▀▀▀▀   ╓╫███╣╫╬███▓N_    j▐██▀▀▀▀▀▀▀▀▀`
    ___________]╫▓▓█▓φ╓╓╓╓╓╓,__╟╣█▌▌,,,,,_ _╬▓████████████▓▄_  ▐M█▌
    _ _________]╫▌▓█████████▓▓▄╟╣████████▓▄╣██▓▀^    _ ╙▓██▓▓╗__M█▓
      ___   _ _ ▀╣▀╣╩╩╩╩╩╩╩▀▀▀▀╙▀▀▀▀▀▀▀▀▀▀▀▀▀▀`          ╙▀▀▀▀╩═╩▀▀
     _ __ __ _____ ___  ____ _  __  _  ____ ___
    ____ ____ __  _ _  _ _ _ _ __   _ __ __
    __ _ ________  ___ ________   ___ _ _ _      _
       _ __  __     _ __ ____ _     _ ____ _   _ _       _
     _  _ _     ____ ____ _ _    _ __  _ _ _ __

---
Leap has successfully built and constructed its packages. You should be able to
find the packages at:
TXT
echo "${LEAP_DIR}"
echo
echo 'Thank you, fam!!'
echo

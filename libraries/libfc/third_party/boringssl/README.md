# BoringSSL Build instructions

BoringSSL build is very straightforward but has a few caveats. By default integration of library
requires pre-installed Go language to generate make files. Here a way to build/integrate BoringSSL
without introducing any extra dependencies will be described.

Alternative way of generating build scripts described here: https://boringssl.googlesource.com/boringssl/+/HEAD/INCORPORATING.md#build-support

Unfortunately generated build is not sufficient for leap as it misses certain libraries (such as decrepit), so
changes to generation script has been introduced and following procedure can be used to perform a build:

* Copy patched generate_build_files.py file from libraries/libfc/third_party/boringssl/generate_build_files.py over to 
libraries/libfc/third_party/boringssl/src/util/generate_build_files.py replacing the file which is there
* From libraries/libfc/third_party/boringssl folder execute python3 src/util/generate_build_files.py cmake
* Wait until script successfully exits. Script need Go v1.20+ to run. Go wont be required for building leap after build files generated.
* Now you can configure/build leap and push new build scripts into repo.
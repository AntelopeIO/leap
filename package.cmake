#DEB & RPM are known to work, but don't enable them by default
set(CPACK_GENERATOR "TGZ")

set(CPACK_PACKAGE_VERSION "${VERSION_FULL}")
set(CPACK_PACKAGE_FILE_NAME "${CMAKE_PROJECT_NAME}-${VERSION_FULL}")

set(CPACK_PACKAGE_CONTACT "EOS Network Foundation")
set(CPACK_PACKAGE_VENDOR "EOS Network Foundation")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "daemon and CLI tools for Mandel blockchain protocol")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/eosnetworkfoundation/mandel")

set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

#turn some knobs to try and make package paths cooperate with GNUInstallDirs a little better
set(CPACK_SET_DESTDIR ON)
set(CPACK_PACKAGE_RELOCATABLE OFF)

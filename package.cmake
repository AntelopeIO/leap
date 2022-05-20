set(CPACK_GENERATOR "TGZ")
find_program(DPKG_FOUND "dpkg")
find_program(RPMBUILD_FOUND "rpmbuild")
if(DPKG_FOUND)
   list(APPEND CPACK_GENERATOR "DEB")
endif()
if(RPMBUILD_FOUND)
   list(APPEND CPACK_GENERATOR "RPM")
endif()

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

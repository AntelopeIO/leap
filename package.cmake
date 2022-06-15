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
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.9")
   #if we're doing the build on Ubuntu or RHELish, add the platform version in to the package name
   file(READ /etc/os-release OS_RELEASE LIMIT 4096)
   if(OS_RELEASE MATCHES "\n?ID=\"?ubuntu" AND OS_RELEASE MATCHES "\n?VERSION_ID=\"?([0-9.]+)")
      string(APPEND CPACK_PACKAGE_FILE_NAME "-ubuntu${CMAKE_MATCH_1}")
   elseif(OS_RELEASE MATCHES "\n?ID=\"?rhel" AND OS_RELEASE MATCHES "\n?VERSION_ID=\"?([0-9]+)")
	   string(APPEND CPACK_PACKAGE_FILE_NAME "-el${CMAKE_MATCH_1}")
   elseif(OS_RELEASE MATCHES "\n?ID_LIKE=\"?([a-zA-Z0-9 ]*)" AND CMAKE_MATCH_1 MATCHES "rhel" AND OS_RELEASE MATCHES "\n?VERSION_ID=\"?([0-9]+)")
      string(APPEND CPACK_PACKAGE_FILE_NAME "-el${CMAKE_MATCH_1}")
   endif()
endif()
string(APPEND CPACK_PACKAGE_FILE_NAME "-${CMAKE_SYSTEM_PROCESSOR}")

set(CPACK_PACKAGE_CONTACT "EOS Network Foundation")
set(CPACK_PACKAGE_VENDOR "EOS Network Foundation")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Mandel blockchain protocol")
set(CPACK_COMPONENT_BASE_DESCRIPTION "daemon and CLI tools including ${NODE_EXECUTABLE_NAME}, ${CLI_CLIENT_EXECUTABLE_NAME}, and ${KEY_STORE_EXECUTABLE_NAME}")
set(CPACK_COMPONENT_DEV_DESCRIPTION "headers and libraries for native contract unit testing")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/eosnetworkfoundation/mandel")

set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_BASE_PACKAGE_SECTION "utils")

set(CPACK_DEBIAN_PACKAGE_CONFLICTS "eosio")
set(CPACK_RPM_PACKAGE_CONFLICTS "eosio")

#only consider "base" and "dev" components for per-component packages
get_cmake_property(CPACK_COMPONENTS_ALL COMPONENTS)
list(REMOVE_ITEM CPACK_COMPONENTS_ALL "Unspecified")

#enable per component packages for .deb; ensure main package is just "mandel", not "mandel-base", and make the dev package have "mandel-dev" at the front not the back
set(CPACK_DEB_COMPONENT_INSTALL ON)
set(CPACK_DEBIAN_BASE_PACKAGE_NAME "${CMAKE_PROJECT_NAME}")
set(CPACK_DEBIAN_BASE_FILE_NAME "${CPACK_PACKAGE_FILE_NAME}.deb")
string(REGEX REPLACE "^(${CMAKE_PROJECT_NAME})" "\\1-dev" CPACK_DEBIAN_DEV_FILE_NAME "${CPACK_DEBIAN_BASE_FILE_NAME}")

#deb package tooling will be unable to detect deps for the dev package. llvm is tricky since we don't know what package could have been used; try to figure it out
set(CPACK_DEBIAN_DEV_PACKAGE_DEPENDS "libboost-all-dev;libssl-dev;libgmp-dev")
find_program(DPKG_QUERY "dpkg-query")
if(DPKG_QUERY AND OS_RELEASE MATCHES "\n?ID=\"?ubuntu")
   execute_process(COMMAND "${DPKG_QUERY}" -S "${LLVM_CMAKE_DIR}" COMMAND cut -d: -f1 RESULT_VARIABLE LLVM_PKG_FIND_RESULT OUTPUT_VARIABLE LLVM_PKG_FIND_OUTPUT)
   if(LLVM_PKG_FIND_RESULT EQUAL 0)
      string(STRIP "${LLVM_PKG_FIND_OUTPUT}" LLVM_PKG_FIND_OUTPUT)
      list(APPEND CPACK_DEBIAN_DEV_PACKAGE_DEPENDS "${LLVM_PKG_FIND_OUTPUT}")
   endif()
endif()

#since rpm packages aren't component based, make sure description picks up more detailed description instead of just summary
set(CPACK_RPM_PACKAGE_DESCRIPTION "${CPACK_COMPONENT_BASE_DESCRIPTION}")

#turn some knobs to try and make package paths cooperate with GNUInstallDirs a little better
set(CPACK_SET_DESTDIR ON)
set(CPACK_PACKAGE_RELOCATABLE OFF)

set(CHAINBASE_INSTALL_COMPONENT "dev")
set(FC_INSTALL_COMPONENT "dev")
set(APPBASE_INSTALL_COMPONENT "dev")
set(SOFTFLOAT_INSTALL_COMPONENT "dev")
set(EOSVM_INSTALL_COMPONENT "dev")

add_subdirectory( libfc )
add_subdirectory( builtins )
add_subdirectory( softfloat )
add_subdirectory( wasm-jit )
add_subdirectory( chainbase )
set(APPBASE_ENABLE_AUTO_VERSION OFF CACHE BOOL "enable automatic discovery of version via 'git describe'")
add_subdirectory( appbase )
add_subdirectory( chain )
add_subdirectory( testing )
add_subdirectory( version )
add_subdirectory( state_history )
add_subdirectory( cli11 )

set(USE_EXISTING_SOFTFLOAT ON CACHE BOOL "use pre-exisiting softfloat lib")
set(ENABLE_TOOLS OFF CACHE BOOL "Build tools")
set(ENABLE_TESTS OFF CACHE BOOL "Build tests")
set(ENABLE_ADDRESS_SANITIZER OFF CACHE BOOL "Use address sanitizer")
set(ENABLE_UNDEFINED_BEHAVIOR_SANITIZER OFF CACHE BOOL "Use UB sanitizer")
set(ENABLE_PROFILE OFF CACHE BOOL "Enable for profile builds")
if(eos-vm IN_LIST EOSIO_WASM_RUNTIMES OR eos-vm-jit IN_LIST EOSIO_WASM_RUNTIMES)
add_subdirectory( eos-vm )
endif()

#yubihsm's openssl discovery is via pkg-config instead of find_package. Help it out on macOS otherwise openssl's pkgconfig
# files may not be found down in the /opt/homebrew/opt directory
if(APPLE)
  get_filename_component(OPENSSL_LIB_PATH "${OPENSSL_CRYPTO_LIBRARY}" DIRECTORY)
  set(ENV{PKG_CONFIG_PATH} "$ENV{PKG_CONFIG_PATH}:${OPENSSL_LIB_PATH}/pkgconfig")
endif()
set(ENABLE_STATIC ON)
set(CMAKE_MACOSX_RPATH OFF)
set(BUILD_ONLY_LIB ON CACHE BOOL "Library only build")
message(STATUS "Starting yubihsm configuration...")
add_subdirectory( yubihsm EXCLUDE_FROM_ALL )
target_compile_options(yubihsm_static PRIVATE -fno-lto -fcommon)
message(STATUS "yubihsm configuration complete")

get_property(_CTEST_CUSTOM_TESTS_IGNORE GLOBAL PROPERTY CTEST_CUSTOM_TESTS_IGNORE)
set_property(GLOBAL PROPERTY CTEST_CUSTOM_TESTS_IGNORE
  "change_authkey import_ed decrypt_ec decrypt_rsa ssh logs generate_rsa import_ec echo\
  yubico_otp wrap_data wrap info import_rsa import_authkey generate_hmac generate_ec\
  attest pbkdf2 parsing ${_CTEST_CUSTOM_TESTS_IGNORE}")

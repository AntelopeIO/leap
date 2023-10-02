add_subdirectory( secp256k1 )
add_subdirectory( libraries/bn256/src )
add_subdirectory( libraries/bls12-381 )
add_subdirectory( libraries/boringssl )

set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package(Threads)


set( fc_sources
     src/uint128.cpp
     src/variant.cpp
     src/exception.cpp
     src/variant_object.cpp
     src/string.cpp
     src/time.cpp
     src/mock_time.cpp
     src/utf8.cpp
     src/io/datastream.cpp
     src/io/json.cpp
     src/io/varint.cpp
     src/io/fstream.cpp
     src/io/console.cpp
     src/filesystem.cpp
     src/interprocess/file_mapping.cpp
     src/log/log_message.cpp
     src/log/logger.cpp
     src/log/appender.cpp
     src/log/console_appender.cpp
     src/log/dmlog_appender.cpp
     src/log/logger_config.cpp
     src/crypto/_digest_common.cpp
     src/crypto/aes.cpp
     src/crypto/crc.cpp
     src/crypto/city.cpp
     src/crypto/base58.cpp
     src/crypto/base64.cpp
     src/crypto/bigint.cpp
     src/crypto/hex.cpp
     src/crypto/sha1.cpp
     src/crypto/sha3.cpp
     src/crypto/ripemd160.cpp
     src/crypto/sha256.cpp
     src/crypto/sha224.cpp
     src/crypto/sha512.cpp
     src/crypto/elliptic_common.cpp
     src/crypto/elliptic_impl_priv.cpp
     src/crypto/elliptic_secp256k1.cpp
     src/crypto/elliptic_r1.cpp
     src/crypto/elliptic_webauthn.cpp
     src/crypto/rand.cpp
     src/crypto/public_key.cpp
     src/crypto/private_key.cpp
     src/crypto/signature.cpp
     src/crypto/modular_arithmetic.cpp
     src/crypto/blake2.cpp
     src/crypto/k1_recover.cpp
     src/network/url.cpp
     src/network/http/http_client.cpp
     src/compress/zlib.cpp
     src/log/gelf_appender.cpp
     )

file( GLOB_RECURSE fc_headers ${CMAKE_CURRENT_SOURCE_DIR} *.hpp *.h )

add_library(fc ${fc_sources} ${fc_headers})

find_path(GMP_INCLUDE_DIR NAMES gmp.h)
find_library(GMP_LIBRARY gmp)
if(NOT GMP_LIBRARY MATCHES ${CMAKE_SHARED_LIBRARY_SUFFIX})
  message( FATAL_ERROR "GMP shared library not found" )
endif()
set(gmp_library_type SHARED)
message(STATUS "GMP: ${GMP_LIBRARY}, ${GMP_INCLUDE_DIR}")
add_library(GMP::gmp ${gmp_library_type} IMPORTED)
set_target_properties(
  GMP::gmp PROPERTIES
  IMPORTED_LOCATION ${GMP_LIBRARY}
  INTERFACE_INCLUDE_DIRECTORIES ${GMP_INCLUDE_DIR}
)

target_link_libraries(fc PUBLIC GMP::gmp)

# fc picks up a dependency on zlib via Boost::iostreams, however in some versions of cmake/boost (depending on if CMake config
#  files are used) the Boost::iostreams target does not have a dependency on zlib itself so add it explicitly
find_package(ZLIB REQUIRED)

target_include_directories(fc PUBLIC include)

# try and make this very clear that this json parser is intended only for webauthn parsing..
set_source_files_properties(src/crypto/elliptic_webauthn.cpp PROPERTIES INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/include/fc/crypto/webauthn_json/include")

if(WIN32)
  target_link_libraries( fc PUBLIC ws2_32 mswsock userenv )
endif()

if(APPLE)
  find_library(security_framework Security)
  find_library(corefoundation_framework CoreFoundation)
endif()
target_link_libraries( fc PUBLIC Boost::date_time Boost::chrono Boost::iostreams Boost::interprocess Boost::multi_index Boost::dll
                                 Boost::multiprecision Boost::beast Boost::asio Boost::thread Boost::unit_test_framework Threads::Threads
                                 boringssl ZLIB::ZLIB ${PLATFORM_SPECIFIC_LIBS} ${CMAKE_DL_LIBS} secp256k1 bls12-381 ${security_framework} ${corefoundation_framework})

add_subdirectory( test )

install(TARGETS fc
   LIBRARY DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR} COMPONENT dev EXCLUDE_FROM_ALL
   ARCHIVE DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR} COMPONENT dev EXCLUDE_FROM_ALL)
install(DIRECTORY include/fc DESTINATION ${CMAKE_INSTALL_FULL_INCLUDEDIR} COMPONENT dev EXCLUDE_FROM_ALL)

install(TARGETS bls12-381 ARCHIVE       DESTINATION "${CMAKE_INSTALL_FULL_LIBDIR}"               COMPONENT dev EXCLUDE_FROM_ALL
                          PUBLIC_HEADER DESTINATION "${CMAKE_INSTALL_FULL_INCLUDEDIR}/bls12-381" COMPONENT dev EXCLUDE_FROM_ALL)

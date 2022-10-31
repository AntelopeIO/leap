#the secp256k1-internal INTERFACE library is used to define some include paths & compile defs that are needed not just
# for compiling the library (where PRIVATE would have been fine), but also for the unit tests.
add_library(secp256k1-internal INTERFACE)

target_include_directories(secp256k1-internal
    INTERFACE
        secp256k1/src
        config
)

target_compile_definitions(secp256k1-internal INTERFACE HAVE_CONFIG_H)

# secp256k1 produces over 190 "unused-function" warnings like
# warning: ‘secp256k1_fe_normalize_weak’ declared ‘static’ but never defined [-Wunused-f unction]
# As we consider it as a system header and use it verbatim without any modifications,
# just disable the warning to avoid cluttering compile log
target_compile_options(secp256k1-internal INTERFACE -Wno-unused-function)

add_library(secp256k1 STATIC
  secp256k1/src/secp256k1.c secp256k1/src/precomputed_ecmult.c secp256k1/src/precomputed_ecmult_gen.c
)

target_include_directories(secp256k1
    PUBLIC
        secp256k1
        secp256k1/include
)

target_link_libraries(secp256k1 PRIVATE secp256k1-internal)

install( TARGETS secp256k1 
   RUNTIME DESTINATION ${CMAKE_INSTALL_FULL_BINDIR} COMPONENT dev EXCLUDE_FROM_ALL
   LIBRARY DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR} COMPONENT dev EXCLUDE_FROM_ALL
   ARCHIVE DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR} COMPONENT dev EXCLUDE_FROM_ALL
)

add_executable(secp256k1-bench secp256k1/src/bench.c)
target_link_libraries(secp256k1-bench secp256k1 secp256k1-internal)

add_executable(secp256k1-tests secp256k1/src/tests.c)
target_link_libraries(secp256k1-tests secp256k1 secp256k1-internal)
add_test(secp256k1-tests secp256k1-tests)

add_executable(secp256k1-exhaustive-tests secp256k1/src/tests_exhaustive.c)
target_link_libraries(secp256k1-exhaustive-tests secp256k1 secp256k1-internal)
add_test(secp256k1-exhaustive-tests secp256k1-exhaustive-tests)

#the secp256k1-internal INTERFACE library is used to define some include paths & compile defs that are needed not just
# for compiling the library (where PRIVATE would have been fine), but also for the unit tests.
add_library(secp256k1-internal INTERFACE)

target_include_directories(secp256k1-internal
    INTERFACE
        secp256k1/src
        config
)

target_compile_definitions(secp256k1-internal INTERFACE HAVE_CONFIG_H)

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
   RUNTIME DESTINATION ${CMAKE_INSTALL_FULL_BINDIR} ${INSTALL_COMPONENT_ARGS}
   LIBRARY DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR} ${INSTALL_COMPONENT_ARGS}
   ARCHIVE DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR} ${INSTALL_COMPONENT_ARGS}
)

add_executable(secp256k1-bench secp256k1/src/bench.c)
target_link_libraries(secp256k1-bench secp256k1 secp256k1-internal)

add_executable(secp256k1-tests secp256k1/src/tests.c)
target_link_libraries(secp256k1-tests secp256k1 secp256k1-internal)
add_test(secp256k1-tests secp256k1-tests)

add_executable(secp256k1-exhaustive-tests secp256k1/src/tests_exhaustive.c)
target_link_libraries(secp256k1-exhaustive-tests secp256k1 secp256k1-internal)
add_test(secp256k1-exhaustive-tests secp256k1-exhaustive-tests)

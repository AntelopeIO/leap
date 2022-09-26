add_executable( test_cypher_suites test_cypher_suites.cpp )
target_link_libraries( test_cypher_suites fc )

add_executable( test_webauthn test_webauthn.cpp )
target_link_libraries( test_webauthn fc )

add_executable( test_hash_functions test_hash_functions.cpp )
target_link_libraries( test_hash_functions fc )

add_executable( test_alt_bn128 test_alt_bn128.cpp )
target_link_libraries( test_alt_bn128 fc )

add_executable( test_blake2 test_blake2.cpp )
target_link_libraries( test_blake2 fc )

add_executable( test_modular_arithmetic test_modular_arithmetic.cpp )
target_link_libraries( test_modular_arithmetic fc )

add_executable( test_k1_recover test_k1_recover.cpp )
target_link_libraries( test_k1_recover fc )

add_test(NAME test_cypher_suites COMMAND test_cypher_suites WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
add_test(NAME test_webauthn COMMAND test_webauthn WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
add_test(NAME test_hash_functions COMMAND test_hash_functions WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
add_test(NAME test_alt_bn128 COMMAND test_alt_bn128 WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
add_test(NAME test_blake2 COMMAND test_blake2 WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
add_test(NAME test_modular_arithmetic COMMAND test_modular_arithmetic WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
add_test(NAME test_k1_recover COMMAND test_k1_recover WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
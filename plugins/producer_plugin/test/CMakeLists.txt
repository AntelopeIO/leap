add_executable( test_subjective_billing test_subjective_billing.cpp )
target_link_libraries( test_subjective_billing producer_plugin eosio_testing )

add_test(NAME test_subjective_billing COMMAND plugins/producer_plugin/test/test_subjective_billing WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable( test_trx_full test_trx_full.cpp )
target_link_libraries( test_trx_full producer_plugin eosio_testing )

add_test(NAME test_trx_full COMMAND plugins/producer_plugin/test/test_trx_full WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable( test_options test_options.cpp )
target_link_libraries( test_options producer_plugin eosio_testing )

add_test(NAME test_options COMMAND plugins/producer_plugin/test/test_options WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

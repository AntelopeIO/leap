add_executable( test_account_query_db test_account_query_db.cpp )

target_link_libraries( test_account_query_db chain_plugin eosio_testing)

add_test(NAME test_account_query_db COMMAND plugins/chain_plugin/test/test_account_query_db WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable( test_trx_retry_db test_trx_retry_db.cpp )

target_link_libraries( test_trx_retry_db chain_plugin eosio_testing)

add_test(NAME test_trx_retry_db COMMAND plugins/chain_plugin/test/test_trx_retry_db WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable( test_trx_finality_status_processing test_trx_finality_status_processing.cpp )

target_link_libraries( test_trx_finality_status_processing chain_plugin eosio_testing)

add_test(NAME test_trx_finality_status_processing COMMAND plugins/chain_plugin/test/test_trx_finality_status_processing WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable( test_chain_plugin test_chain_plugin.cpp )

target_link_libraries( test_chain_plugin chain_plugin eosio_testing)

add_test(NAME test_chain_plugin COMMAND plugins/chain_plugin/test/test_chain_plugin WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

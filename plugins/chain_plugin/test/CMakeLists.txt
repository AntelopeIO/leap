add_executable( test_account_query_db test_account_query_db.cpp )

target_link_libraries( test_account_query_db chain_plugin eosio_testing)

add_test(NAME test_account_query_db COMMAND plugins/chain_plugin/test/test_account_query_db WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable( test_signals_processor test_signals_processor.cpp )

target_link_libraries( test_signals_processor chain_plugin eosio_testing)

add_test(NAME test_signals_processor COMMAND plugins/chain_plugin/test/test_signals_processor WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
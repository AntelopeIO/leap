add_executable( test_account_query_db test_account_query_db.cpp )

target_link_libraries( test_account_query_db chain_plugin eosio_testing)

add_test(NAME test_account_query_db COMMAND plugins/chain_plugin/test/test_account_query_db WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable( test_tracked_storage test_tracked_storage.cpp )

target_link_libraries( test_tracked_storage chain_plugin eosio_testing eosio_chain)

add_test(NAME test_tracked_storage COMMAND plugins/chain_plugin/test/test_tracked_storage WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
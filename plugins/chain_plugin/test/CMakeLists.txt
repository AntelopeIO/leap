add_executable( test_chain_plugin
        test_account_query_db.cpp
        test_trx_retry_db.cpp
        test_trx_finality_status_processing.cpp
        plugin_config_test.cpp
        main.cpp
        )
target_link_libraries( test_chain_plugin chain_plugin eosio_testing)
add_test(NAME test_chain_plugin COMMAND plugins/chain_plugin/test/test_chain_plugin WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
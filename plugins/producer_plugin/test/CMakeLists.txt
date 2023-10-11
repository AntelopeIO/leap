add_executable( test_producer_plugin
        test_trx_full.cpp
        test_options.cpp
        test_block_timing_util.cpp
        test_disallow_delayed_trx.cpp
        main.cpp
        )
target_link_libraries( test_producer_plugin producer_plugin eosio_testing eosio_chain_wrap )
add_test(NAME test_producer_plugin COMMAND plugins/producer_plugin/test/test_producer_plugin WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

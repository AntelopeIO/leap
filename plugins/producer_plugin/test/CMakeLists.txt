add_executable( test_subjective_billing test_subjective_billing.cpp )
target_link_libraries( test_subjective_billing producer_plugin eosio_testing )

add_test(NAME test_subjective_billing COMMAND plugins/producer_plugin/test/test_subjective_billing WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable( test_trx_full test_trx_full.cpp )
target_link_libraries( test_trx_full producer_plugin eosio_testing )

add_test(NAME test_trx_full COMMAND plugins/producer_plugin/test/test_trx_full WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable( test_options test_options.cpp )
target_link_libraries( test_options producer_plugin eosio_testing )

add_test(NAME test_options COMMAND plugins/producer_plugin/test/test_options WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable( test_snapshot_scheduler test_snapshot_scheduler.cpp )
target_link_libraries( test_snapshot_scheduler producer_plugin eosio_testing )

add_test(NAME test_snapshot_scheduler COMMAND plugins/producer_plugin/test/test_snapshot_scheduler WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable( test_read_only_trx test_read_only_trx.cpp )
target_link_libraries( test_read_only_trx producer_plugin eosio_testing )

add_test(NAME test_read_only_trx COMMAND plugins/producer_plugin/test/test_read_only_trx WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable( test_block_timing_util test_block_timing_util.cpp )
target_link_libraries( test_block_timing_util producer_plugin eosio_testing )
add_test(NAME test_block_timing_util COMMAND plugins/producer_plugin/test/test_block_timing_util WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
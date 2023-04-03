include_directories( "${CMAKE_SOURCE_DIR}/plugins/wallet_plugin/include" )

file(GLOB UNIT_TESTS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "*.cpp")
list(REMOVE_ITEM UNIT_TESTS ship_client.cpp)
list(REMOVE_ITEM UNIT_TESTS ship_streamer.cpp)

add_executable( plugin_test ${UNIT_TESTS} )
target_link_libraries( plugin_test eosio_testing eosio_chain chainbase chain_plugin wallet_plugin fc state_history ${PLATFORM_SPECIFIC_LIBS} )

target_include_directories( plugin_test PUBLIC
                            ${CMAKE_SOURCE_DIR}/plugins/net_plugin/include
                            ${CMAKE_SOURCE_DIR}/plugins/chain_plugin/include
                            ${CMAKE_SOURCE_DIR}/unittests
                            ${CMAKE_BINARY_DIR}/unittests/include/ )

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/p2p_tests/dawn_515/test.sh ${CMAKE_CURRENT_BINARY_DIR}/p2p_tests/dawn_515/test.sh COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/block_log_util_test.py ${CMAKE_CURRENT_BINARY_DIR}/block_log_util_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/block_log_retain_blocks_test.py ${CMAKE_CURRENT_BINARY_DIR}/block_log_retain_blocks_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/distributed-transactions-test.py ${CMAKE_CURRENT_BINARY_DIR}/distributed-transactions-test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/distributed-transactions-remote-test.py ${CMAKE_CURRENT_BINARY_DIR}/distributed-transactions-remote-test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/sample-cluster-map.json ${CMAKE_CURRENT_BINARY_DIR}/sample-cluster-map.json COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/restart-scenarios-test.py ${CMAKE_CURRENT_BINARY_DIR}/restart-scenarios-test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/terminate-scenarios-test.py ${CMAKE_CURRENT_BINARY_DIR}/terminate-scenarios-test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/nodeos_startup_catchup.py ${CMAKE_CURRENT_BINARY_DIR}/nodeos_startup_catchup.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/nodeos_snapshot_diff_test.py ${CMAKE_CURRENT_BINARY_DIR}/nodeos_snapshot_diff_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/nodeos_snapshot_forked_test.py ${CMAKE_CURRENT_BINARY_DIR}/nodeos_snapshot_forked_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/nodeos_forked_chain_test.py ${CMAKE_CURRENT_BINARY_DIR}/nodeos_forked_chain_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/nodeos_short_fork_take_over_test.py ${CMAKE_CURRENT_BINARY_DIR}/nodeos_short_fork_take_over_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/nodeos_run_test.py ${CMAKE_CURRENT_BINARY_DIR}/nodeos_run_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/nodeos_under_min_avail_ram.py ${CMAKE_CURRENT_BINARY_DIR}/nodeos_under_min_avail_ram.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/nodeos_voting_test.py ${CMAKE_CURRENT_BINARY_DIR}/nodeos_voting_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/nodeos_irreversible_mode_test.py ${CMAKE_CURRENT_BINARY_DIR}/nodeos_irreversible_mode_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/nodeos_read_terminate_at_block_test.py ${CMAKE_CURRENT_BINARY_DIR}/nodeos_read_terminate_at_block_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/nodeos_chainbase_allocation_test.py ${CMAKE_CURRENT_BINARY_DIR}/nodeos_chainbase_allocation_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/nodeos_protocol_feature_test.py ${CMAKE_CURRENT_BINARY_DIR}/nodeos_protocol_feature_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/nodeos_multiple_version_protocol_feature_test.py ${CMAKE_CURRENT_BINARY_DIR}/nodeos_multiple_version_protocol_feature_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/nodeos_extra_packed_data_test.py ${CMAKE_CURRENT_BINARY_DIR}/nodeos_extra_packed_data_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/validate-dirty-db.py ${CMAKE_CURRENT_BINARY_DIR}/validate-dirty-db.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/launcher_test.py ${CMAKE_CURRENT_BINARY_DIR}/launcher_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/keosd_auto_launch_test.py ${CMAKE_CURRENT_BINARY_DIR}/keosd_auto_launch_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/db_modes_test.sh ${CMAKE_CURRENT_BINARY_DIR}/db_modes_test.sh COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/prod_preactivation_test.py ${CMAKE_CURRENT_BINARY_DIR}/prod_preactivation_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/release-build.sh ${CMAKE_CURRENT_BINARY_DIR}/release-build.sh COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/version-label.sh ${CMAKE_CURRENT_BINARY_DIR}/version-label.sh COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/full-version-label.sh ${CMAKE_CURRENT_BINARY_DIR}/full-version-label.sh COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/nodeos_producer_watermark_test.py ${CMAKE_CURRENT_BINARY_DIR}/nodeos_producer_watermark_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cli_test.py ${CMAKE_CURRENT_BINARY_DIR}/cli_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/ship_test.py ${CMAKE_CURRENT_BINARY_DIR}/ship_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/ship_streamer_test.py ${CMAKE_CURRENT_BINARY_DIR}/ship_streamer_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/large-lib-test.py ${CMAKE_CURRENT_BINARY_DIR}/large-lib-test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/http_plugin_test.py ${CMAKE_CURRENT_BINARY_DIR}/http_plugin_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/p2p_high_latency_test.py ${CMAKE_CURRENT_BINARY_DIR}/p2p_high_latency_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/compute_transaction_test.py ${CMAKE_CURRENT_BINARY_DIR}/compute_transaction_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/subjective_billing_test.py ${CMAKE_CURRENT_BINARY_DIR}/subjective_billing_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/get_account_test.py ${CMAKE_CURRENT_BINARY_DIR}/get_account_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/nodeos_high_transaction_test.py ${CMAKE_CURRENT_BINARY_DIR}/nodeos_high_transaction_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/nodeos_retry_transaction_test.py ${CMAKE_CURRENT_BINARY_DIR}/nodeos_retry_transaction_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/trx_finality_status_test.py ${CMAKE_CURRENT_BINARY_DIR}/trx_finality_status_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/trx_finality_status_forked_test.py ${CMAKE_CURRENT_BINARY_DIR}/trx_finality_status_forked_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/plugin_http_api_test.py ${CMAKE_CURRENT_BINARY_DIR}/plugin_http_api_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/nodeos_contrl_c_test.py ${CMAKE_CURRENT_BINARY_DIR}/nodeos_contrl_c_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/read_only_trx_test.py ${CMAKE_CURRENT_BINARY_DIR}/read_only_trx_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/resource_monitor_plugin_test.py ${CMAKE_CURRENT_BINARY_DIR}/resource_monitor_plugin_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/light_validation_sync_test.py ${CMAKE_CURRENT_BINARY_DIR}/light_validation_sync_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/trace_plugin_test.py ${CMAKE_CURRENT_BINARY_DIR}/trace_plugin_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/nested_container_multi_index_test.py ${CMAKE_CURRENT_BINARY_DIR}/nested_container_multi_index_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/large-lib-test.py ${CMAKE_CURRENT_BINARY_DIR}/large-lib-test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/launcher.py ${CMAKE_CURRENT_BINARY_DIR}/launcher.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/auto_bp_peering_test.py ${CMAKE_CURRENT_BINARY_DIR}/auto_bp_peering_test.py COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/auto_bp_peering_test_shape.json ${CMAKE_CURRENT_BINARY_DIR}/auto_bp_peering_test_shape.json COPYONLY)

if(DEFINED ENV{GITHUB_ACTIONS})
  set(UNSHARE "--unshared")
else()
  set(UNSHARE "")
endif()

#To run plugin_test with all log from blockchain displayed, put --verbose after --, i.e. plugin_test -- --verbose
add_test(NAME plugin_test COMMAND plugin_test --report_level=detailed --color_output)

add_test(NAME nodeos_sanity_test COMMAND tests/nodeos_run_test.py -v --sanity-test --clean-run ${UNSHARE} WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_property(TEST nodeos_sanity_test PROPERTY LABELS nonparallelizable_tests)
add_test(NAME nodeos_run_test COMMAND tests/nodeos_run_test.py -v --clean-run ${UNSHARE} WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_property(TEST nodeos_run_test PROPERTY LABELS nonparallelizable_tests)
add_test(NAME block_log_util_test COMMAND tests/block_log_util_test.py -v --clean-run ${UNSHARE} WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_property(TEST block_log_util_test PROPERTY LABELS nonparallelizable_tests)
add_test(NAME block_log_retain_blocks_test COMMAND tests/block_log_retain_blocks_test.py -v --clean-run ${UNSHARE} WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_property(TEST block_log_retain_blocks_test PROPERTY LABELS nonparallelizable_tests)

option(ABIEOS_ONLY_LIBRARY "define and build the ABIEOS library" ON)
set(ABIEOS_INSTALL_COMPONENT "dev")
set(SKIP_SUBMODULE_TEST ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
   set(CMAKE_BUILD_TYPE "StopAbieosFromTouchingThis")
   add_subdirectory(abieos)
   unset(CMAKE_BUILD_TYPE)
else()
   add_subdirectory(abieos)
endif()

add_subdirectory( TestHarness )
add_subdirectory( trx_generator )
add_subdirectory( performance_tests )

find_package(Threads)
add_executable(ship_client ship_client.cpp)
target_link_libraries(ship_client abieos Boost::program_options Boost::system Threads::Threads)
add_executable(ship_streamer ship_streamer.cpp)
target_link_libraries(ship_streamer abieos Boost::program_options Boost::system Threads::Threads)

add_test(NAME ship_test COMMAND tests/ship_test.py -v --num-clients 10 --num-requests 5000 --clean-run ${UNSHARE} WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_property(TEST ship_test PROPERTY LABELS nonparallelizable_tests)
add_test(NAME ship_test_unix COMMAND tests/ship_test.py -v --num-clients 10 --num-requests 5000 --clean-run ${UNSHARE} --unix-socket WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_property(TEST ship_test_unix PROPERTY LABELS nonparallelizable_tests)

add_test(NAME ship_streamer_test COMMAND tests/ship_streamer_test.py -v --num-clients 10 --num-blocks 50 --clean-run ${UNSHARE} WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_property(TEST ship_streamer_test PROPERTY LABELS nonparallelizable_tests)

add_test(NAME p2p_dawn515_test COMMAND tests/p2p_tests/dawn_515/test.sh WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_property(TEST p2p_dawn515_test PROPERTY LABELS nonparallelizable_tests)

add_test(NAME producer-preactivate-feature-test COMMAND tests/prod_preactivation_test.py --clean-run ${UNSHARE} WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_property(TEST producer-preactivate-feature-test PROPERTY LABELS nonparallelizable_tests)
add_test(NAME nodeos_protocol_feature_test COMMAND tests/nodeos_protocol_feature_test.py -v --clean-run ${UNSHARE} WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_property(TEST nodeos_protocol_feature_test PROPERTY LABELS nonparallelizable_tests)
add_test(NAME compute_transaction_test COMMAND tests/compute_transaction_test.py -v -p 2 -n 3 --clean-run ${UNSHARE} WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_property(TEST compute_transaction_test PROPERTY LABELS nonparallelizable_tests)
add_test(NAME read-only-trx-basic-test COMMAND tests/read_only_trx_test.py -v -p 2 -n 3 --read-only-threads 0 --clean-run ${UNSHARE} WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_property(TEST read-only-trx-basic-test PROPERTY LABELS nonparallelizable_tests)
add_test(NAME read-only-trx-parallel-test COMMAND tests/read_only_trx_test.py -v -p 2 -n 3 --read-only-threads 3 --num-test-runs 3 --clean-run ${UNSHARE} WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_property(TEST read-only-trx-parallel-test PROPERTY LABELS nonparallelizable_tests)
add_test(NAME read-only-trx-parallel-eos-vm-oc-test COMMAND tests/read_only_trx_test.py -v -p 2 -n 3 --eos-vm-oc-enable --read-only-threads 3 --num-test-runs 3 --clean-run ${UNSHARE} WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_property(TEST read-only-trx-parallel-eos-vm-oc-test PROPERTY LABELS nonparallelizable_tests)
add_test(NAME subjective_billing_test COMMAND tests/subjective_billing_test.py -v -p 2 -n 4 --clean-run ${UNSHARE} WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_property(TEST subjective_billing_test PROPERTY LABELS nonparallelizable_tests)
add_test(NAME get_account_test COMMAND tests/get_account_test.py -v -p 2 --clean-run ${UNSHARE} WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_property(TEST get_account_test PROPERTY LABELS nonparallelizable_tests)

add_test(NAME distributed-transactions-test COMMAND tests/distributed-transactions-test.py -d 2 -p 4 -n 6 -v --clean-run ${UNSHARE} WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_property(TEST distributed-transactions-test PROPERTY LABELS nonparallelizable_tests)
add_test(NAME restart-scenarios-test-resync COMMAND tests/restart-scenarios-test.py -c resync -p4 -v --clean-run ${UNSHARE} WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_property(TEST restart-scenarios-test-resync PROPERTY LABELS nonparallelizable_tests)
add_test(NAME restart-scenarios-test-hard_replay COMMAND tests/restart-scenarios-test.py -c hardReplay -p4 -v --clean-run ${UNSHARE} WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_property(TEST restart-scenarios-test-hard_replay PROPERTY LABELS nonparallelizable_tests)
add_test(NAME restart-scenarios-test-none COMMAND tests/restart-scenarios-test.py -c none --kill-sig term -p4 -v --clean-run ${UNSHARE} WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_property(TEST restart-scenarios-test-none PROPERTY LABELS nonparallelizable_tests)
add_test(NAME terminate-scenarios-test-resync COMMAND tests/terminate-scenarios-test.py -c resync --terminate-at-block 10 --kill-sig term ${UNSHARE} WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_property(TEST terminate-scenarios-test-resync PROPERTY LABELS nonparallelizable_tests)
add_test(NAME terminate-scenarios-test-replay COMMAND tests/terminate-scenarios-test.py -c replay --terminate-at-block 10 --kill-sig term ${UNSHARE} WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_property(TEST terminate-scenarios-test-replay PROPERTY LABELS nonparallelizable_tests)
add_test(NAME terminate-scenarios-test-hard_replay COMMAND tests/terminate-scenarios-test.py -c hardReplay --terminate-at-block 10 --kill-sig term ${UNSHARE} WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_property(TEST terminate-scenarios-test-hard_replay PROPERTY LABELS nonparallelizable_tests)
add_test(NAME validate_dirty_db_test COMMAND tests/validate-dirty-db.py -v --clean-run ${UNSHARE} WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_property(TEST validate_dirty_db_test PROPERTY LABELS nonparallelizable_tests)
add_test(NAME launcher_test COMMAND tests/launcher_test.py -v --clean-run ${UNSHARE} ${UNSHARE} WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_property(TEST launcher_test PROPERTY LABELS nonparallelizable_tests)
add_test(NAME keosd_auto_launch_test COMMAND tests/keosd_auto_launch_test.py WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_property(TEST keosd_auto_launch_test PROPERTY LABELS nonparallelizable_tests)
add_test(NAME nodeos_snapshot_diff_test COMMAND tests/nodeos_snapshot_diff_test.py -v --clean-run ${UNSHARE} WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_property(TEST nodeos_snapshot_diff_test PROPERTY LABELS nonparallelizable_tests)
add_test(NAME nodeos_snapshot_forked_test COMMAND tests/nodeos_snapshot_forked_test.py -v --clean-run ${UNSHARE} WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_property(TEST nodeos_snapshot_forked_test PROPERTY LABELS nonparallelizable_tests)

add_test(NAME trx_finality_status_test COMMAND tests/trx_finality_status_test.py -v --clean-run ${UNSHARE} WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_property(TEST trx_finality_status_test PROPERTY LABELS nonparallelizable_tests)

add_test(NAME trx_finality_status_forked_test COMMAND tests/trx_finality_status_forked_test.py -v --clean-run ${UNSHARE} WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_property(TEST trx_finality_status_forked_test PROPERTY LABELS nonparallelizable_tests)

add_test(NAME db_modes_test COMMAND tests/db_modes_test.sh -v WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(db_modes_test PROPERTIES COST 6000)
add_test(NAME release-build-test COMMAND tests/release-build.sh WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
add_test(NAME version-label-test COMMAND tests/version-label.sh "v${VERSION_FULL}" WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
add_test(NAME full-version-label-test COMMAND tests/full-version-label.sh "v${VERSION_FULL}" ${CMAKE_SOURCE_DIR} WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
add_test(NAME nested_container_multi_index_test COMMAND tests/nested_container_multi_index_test.py WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_property(TEST nested_container_multi_index_test PROPERTY LABELS nonparallelizable_tests)

add_test(NAME nodeos_run_check_test COMMAND tests/nodeos_run_test.py -v --clean-run ${UNSHARE} WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_property(TEST nodeos_run_check_test PROPERTY LABELS nonparallelizable_tests)

# needs iproute-tc or iproute2 depending on platform
#add_test(NAME p2p_high_latency_test COMMAND tests/p2p_high_latency_test.py -v --clean-run WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
#set_property(TEST p2p_high_latency_test PROPERTY LABELS nonparallelizable_tests)

#add_test(NAME distributed_transactions_lr_test COMMAND tests/distributed-transactions-test.py -d 2 -p 21 -n 21 -v --clean-run WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
#set_property(TEST distributed_transactions_lr_test PROPERTY LABELS long_running_tests)

add_test(NAME nodeos_forked_chain_lr_test COMMAND tests/nodeos_forked_chain_test.py -v --wallet-port 9901 --clean-run ${UNSHARE} WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_property(TEST nodeos_forked_chain_lr_test PROPERTY LABELS long_running_tests)

add_test(NAME nodeos_contrl_c_test COMMAND tests/nodeos_contrl_c_test.py -v --wallet-port 9901 ${UNSHARE} WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_property(TEST nodeos_contrl_c_test PROPERTY LABELS nonparallelizable_tests)

add_test(NAME nodeos_voting_lr_test COMMAND tests/nodeos_voting_test.py -v --wallet-port 9902 --clean-run ${UNSHARE} WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_property(TEST nodeos_voting_lr_test PROPERTY LABELS long_running_tests)

add_test(NAME nodeos_under_min_avail_ram_lr_test COMMAND tests/nodeos_under_min_avail_ram.py -v --wallet-port 9904 --clean-run ${UNSHARE} WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_property(TEST nodeos_under_min_avail_ram_lr_test PROPERTY LABELS long_running_tests)

add_test(NAME nodeos_irreversible_mode_lr_test COMMAND tests/nodeos_irreversible_mode_test.py -v --clean-run ${UNSHARE} WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_property(TEST nodeos_irreversible_mode_lr_test PROPERTY LABELS long_running_tests)

add_test(NAME nodeos_read_terminate_at_block_lr_test COMMAND tests/nodeos_read_terminate_at_block_test.py -v --clean-run ${UNSHARE} WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_property(TEST nodeos_read_terminate_at_block_lr_test PROPERTY LABELS long_running_tests)

add_test(NAME nodeos_chainbase_allocation_test COMMAND tests/nodeos_chainbase_allocation_test.py -v --clean-run ${UNSHARE} WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_property(TEST nodeos_chainbase_allocation_test PROPERTY LABELS nonparallelizable_tests)

add_test(NAME nodeos_startup_catchup_lr_test COMMAND tests/nodeos_startup_catchup.py -v --clean-run ${UNSHARE} WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_property(TEST nodeos_startup_catchup_lr_test PROPERTY LABELS long_running_tests)

add_test(NAME nodeos_short_fork_take_over_test COMMAND tests/nodeos_short_fork_take_over_test.py -v --wallet-port 9905 --clean-run ${UNSHARE} WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_property(TEST nodeos_short_fork_take_over_test PROPERTY LABELS nonparallelizable_tests)

add_test(NAME nodeos_extra_packed_data_test COMMAND tests/nodeos_extra_packed_data_test.py -v --clean-run -p 8 ${UNSHARE} WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_property(TEST nodeos_extra_packed_data_test PROPERTY LABELS nonparallelizable_tests)

add_test(NAME nodeos_producer_watermark_lr_test COMMAND tests/nodeos_producer_watermark_test.py -v --clean-run ${UNSHARE} WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_property(TEST nodeos_producer_watermark_lr_test PROPERTY LABELS long_running_tests)

add_test(NAME nodeos_high_transaction_lr_test COMMAND tests/nodeos_high_transaction_test.py -v --clean-run -p 4 -n 8 --num-transactions 10000 --max-transactions-per-second 500 ${UNSHARE} WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_property(TEST nodeos_high_transaction_lr_test PROPERTY LABELS long_running_tests)

add_test(NAME nodeos_retry_transaction_lr_test COMMAND tests/nodeos_retry_transaction_test.py -v --clean-run --num-transactions 100 --max-transactions-per-second 10 --total-accounts 5 ${UNSHARE} WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_property(TEST nodeos_retry_transaction_lr_test PROPERTY LABELS long_running_tests)


add_test(NAME cli_test COMMAND tests/cli_test.py WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_property(TEST cli_test PROPERTY LABELS nonparallelizable_tests)

add_test(NAME larger_lib_test COMMAND tests/large-lib-test.py ${UNSHARE} WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_property(TEST larger_lib_test PROPERTY LABELS nonparallelizable_tests)

add_test(NAME http_plugin_test COMMAND tests/http_plugin_test.py ${UNSHARE} WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(http_plugin_test PROPERTIES TIMEOUT 100)
set_property(TEST http_plugin_test PROPERTY LABELS nonparallelizable_tests)

add_test(NAME plugin_http_api_test COMMAND tests/plugin_http_api_test.py WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(plugin_http_api_test PROPERTIES TIMEOUT 50)
set_property(TEST plugin_http_api_test PROPERTY LABELS nonparallelizable_tests)

add_test(NAME trace_plugin_test COMMAND tests/trace_plugin_test.py -v WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_tests_properties(trace_plugin_test PROPERTIES TIMEOUT 300)
set_property(TEST trace_plugin_test PROPERTY LABELS nonparallelizable_tests)

add_test(NAME resource_monitor_plugin_test COMMAND tests/resource_monitor_plugin_test.py WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_property(TEST resource_monitor_plugin_test PROPERTY LABELS long_running_tests)

add_test(NAME nodeos_repeat_transaction_lr_test COMMAND tests/nodeos_high_transaction_test.py -v --clean-run -p 4 -n 8 --num-transactions 1000 --max-transactions-per-second 500 --send-duplicates ${UNSHARE} WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_property(TEST nodeos_repeat_transaction_lr_test PROPERTY LABELS long_running_tests)

add_test(NAME light_validation_sync_test COMMAND tests/light_validation_sync_test.py -v --clean-run ${UNSHARE} WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_property(TEST light_validation_sync_test PROPERTY LABELS nonparallelizable_tests)

add_test(NAME auto_bp_peering_test COMMAND tests/auto_bp_peering_test.py ${UNSHARE} WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
set_property(TEST auto_bp_peering_test PROPERTY LABELS long_running_tests)

if(ENABLE_COVERAGE_TESTING)

  set(Coverage_NAME ${PROJECT_NAME}_coverage)

  if(NOT LCOV_PATH)
    message(FATAL_ERROR "lcov not found! Aborting...")
  endif() # NOT LCOV_PATH

  if(NOT LLVMCOV_PATH)
    message(FATAL_ERROR "llvm-cov not found! Aborting...")
  endif()

  if(NOT GENHTML_PATH)
    message(FATAL_ERROR "genhtml not found! Aborting...")
  endif() # NOT GENHTML_PATH

  # no spaces allowed within tests list
  set(ctest_tests 'plugin_test|p2p_dawn515_test|nodeos_run_test|distributed-transactions-test|restart-scenarios-test_resync')
  set(ctest_exclude_tests 'distributed-transactions-remote-test|restart-scenarios-test_replay')

  # Setup target
  add_custom_target(${Coverage_NAME}

    COMMAND ${LCOV_PATH} --directory . --zerocounters

    COMMAND ctest -R ${ctest_tests} -E ${ctest_exclude_tests}

    COMMENT "Capturing lcov counters and generating report"
    COMMAND ${LCOV_PATH} --directory . --capture --gcov-tool ${CMAKE_SOURCE_DIR}/tools/llvm-gcov.sh --output-file ${Coverage_NAME}.info

    COMMENT "Processing code coverage counters and generating report."
    COMMAND ${GENHTML_PATH} -o ${Coverage_NAME} ${PROJECT_BINARY_DIR}/${Coverage_NAME}.info
    COMMAND ${CMAKE_COMMAND} -E remove ${Coverage_NAME}.base ${Coverage_NAME}.info ${Coverage_NAME}.total ${PROJECT_BINARY_DIR}/${Coverage_NAME}.info.cleaned

    WORKING_DIRECTORY ${PROJECT_BINARY_DIR}
    COMMENT "Resetting code coverage counters to zero. Processing code coverage counters and generating report."
    )

  # Show info where to find the report
  add_custom_command(TARGET ${Coverage_NAME} POST_BUILD
    COMMAND ;
    COMMENT "Open ./${Coverage_NAME}/index.html in your browser to view the coverage report."
    )
endif()

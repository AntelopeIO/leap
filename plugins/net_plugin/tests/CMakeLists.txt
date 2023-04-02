
add_executable(auto_bp_peering_unittest auto_bp_peering_unittest.cpp)

target_link_libraries(auto_bp_peering_unittest eosio_chain)

target_include_directories(auto_bp_peering_unittest PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../include" )

add_test(auto_bp_peering_unittest auto_bp_peering_unittest)

add_executable(address_manager_unittest address_manager_unittest.cpp)

target_link_libraries(address_manager_unittest eosio_chain net_plugin)

target_include_directories(address_manager_unittest PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../include" )

add_test(address_manager_unittest address_manager_unittest)

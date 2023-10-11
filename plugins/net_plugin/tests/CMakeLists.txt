
add_executable(auto_bp_peering_unittest auto_bp_peering_unittest.cpp)

target_link_libraries(auto_bp_peering_unittest eosio_chain)

target_include_directories(auto_bp_peering_unittest PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../include" )

add_test(auto_bp_peering_unittest auto_bp_peering_unittest)

add_executable(rate_limit_parse_unittest rate_limit_parse_unittest.cpp)

target_link_libraries(rate_limit_parse_unittest net_plugin)

target_include_directories(rate_limit_parse_unittest PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../include")

add_test(rate_limit_parse_unittest rate_limit_parse_unittest)

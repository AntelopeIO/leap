file(GLOB HEADERS "include/eosio/prometheus_plugin/*.hpp")
add_library( prometheus_plugin
        prometheus_plugin.cpp
        ${HEADERS} )

target_link_libraries( prometheus_plugin appbase fc http_plugin chain_plugin eosio_testing_contracts )
target_include_directories( prometheus_plugin PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
        prometheus_plugin PUBLIC ${CMAKE_SOURCE_DIR}/libraries/testing/include
        prometheus_plugin PUBLIC ${CMAKE_BINARY_DIR}/unittests/include )

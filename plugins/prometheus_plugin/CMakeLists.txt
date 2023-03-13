file(GLOB HEADERS "include/eosio/prometheus_plugin/*.hpp")
add_library( prometheus_plugin
        prometheus_plugin.cpp
        ${HEADERS} )

target_link_libraries( prometheus_plugin appbase fc prometheus-core http_plugin chain_plugin net_plugin)
target_include_directories( prometheus_plugin PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

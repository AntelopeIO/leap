file(GLOB HEADERS "include/eosio/net_plugin/*.hpp" )
add_library( net_plugin
             address_manager.cpp
             net_plugin.cpp
             ${HEADERS} )

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 14.0)
  target_compile_options(net_plugin PUBLIC -Wthread-safety)
endif()

target_link_libraries( net_plugin chain_plugin producer_plugin appbase fc )
target_include_directories( net_plugin PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/../chain_interface/include  "${CMAKE_CURRENT_SOURCE_DIR}/../../libraries/appbase/include")

add_subdirectory(tests)

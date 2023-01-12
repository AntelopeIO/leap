cmake_minimum_required( VERSION 3.8 )
project(testing_contracts)

if( EOSIO_COMPILE_TEST_CONTRACTS )
    set(EOSIO_WASM_OLD_BEHAVIOR "Off")
    if(USE_EOSIO_CDT_1_7_X OR USE_EOSIO_CDT_1_8_X)
        find_package( eosio.cdt REQUIRED )
    else()
        find_package( cdt REQUIRED )
    endif()
endif()

if ("${CMAKE_GENERATOR}" STREQUAL "Ninja")
    add_compile_options(-fcolor-diagnostics)
endif()

if( USE_EOSIO_CDT_1_7_X )
    add_definitions(-DUSE_EOSIO_CDT_1_7_X=true)
elseif( USE_EOSIO_CDT_1_8_X )
    add_definitions(-DUSE_EOSIO_CDT_1_8_X=true)
endif()

add_subdirectory(eosio.bios)

file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/old_versions/v1.6.0-rc3/eosio.bios/ DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/old_versions/v1.6.0-rc3/eosio.bios/)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/old_versions/v1.7.0-develop-preactivate_feature/eosio.bios/ DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/old_versions/v1.7.0-develop-preactivate_feature/eosio.bios/)

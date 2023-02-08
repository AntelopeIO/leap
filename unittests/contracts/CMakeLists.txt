cmake_minimum_required( VERSION 3.8 )
project(unittest_contracts)


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

add_subdirectory(eosio.msig)
add_subdirectory(eosio.system)
add_subdirectory(eosio.token)
add_subdirectory(eosio.wrap)

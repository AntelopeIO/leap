cmake_minimum_required(VERSION 3.5)
project(wasm_substitution_test)
set(EOSIO_WASM_OLD_BEHAVIOR "Off")
find_package(cdt)
include(CDTWasmToolchain)

add_contract(helloworld helloworld helloworld.cpp)
add_contract(debugworld debugworld debugworld.cpp)

target_include_directories(helloworld PUBLIC ${CMAKE_SOURCE_DIR})
target_include_directories(debugworld PUBLIC ${CMAKE_SOURCE_DIR})


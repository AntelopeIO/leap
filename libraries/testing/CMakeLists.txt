### Build contracts with cdt if available ###
include(ExternalProject)

if( EOSIO_COMPILE_TEST_CONTRACTS )
    set(EOSIO_WASM_OLD_BEHAVIOR "Off")
    if( USE_EOSIO_CDT_1_7_X OR USE_EOSIO_CDT_1_8_X )
        find_package( eosio.cdt REQUIRED )
        set(CMAKE_ARGS_VAL -DCMAKE_TOOLCHAIN_FILE=${EOSIO_CDT_ROOT}/lib/cmake/eosio.cdt/EosioWasmToolchain.cmake -DEOSIO_COMPILE_TEST_CONTRACTS=${EOSIO_COMPILE_TEST_CONTRACTS} )
    else()
        find_package( cdt REQUIRED )
        set(CMAKE_ARGS_VAL -DCMAKE_TOOLCHAIN_FILE=${CDT_ROOT}/lib/cmake/cdt/CDTWasmToolchain.cmake -DEOSIO_COMPILE_TEST_CONTRACTS=${EOSIO_COMPILE_TEST_CONTRACTS} )
    endif()

    if( USE_EOSIO_CDT_1_7_X )
        list(APPEND CMAKE_ARGS_VAL -DUSE_EOSIO_CDT_1_7_X=${USE_EOSIO_CDT_1_7_X})
    elseif( USE_EOSIO_CDT_1_8_X )
        list(APPEND CMAKE_ARGS_VAL -DUSE_EOSIO_CDT_1_8_X=${USE_EOSIO_CDT_1_8_X})
    endif()

    message( STATUS "Building contracts in directory `./libraries/contracts/`" )
    ExternalProject_Add(
            testing_contracts_project
            SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/contracts
            BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/contracts
            CMAKE_ARGS ${CMAKE_ARGS_VAL}
            UPDATE_COMMAND ""
            PATCH_COMMAND ""
            TEST_COMMAND ""
            INSTALL_COMMAND ""
            BUILD_ALWAYS 1
    )
else()
    message( STATUS "Not building contracts in directory `./libraries/contracts/`" )
    add_subdirectory(contracts)
endif()

file(GLOB HEADERS "include/eosio/testing/*.hpp")

configure_file(contracts.hpp.in include/testing_contracts/contracts.hpp ESCAPE_QUOTES)
add_library(eosio_testing_contracts INTERFACE)
target_include_directories(eosio_testing_contracts INTERFACE ${CMAKE_CURRENT_BINARY_DIR}/include/testing_contracts)

configure_file(contracts.cpp.in contracts.cpp ESCAPE_QUOTES)

## SORT .cpp by most likely to change / break compile
add_library( eosio_testing
             tester.cpp
             tester_network.cpp
             ${CMAKE_CURRENT_BINARY_DIR}/contracts.cpp
             ${HEADERS}
        )

target_link_libraries( eosio_testing eosio_testing_contracts eosio_chain hotstuff fc chainbase Logging IR WAST WASM)
target_include_directories( eosio_testing
                            PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" "${CMAKE_CURRENT_BINARY_DIR}/include"
                                   "${CMAKE_CURRENT_SOURCE_DIR}/../wasm-jit/Include"
                                   "${CMAKE_CURRENT_SOURCE_DIR}/contracts"
                                   "${CMAKE_CURRENT_BINARY_DIR}/contracts"
                          )

set_target_properties( eosio_testing PROPERTIES PUBLIC_HEADER "${HEADERS}" )
install( TARGETS eosio_testing
   RUNTIME DESTINATION ${CMAKE_INSTALL_FULL_BINDIR} COMPONENT dev EXCLUDE_FROM_ALL
   LIBRARY DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR} COMPONENT dev EXCLUDE_FROM_ALL
   ARCHIVE DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR} COMPONENT dev EXCLUDE_FROM_ALL
   PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_FULL_INCLUDEDIR}/eosio/testing COMPONENT dev EXCLUDE_FROM_ALL
)
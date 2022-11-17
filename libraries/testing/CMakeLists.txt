file(GLOB HEADERS "include/eosio/testing/*.hpp")

configure_file(contracts.hpp.in include/testing_contracts/contracts.hpp ESCAPE_QUOTES)
add_library(eosio_testing_contracts INTERFACE)
target_include_directories(eosio_testing_contracts INTERFACE ${CMAKE_CURRENT_BINARY_DIR}/include/testing_contracts)

configure_file(contracts.cpp.in contracts.cpp ESCAPE_QUOTES)

## SORT .cpp by most likely to change / break compile
add_library( eosio_testlib
             tester.cpp
             tester_network.cpp
             ${CMAKE_CURRENT_BINARY_DIR}/contracts.cpp
             ${HEADERS}
        )

target_link_libraries( eosio_testlib PUBLIC eosio_chain fc chainbase Logging IR WAST WASM Runtime )
target_include_directories( eosio_testlib
                            PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
                                   "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>"
                                   "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../wasm-jit/Include>"
                                   "$<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/unittests/include>"
                                   "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
                            PRIVATE "$<TARGET_PROPERTY:eosio_testing_contracts,INTERFACE_INCLUDE_DIRECTORIES>"
                            )

set_target_properties( eosio_testlib PROPERTIES PUBLIC_HEADER "${HEADERS}" )
install( TARGETS eosio_testlib
   RUNTIME DESTINATION ${CMAKE_INSTALL_FULL_BINDIR} COMPONENT dev EXCLUDE_FROM_ALL
   LIBRARY DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR} COMPONENT dev EXCLUDE_FROM_ALL
   ARCHIVE DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR} COMPONENT dev EXCLUDE_FROM_ALL
   PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_FULL_INCLUDEDIR}/eosio/testing COMPONENT dev EXCLUDE_FROM_ALL
)

add_library(eosio_testing INTERFACE)
target_link_libraries(eosio_testing INTERFACE eosio_testlib eosio_testing_contracts)
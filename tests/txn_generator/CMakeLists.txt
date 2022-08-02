add_executable( txn_generator main.cpp )

target_include_directories(txn_generator PUBLIC ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries( txn_generator
                       PRIVATE eosio_chain fc chain_plugin eosio_testing_contracts ${CMAKE_DL_LIBS} ${PLATFORM_SPECIFIC_LIBS} )

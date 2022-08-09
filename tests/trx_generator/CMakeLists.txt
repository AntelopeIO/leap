add_executable( trx_generator main.cpp trx_provider.cpp )

target_include_directories(trx_generator PUBLIC ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries( trx_generator
                       PRIVATE eosio_chain fc chain_plugin eosio_testing_contracts ${CMAKE_DL_LIBS} ${PLATFORM_SPECIFIC_LIBS} )

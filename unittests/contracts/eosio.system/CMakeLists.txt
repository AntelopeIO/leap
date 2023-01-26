if( EOSIO_COMPILE_TEST_CONTRACTS )
   add_contract( eosio.system eosio.system eosio.system.cpp )
   target_include_directories(eosio.system
           PUBLIC
           ${CMAKE_CURRENT_SOURCE_DIR}/include
           ${CMAKE_CURRENT_SOURCE_DIR}/../eosio.token)
else()
   configure_file( ${CMAKE_CURRENT_SOURCE_DIR}/eosio.system.wasm ${CMAKE_CURRENT_BINARY_DIR}/eosio.system.wasm COPYONLY )
   configure_file( ${CMAKE_CURRENT_SOURCE_DIR}/eosio.system.abi  ${CMAKE_CURRENT_BINARY_DIR}/eosio.system.abi  COPYONLY )
endif()

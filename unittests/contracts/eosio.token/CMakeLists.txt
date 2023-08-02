if( EOSIO_COMPILE_TEST_CONTRACTS )
   add_contract( eosio.token eosio.token eosio.token.cpp )
else()
   configure_file( ${CMAKE_CURRENT_SOURCE_DIR}/eosio.token.wasm ${CMAKE_CURRENT_BINARY_DIR}/eosio.token.wasm COPYONLY )
   configure_file( ${CMAKE_CURRENT_SOURCE_DIR}/eosio.token.abi  ${CMAKE_CURRENT_BINARY_DIR}/eosio.token.abi  COPYONLY )
endif()
configure_file( ${CMAKE_CURRENT_SOURCE_DIR}/eosio.token.bad.abi  ${CMAKE_CURRENT_BINARY_DIR}/eosio.token.bad.abi  COPYONLY )


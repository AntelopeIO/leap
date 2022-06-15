#pragma once

#include <eosio/eosio.hpp>

using bytes = std::vector<char>;

namespace eosio {
   namespace internal_use_do_not_use {
      extern "C" {
         __attribute__((eosio_wasm_import))
         uint32_t get_block_num(); 
      }
   }
}

class [[eosio::contract]] get_block_num_test : public eosio::contract {
public:
   using eosio::contract::contract;

   [[eosio::action]]
   void testblock(uint32_t expected_result);
};

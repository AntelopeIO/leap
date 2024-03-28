#pragma once

#include <eosio/eosio.hpp>
#include <eosio/crypto.hpp>
#include <eosio/ignore.hpp>

using eosio::ignore;
using eosio::name;
using eosio::permission_level;
using eosio::public_key;

namespace eosio {
   namespace internal_use_do_not_use {
      extern "C" {
         __attribute__((eosio_wasm_import))
         void create_slim_account(uint64_t creator, uint64_t account, const void* data, uint32_t len); 
      }
   }
}

struct permission_level_weight
{
   permission_level permission;
   uint16_t weight;

   // explicit serialization macro is not necessary, used here only to improve compilation time
   EOSLIB_SERIALIZE(permission_level_weight, (permission)(weight))
};

/**
 * Weighted key.
 *
 * A weighted key is defined by a public key and an associated weight.
 */
struct key_weight
{
   eosio::public_key key;
   uint16_t weight;

   // explicit serialization macro is not necessary, used here only to improve compilation time
   EOSLIB_SERIALIZE(key_weight, (key)(weight))
};

/**
 * Wait weight.
 *
 * A wait weight is defined by a number of seconds to wait for and a weight.
 */
struct wait_weight
{
   uint32_t wait_sec;
   uint16_t weight;

   // explicit serialization macro is not necessary, used here only to improve compilation time
   EOSLIB_SERIALIZE(wait_weight, (wait_sec)(weight))
};
struct authority
{
   uint32_t threshold = 0;
   std::vector<key_weight> keys;
   std::vector<permission_level_weight> accounts;
   std::vector<wait_weight> waits;

   // explicit serialization macro is not necessary, used here only to improve compilation time
   EOSLIB_SERIALIZE(authority, (threshold)(keys)(accounts)(waits))
};

class [[eosio::contract]] create_slim_account_test : public eosio::contract
{
public:
   using eosio::contract::contract;

   [[eosio::action]] void testcreate(eosio::name creator, eosio::name account, authority active_auth);
};

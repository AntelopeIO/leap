#pragma once

#include <fc/crypto/bls_public_key.hpp>
#include <string>

namespace eosio::chain {

   struct finalizer_authority {

      std::string  description;
      uint64_t     fweight = 0; // weight that this finalizer's vote has for meeting fthreshold
      fc::crypto::blslib::bls_public_key  public_key;

      auto operator<=>(const finalizer_authority&) const = default;
   };

} /// eosio::chain

FC_REFLECT( eosio::chain::finalizer_authority, (description)(fweight)(public_key) )

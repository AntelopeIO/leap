#pragma once

#include <fc/crypto/bls_private_key.hpp>
#include <fc/crypto/bls_public_key.hpp>
#include <eosio/chain/name.hpp>

namespace eosio::testing {

   inline auto get_bls_private_key( eosio::chain::name keyname ) {
      auto secret = fc::sha256::hash(keyname.to_string());
      std::vector<uint8_t> seed(secret.data_size());
      memcpy(seed.data(), secret.data(), secret.data_size());
      return fc::crypto::blslib::bls_private_key(seed);
   }

   inline std::tuple<fc::crypto::blslib::bls_private_key,
                     fc::crypto::blslib::bls_public_key,
                     fc::crypto::blslib::bls_signature> get_bls_key(eosio::chain::name keyname) {
      const auto private_key = get_bls_private_key(keyname);
      return { private_key, private_key.get_public_key(), private_key.proof_of_possession() };
   }

}
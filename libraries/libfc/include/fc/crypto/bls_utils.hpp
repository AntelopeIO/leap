#pragma once
#include <fc/crypto/bls_private_key.hpp>
#include <fc/crypto/bls_public_key.hpp>
#include <fc/crypto/bls_signature.hpp>

namespace fc::crypto::blslib {

   bool verify(const bls_public_key& pubkey,
               const vector<uint8_t>& message,
               const bls_signature& signature);

   bls_public_key aggregate(const vector<bls_public_key>& keys);

   bls_signature aggregate(const vector<bls_signature>& signatures);

   bool aggregate_verify(const vector<bls_public_key>& pubkeys,
                         const vector<vector<uint8_t>>& messages,
                         const bls_signature& signature);

} // fc::crypto::blslib

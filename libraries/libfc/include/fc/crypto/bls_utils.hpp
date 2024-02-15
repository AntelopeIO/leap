#pragma once
#include <fc/crypto/bls_private_key.hpp>
#include <fc/crypto/bls_public_key.hpp>
#include <fc/crypto/bls_signature.hpp>

namespace fc::crypto::blslib {

   bool verify(const bls_public_key& pubkey,
               std::span<const uint8_t> message,
               const bls_signature& signature);

   bls_public_key aggregate(std::span<const bls_public_key> keys);

   bls_signature aggregate(std::span<const bls_signature> signatures);

   bool aggregate_verify(std::span<const bls_public_key> pubkeys,
                         std::span<const std::vector<uint8_t>> messages,
                         const bls_signature& signature);

} // fc::crypto::blslib

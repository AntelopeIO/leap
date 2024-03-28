#pragma once
#include <fc/crypto/bls_private_key.hpp>
#include <fc/crypto/bls_public_key.hpp>
#include <fc/crypto/bls_signature.hpp>

namespace fc::crypto::blslib {

   bool verify(const bls_public_key& pubkey,
               std::span<const uint8_t> message,
               const bls_signature& signature);

} // fc::crypto::blslib

#include <fc/crypto/bls_utils.hpp>

namespace fc::crypto::blslib {

   bool verify(const bls_public_key& pubkey,
               std::span<const uint8_t> message,
               const bls_signature& signature) {
      return bls12_381::verify(pubkey.jacobian_montgomery_le(), message, signature.jacobian_montgomery_le());
   };

} // fc::crypto::blslib

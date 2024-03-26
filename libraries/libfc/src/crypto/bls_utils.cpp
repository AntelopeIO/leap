#include <fc/crypto/bls_utils.hpp>

namespace fc::crypto::blslib {

   bool verify(const bls_public_key& pubkey,
               std::span<const uint8_t> message,
               const bls_signature& signature) {
      return bls12_381::verify(pubkey.jacobian_montgomery_le(), message, signature._sig); //jacobian_montgomery_le());
   };

   bls_signature aggregate(std::span<const bls_signature> signatures) {
      std::vector<bls12_381::g2> sigs;
      sigs.reserve(signatures.size());
      for( const auto& s : signatures ) {
         sigs.push_back(s._sig);
      }

      bls12_381::g2 agg = bls12_381::aggregate_signatures(sigs);
      return bls_signature{agg};
   };

} // fc::crypto::blslib

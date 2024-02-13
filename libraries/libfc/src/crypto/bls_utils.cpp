#include <fc/crypto/bls_utils.hpp>

namespace fc::crypto::blslib {

   bool verify(const bls_public_key& pubkey,
               std::span<const uint8_t> message,
               const bls_signature& signature) {
      return bls12_381::verify(pubkey._pkey, message, signature._sig);
   };

   bls_public_key aggregate(std::span<const bls_public_key> keys) {
      std::vector<bls12_381::g1> ks;
      ks.reserve(keys.size());
      for( const auto& k : keys ) {
         ks.push_back(k._pkey);
      }
      bls12_381::g1 agg = bls12_381::aggregate_public_keys(ks);
      return bls_public_key(agg);
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

   bool aggregate_verify(std::span<const bls_public_key> pubkeys,
                         std::span<const std::vector<uint8_t>> messages, // should be `std::span<const std::span<const uint8_t>>`
                         const bls_signature& signature) {
      std::vector<bls12_381::g1> ks;
      ks.reserve(pubkeys.size());
      for( const auto& k : pubkeys ) {
         ks.push_back(k._pkey);
      }

      return bls12_381::aggregate_verify(ks, messages, signature._sig);
   };

} // fc::crypto::blslib

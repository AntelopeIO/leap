#include <fc/crypto/bls_utils.hpp>

namespace fc { namespace crypto { namespace blslib {

   bls_private_key generate() {

     char* r = (char*) malloc(32);

     rand_bytes(r, 32);

     vector<uint8_t> v(r, r+32);

     return bls_private_key(v);

   }

   bool verify(const bls_public_key &pubkey,
               const vector<uint8_t> &message,
               const bls_signature &signature) {
      return bls12_381::verify(pubkey._pkey, message, signature._sig);
   };

   bls_public_key aggregate(const vector<bls_public_key>& keys) {
      std::vector<bls12_381::g1> ks;
      ks.reserve(keys.size());
      for( const auto& k : keys ) {
         ks.push_back(k._pkey);
      }
      bls12_381::g1 agg = bls12_381::aggregate_public_keys(ks);
      return bls_public_key(agg);
   };

   bls_signature aggregate(const vector<bls_signature>& signatures) {
      std::vector<bls12_381::g2> sigs;
      sigs.reserve(signatures.size());

      bls12_381::g2 agg = bls12_381::aggregate_signatures(sigs);
      return bls_signature{agg};
   };

   bool aggregate_verify(const vector<bls_public_key>& pubkeys,
                         const vector<vector<uint8_t>>& messages,
                         const bls_signature& signature) {
      std::vector<bls12_381::g1> ks;
      ks.reserve(pubkeys.size());
      for( const auto& k : pubkeys ) {
         ks.push_back(k._pkey);
      }

      return bls12_381::aggregate_verify(ks, messages, signature._sig);
   };

} } }  // fc::crypto::blslib

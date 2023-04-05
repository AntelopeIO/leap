#include <fc/crypto/bls_utils.hpp>

namespace fc { namespace crypto { namespace blslib {

   bls_private_key generate() {

     char* r = (char*) malloc(32);

     rand_bytes(r, 32);

     vector<uint8_t> v(r, r+32);

     return bls_private_key(v);

   }

   bool verify( const bls_public_key &pubkey,
                  const vector<uint8_t> &message,
                  const bls_signature &signature){

      return PopSchemeMPL().Verify(pubkey._pkey, message, signature._sig);

   };

   bls_public_key aggregate( const vector<bls_public_key> &keys){

      G1Element aggKey;

      for (size_t i = 0 ; i < keys.size(); i++){
         aggKey += G1Element::FromByteVector(keys[i]._pkey);
      }

      return bls_public_key(aggKey.Serialize());

   };

   bls_signature aggregate( const vector<bls_signature> &signatures){

      vector<vector<uint8_t>> v_sigs;

      for (size_t i = 0 ; i < signatures.size(); i++)
         v_sigs.push_back(signatures[i]._sig);

      return bls_signature(PopSchemeMPL().Aggregate(v_sigs));

   };

   bool aggregate_verify( const vector<bls_public_key> &pubkeys,
                  const vector<vector<uint8_t>> &messages,
                  const bls_signature &signature){

      vector<vector<uint8_t>> v_pubkeys;

      for (size_t i = 0 ; i < pubkeys.size(); i++)
         v_pubkeys.push_back(pubkeys[i]._pkey);

      return PopSchemeMPL().AggregateVerify(v_pubkeys, messages, signature._sig);

   };

} } }  // fc::crypto::blslib

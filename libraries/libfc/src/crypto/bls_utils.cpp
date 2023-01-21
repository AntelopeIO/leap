#include <fc/crypto/bls_utils.hpp>
#include <fc/crypto/common.hpp>
#include <fc/exception/exception.hpp>
#include <bls.hpp>

namespace fc { namespace crypto { namespace blslib { namespace bls_utils {

   using namespace bls;

   bls_private_key generate() {

     char* r = (char*) malloc(32);

     rand_bytes(r, 32);

     std::vector<uint8_t> v(r, r+32);

     return bls_private_key(v);

   }

   bool verify( const bls_public_key &pubkey,
                  const std::vector<uint8_t> &message,
                  const bls_signature &signature){

      return bls::PopSchemeMPL().Verify(pubkey._pkey, message, signature._sig);

   };

   bls_public_key aggregate( const std::vector<bls_public_key> &keys){

      bls::G1Element aggKey;

      for (size_t i = 0 ; i < keys.size(); i++){
         aggKey += bls::G1Element::FromByteVector(keys[i]._pkey);
      }

      return bls_public_key(aggKey.Serialize());

   };

   bls_signature aggregate( const std::vector<bls_signature> &signatures){

      std::vector<std::vector<uint8_t>> v_sigs;

      for (size_t i = 0 ; i < signatures.size(); i++)
         v_sigs.push_back(signatures[i]._sig);

      return bls_signature(bls::PopSchemeMPL().Aggregate(v_sigs));

   };

   bool aggregate_verify( const std::vector<bls_public_key> &pubkeys,
                  const std::vector<std::vector<uint8_t>> &messages,
                  const bls_signature &signature){

      vector<vector<uint8_t>> v_pubkeys;

      for (size_t i = 0 ; i < pubkeys.size(); i++)
         v_pubkeys.push_back(pubkeys[i]._pkey);

      return bls::PopSchemeMPL().AggregateVerify(v_pubkeys, messages, signature._sig);

   };

} } } }  // fc::crypto::blslib::bls_utils
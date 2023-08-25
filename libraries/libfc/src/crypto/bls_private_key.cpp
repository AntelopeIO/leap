#include <fc/crypto/bls_private_key.hpp>
#include <fc/crypto/rand.hpp>
#include <fc/utility.hpp>
#include <fc/crypto/common.hpp>
#include <fc/exception/exception.hpp>
#include <fc/crypto/bls_common.hpp>

namespace fc::crypto::blslib {

   bls_public_key bls_private_key::get_public_key() const
   {
      bls12_381::g1 pk = bls12_381::public_key(_sk);
      return bls_public_key(pk);
   }

   bls_signature bls_private_key::sign( const std::vector<uint8_t>& message ) const
   {
      bls12_381::g2 sig = bls12_381::sign(_sk, message);
      return bls_signature(sig);
   }

   bls_private_key bls_private_key::generate() {
      std::vector<uint8_t> v(32);
      rand_bytes(reinterpret_cast<char*>(&v[0]), 32);
      return bls_private_key(v);
   }

   static std::array<uint64_t, 4> priv_parse_base58(const std::string& base58str)
   {  
      auto res = std::mismatch(config::bls_private_key_prefix.begin(), config::bls_private_key_prefix.end(),
                               base58str.begin());
      FC_ASSERT(res.first == config::bls_private_key_prefix.end(), "BLS Private Key has invalid format : ${str}", ("str", base58str));

      auto data_str = base58str.substr(config::bls_private_key_prefix.size());

      std::array<uint64_t, 4> bytes = fc::crypto::blslib::serialize_base58<std::array<uint64_t, 4>>(data_str);

      return bytes;
   }

   bls_private_key::bls_private_key(const std::string& base58str)
   :_sk(priv_parse_base58(base58str))
   {}

   std::string bls_private_key::to_string(const yield_function_t& yield) const
   {
      std::string data_str = fc::crypto::blslib::deserialize_base58<std::array<uint64_t, 4>>(_sk, yield); 

      return std::string(config::bls_private_key_prefix) + data_str;
   }

   bool operator == ( const bls_private_key& pk1, const bls_private_key& pk2) {
      return std::memcmp(&pk1, &pk2, sizeof(pk1)) == 0;
   }

} // fc::crypto::blslib

namespace fc
{
   void to_variant(const crypto::blslib::bls_private_key& var, variant& vo, const yield_function_t& yield)
   {
      vo = var.to_string(yield);
   }

   void from_variant(const variant& var, crypto::blslib::bls_private_key& vo)
   {
      vo = crypto::blslib::bls_private_key(var.as_string());
   }

} // fc

#include <fc/crypto/bls_private_key.hpp>
#include <fc/crypto/rand.hpp>
#include <fc/utility.hpp>
#include <fc/crypto/common.hpp>
#include <fc/exception/exception.hpp>
#include <fc/crypto/bls_common.hpp>

namespace fc::crypto::blslib {

   bls_public_key bls_private_key::get_public_key() const
   {
      //auto sk = bls12_381::secret_key(_seed);
      bls12_381::g1 pk = bls12_381::public_key(_sk);
      return bls_public_key(pk);
   }

   bls_signature bls_private_key::sign( const vector<uint8_t>& message ) const
   {
      //std::array<uint64_t, 4> sk = bls12_381::secret_key(_seed);
      bls12_381::g2 sig = bls12_381::sign(_sk, message);
      return bls_signature(sig);
   }

   bls_private_key bls_private_key::generate() {
      std::vector<uint8_t> v(32);
      rand_bytes(reinterpret_cast<char*>(&v[0]), 32);
      return bls_private_key(v);
   }

   static std::array<uint64_t, 4> priv_parse_base58(const string& base58str)
   {  

      const auto pivot = base58str.find('_');
      FC_ASSERT(pivot != std::string::npos, "No delimiter in string, cannot determine data type: ${str}", ("str", base58str));

      const auto base_prefix_str = base58str.substr(0, 3); //pvt
      FC_ASSERT(config::bls_private_key_base_prefix == base_prefix_str, "BLS Private Key has invalid base prefix: ${str}", ("str", base58str)("base_prefix_str", base_prefix_str));
      
      const auto prefix_str = base58str.substr(pivot + 1, 3); //bls
      FC_ASSERT(config::bls_private_key_prefix == prefix_str, "BLS Private Key has invalid prefix: ${str}", ("str", base58str)("prefix_str", prefix_str));

      auto data_str = base58str.substr(8);

      std::array<uint64_t, 4> bytes = fc::crypto::blslib::serialize_base58<std::array<uint64_t, 4>>(data_str);

      return bytes;

   }

   bls_private_key::bls_private_key(const std::string& base58str)
   :_sk(priv_parse_base58(base58str))
   {}

   std::string bls_private_key::to_string(const yield_function_t& yield) const
   {
      
      string data_str = fc::crypto::blslib::deserialize_base58<std::array<uint64_t, 4>>(_sk, yield); 

      return std::string(config::bls_private_key_base_prefix) + "_" + std::string(config::bls_private_key_prefix)+ "_" + data_str;
      
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

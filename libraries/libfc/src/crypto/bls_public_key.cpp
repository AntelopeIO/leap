#include <fc/crypto/bls_public_key.hpp>
#include <fc/crypto/common.hpp>
#include <fc/exception/exception.hpp>
#include <fc/crypto/bls_common.hpp>

namespace fc::crypto::blslib {

   static bls12_381::g1 pub_parse_base58(const std::string& base58str)
   {  
      auto res = std::mismatch(config::bls_public_key_prefix.begin(), config::bls_public_key_prefix.end(),
                               base58str.begin());
      FC_ASSERT(res.first == config::bls_public_key_prefix.end(), "BLS Public Key has invalid format : ${str}", ("str", base58str));

      auto data_str = base58str.substr(config::bls_public_key_prefix.size());

      std::array<uint8_t, 48> bytes = fc::crypto::blslib::serialize_base58<std::array<uint8_t, 48>>(data_str);
      
      std::optional<bls12_381::g1> g1 = bls12_381::g1::fromCompressedBytesBE(bytes);
      FC_ASSERT(g1);
      return *g1;
   }

   bls_public_key::bls_public_key(const std::string& base58str)
   :_pkey(pub_parse_base58(base58str))
   {}

   std::string bls_public_key::to_string(const yield_function_t& yield)const {

      std::array<uint8_t, 48> bytes = _pkey.toCompressedBytesBE();

      std::string data_str = fc::crypto::blslib::deserialize_base58<std::array<uint8_t, 48>>(bytes, yield); 

      return config::bls_public_key_prefix + data_str;

   }

   bool operator == ( const bls_public_key& p1, const bls_public_key& p2) {
      
      // until `bls12_381::g1` has an `operator==`, do binary comparison
      return std::memcmp(&p1._pkey, &p2._pkey, sizeof(p1._pkey)) == 0;
   }

} // fc::crypto::blslib

namespace fc
{
   using namespace std;
   void to_variant(const crypto::blslib::bls_public_key& var, variant& vo, const yield_function_t& yield)
   {
      vo = var.to_string(yield);
   }

   void from_variant(const variant& var, crypto::blslib::bls_public_key& vo)
   {
      vo = crypto::blslib::bls_public_key(var.as_string());
   }
} // fc

#include <fc/crypto/bls_public_key.hpp>
#include <fc/crypto/common.hpp>
#include <fc/exception/exception.hpp>
#include <fc/crypto/bls_common.hpp>

namespace fc::crypto::blslib {

   static bls12_381::g1 pub_parse_base64url(const std::string& base64urlstr)
   {  
      auto res = std::mismatch(config::bls_public_key_prefix.begin(), config::bls_public_key_prefix.end(),
                               base64urlstr.begin());
      FC_ASSERT(res.first == config::bls_public_key_prefix.end(), "BLS Public Key has invalid format : ${str}", ("str", base64urlstr));

      auto data_str = base64urlstr.substr(config::bls_public_key_prefix.size());

      std::array<uint8_t, 96> bytes = fc::crypto::blslib::deserialize_base64url<std::array<uint8_t, 96>>(data_str);
      
      constexpr bool check = true; // check if base64urlstr is invalid
      constexpr bool raw = false;  // non-montgomery
      std::optional<bls12_381::g1> g1 = bls12_381::g1::fromAffineBytesLE(bytes, check, raw);
      FC_ASSERT(g1);
      return *g1;
   }

   bls_public_key::bls_public_key(const std::string& base64urlstr)
   :_pkey(pub_parse_base64url(base64urlstr))
   {}

   std::string bls_public_key::to_string(const yield_function_t& yield)const {

      constexpr bool raw = false; // non-montgomery
      std::array<uint8_t, 96> bytes = _pkey.toAffineBytesLE(raw);

      std::string data_str = fc::crypto::blslib::serialize_base64url<std::array<uint8_t, 96>>(bytes);

      return config::bls_public_key_prefix + data_str;

   }

   bool bls_public_key::equal( const bls_public_key& pkey) const {
      return _pkey.equal(pkey._pkey);
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

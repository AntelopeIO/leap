#include <fc/crypto/bls_signature.hpp>
#include <fc/crypto/common.hpp>
#include <fc/exception/exception.hpp>
#include <fc/crypto/bls_common.hpp>

namespace fc::crypto::blslib {

   static bls12_381::g2 sig_parse_base64(const std::string& base64str)
   { 
      try {

         auto res = std::mismatch(config::bls_signature_prefix.begin(), config::bls_signature_prefix.end(),
                                  base64str.begin());
         FC_ASSERT(res.first == config::bls_signature_prefix.end(), "BLS Signature has invalid format : ${str}", ("str", base64str));

         auto data_str = base64str.substr(config::bls_signature_prefix.size());

         std::array<uint8_t, 192> bytes = fc::crypto::blslib::deserialize_base64<std::array<uint8_t, 192>>(data_str);

         constexpr bool check = true; // check if base64str is invalid
         constexpr bool raw = false;  // non-montgomery
         std::optional<bls12_381::g2> g2 = bls12_381::g2::fromAffineBytesLE(bytes, check, raw);
         FC_ASSERT(g2);
         return *g2;

      } FC_RETHROW_EXCEPTIONS( warn, "error parsing bls_signature", ("str", base64str ) ) 
   }

   bls_signature::bls_signature(const std::string& base64str)
     :_sig(sig_parse_base64(base64str))
   {}

   std::string bls_signature::to_string(const yield_function_t& yield) const
   {

      constexpr bool raw = false; // non-montgomery
      std::array<uint8_t, 192> bytes = _sig.toAffineBytesLE(raw);

      std::string data_str = fc::crypto::blslib::serialize_base64<std::array<uint8_t, 192>>(bytes);

      return config::bls_signature_prefix + data_str;

   }

   bool bls_signature::equal( const bls_signature& sig) const {
      return _sig.equal(sig._sig);
   }

} // fc::crypto::blslib

namespace fc
{
   void to_variant(const crypto::blslib::bls_signature& var, variant& vo, const yield_function_t& yield)
   {
      vo = var.to_string(yield);
   }

   void from_variant(const variant& var, crypto::blslib::bls_signature& vo)
   {
      vo = crypto::blslib::bls_signature(var.as_string());
   }
} // fc

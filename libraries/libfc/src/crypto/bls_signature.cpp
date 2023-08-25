#include <fc/crypto/bls_signature.hpp>
#include <fc/crypto/common.hpp>
#include <fc/exception/exception.hpp>
#include <fc/crypto/bls_common.hpp>

namespace fc::crypto::blslib {

   static bls12_381::g2 sig_parse_base58(const std::string& base58str)
   { 
      try {

         auto res = std::mismatch(config::bls_signature_prefix.begin(), config::bls_signature_prefix.end(),
                                  base58str.begin());
         FC_ASSERT(res.first == config::bls_signature_prefix.end(), "BLS Signature has invalid format : ${str}", ("str", base58str));

         auto data_str = base58str.substr(config::bls_signature_prefix.size());

         std::array<uint8_t, 96> bytes = fc::crypto::blslib::serialize_base58<std::array<uint8_t, 96>>(data_str);

         std::optional<bls12_381::g2> g2 = bls12_381::g2::fromCompressedBytesBE(bytes);
         FC_ASSERT(g2);
         return *g2;

      } FC_RETHROW_EXCEPTIONS( warn, "error parsing bls_signature", ("str", base58str ) ) 
   }

   bls_signature::bls_signature(const std::string& base58str)
     :_sig(sig_parse_base58(base58str))
   {}

   std::string bls_signature::to_string(const yield_function_t& yield) const
   {

      std::array<uint8_t, 96> bytes = _sig.toCompressedBytesBE();

      std::string data_str = fc::crypto::blslib::deserialize_base58<std::array<uint8_t, 96>>(bytes, yield); 

      return std::string(config::bls_signature_prefix) + data_str;

   }

   std::ostream& operator<<(std::ostream& s, const bls_signature& k) {
      s << "bls_signature(" << k.to_string() << ')';
      return s;
   }

   bool operator == ( const bls_signature& p1, const bls_signature& p2) {
      
      // until `bls12_381::g2` has an `operator==`, do binary comparison
      return std::memcmp(&p1._sig, &p2._sig, sizeof(p1._sig)) == 0;
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

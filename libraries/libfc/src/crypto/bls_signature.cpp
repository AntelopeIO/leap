#include <fc/crypto/bls_signature.hpp>
#include <fc/crypto/common.hpp>
#include <fc/exception/exception.hpp>
#include <fc/crypto/bls_common.hpp>

namespace fc::crypto::blslib {

   static bls12_381::g2 sig_parse_base58(const std::string& base58str)
   { try {

      const auto pivot = base58str.find('_');
      FC_ASSERT(pivot != std::string::npos, "No delimiter in string, cannot determine data type: ${str}", ("str", base58str));

      const auto base_prefix_str = base58str.substr(0, 3);
      FC_ASSERT(config::bls_signature_base_prefix == base_prefix_str, "BLS Signature has invalid base prefix: ${str}", ("str", base58str)("base_prefix_str", base_prefix_str));
      
      const auto prefix_str = base58str.substr(pivot + 1, 3);
      FC_ASSERT(config::bls_signature_prefix == prefix_str, "BLS Signature has invalid prefix: ${str}", ("str", base58str)("prefix_str", prefix_str));

      auto data_str = base58str.substr(8);

      std::array<uint8_t, 96> bytes = fc::crypto::blslib::serialize_base58<std::array<uint8_t, 96>>(data_str);

      std::optional<bls12_381::g2> g2 = bls12_381::g2::fromCompressedBytesBE(bytes);
      FC_ASSERT(g2);
      return *g2;
   } FC_RETHROW_EXCEPTIONS( warn, "error parsing bls_signature", ("str", base58str ) ) }

   bls_signature::bls_signature(const std::string& base58str)
     :_sig(sig_parse_base58(base58str))
   {}

   std::string bls_signature::to_string(const yield_function_t& yield) const
   {

      std::array<uint8_t, 96> bytes = _sig.toCompressedBytesBE();

      std::string data_str = fc::crypto::blslib::deserialize_base58<std::array<uint8_t, 96>>(bytes, yield); 

      return std::string(config::bls_signature_base_prefix) + "_" + std::string(config::bls_signature_prefix) + "_" + data_str;

   }

   std::ostream& operator<<(std::ostream& s, const bls_signature& k) {
      s << "bls_signature(" << k.to_string() << ')';
      return s;
   }

   bool operator == ( const bls_signature& p1, const bls_signature& p2) {
      return p1._sig == p2._sig;
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

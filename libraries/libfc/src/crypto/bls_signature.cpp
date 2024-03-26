#include <fc/crypto/bls_signature.hpp>
#include <fc/crypto/common.hpp>
#include <fc/exception/exception.hpp>
#include <fc/crypto/bls_common.hpp>

namespace fc::crypto::blslib {

   bls_signature::bls_signature(std::span<const uint8_t, 192> affine_non_montgomery_le) {
      std::ranges::copy(affine_non_montgomery_le, _affine_non_montgomery_le.begin());
      constexpr bool check = true;  // verify
      constexpr bool raw   = false; // to montgomery
      auto           g2    = bls12_381::g2::fromAffineBytesLE(affine_non_montgomery_le, check, raw);
      FC_ASSERT(g2, "Invalid bls_signature");
      _jacobian_montgomery_le = *g2;
   }

   static std::tuple<bls12_381::g2, std::array<uint8_t, 192>> sig_parse_base64url(const std::string& base64urlstr) {
      try {
         auto res = std::mismatch(config::bls_signature_prefix.begin(), config::bls_signature_prefix.end(),
                                  base64urlstr.begin());
         FC_ASSERT(res.first == config::bls_signature_prefix.end(), "BLS Signature has invalid format : ${str}", ("str", base64urlstr));

         auto data_str = base64urlstr.substr(config::bls_signature_prefix.size());

         std::array<uint8_t, 192> bytes = fc::crypto::blslib::deserialize_base64url<std::array<uint8_t, 192>>(data_str);

         constexpr bool check = true; // check if base64urlstr is invalid
         constexpr bool raw = false;  // non-montgomery
         std::optional<bls12_381::g2> g2 = bls12_381::g2::fromAffineBytesLE(bytes, check, raw);
         FC_ASSERT(g2);
         return {*g2, bytes};
      } FC_RETHROW_EXCEPTIONS( warn, "error parsing bls_signature", ("str", base64urlstr ) ) 
   }

   bls_signature::bls_signature(const std::string& base64urlstr) {
      std::tie(_jacobian_montgomery_le, _affine_non_montgomery_le) = sig_parse_base64url(base64urlstr);
   }

   std::string bls_signature::to_string() const {
      std::string data_str = fc::crypto::blslib::serialize_base64url<std::array<uint8_t, 192>>(_affine_non_montgomery_le);
      return config::bls_signature_prefix + data_str;
   }

   bls_aggregate_signature::bls_aggregate_signature(const std::string& base64urlstr) {
      std::tie(_jacobian_montgomery_le, std::ignore) = sig_parse_base64url(base64urlstr);
   }

   std::string bls_aggregate_signature::to_string() const {
      constexpr bool raw = false;
      std::array<uint8_t, 192> affine_non_montgomery_le = _jacobian_montgomery_le.toAffineBytesLE(raw);
      std::string data_str = fc::crypto::blslib::serialize_base64url<std::array<uint8_t, 192>>(affine_non_montgomery_le);
      return config::bls_signature_prefix + data_str;
   }

} // fc::crypto::blslib

namespace fc {

   void to_variant(const crypto::blslib::bls_signature& var, variant& vo) {
      vo = var.to_string();
   }

   void from_variant(const variant& var, crypto::blslib::bls_signature& vo) {
      vo = crypto::blslib::bls_signature(var.as_string());
   }

   void to_variant(const crypto::blslib::bls_aggregate_signature& var, variant& vo) {
      vo = var.to_string();
   }

   void from_variant(const variant& var, crypto::blslib::bls_aggregate_signature& vo) {
      vo = crypto::blslib::bls_aggregate_signature(var.as_string());
   }
} // fc

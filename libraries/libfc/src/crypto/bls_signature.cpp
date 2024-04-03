#include <fc/crypto/bls_signature.hpp>
#include <fc/crypto/common.hpp>
#include <fc/exception/exception.hpp>
#include <fc/crypto/bls_common.hpp>

namespace fc::crypto::blslib {

   bls12_381::g2 bls_signature::to_jacobian_montgomery_le(const std::array<uint8_t, 192>& affine_non_montgomery_le) {
      auto g2 = bls12_381::g2::fromAffineBytesLE(affine_non_montgomery_le, {.check_valid = true, .to_mont = true});
      FC_ASSERT(g2, "Invalid bls_signature");
      return *g2;
   }

   inline std::array<uint8_t, 192> from_span(std::span<const uint8_t, 192> affine_non_montgomery_le) {
      std::array<uint8_t, 192> r;
      std::ranges::copy(affine_non_montgomery_le, r.begin());
      return r;
   }

   bls_signature::bls_signature(std::span<const uint8_t, 192> affine_non_montgomery_le)
      : _affine_non_montgomery_le(from_span(affine_non_montgomery_le))
      , _jacobian_montgomery_le(to_jacobian_montgomery_le(_affine_non_montgomery_le)) {
   }

   static std::array<uint8_t, 192> sig_parse_base64url(const std::string& base64urlstr) {
      try {
         auto res = std::mismatch(config::bls_signature_prefix.begin(), config::bls_signature_prefix.end(), base64urlstr.begin());
         FC_ASSERT(res.first == config::bls_signature_prefix.end(), "BLS Signature has invalid format : ${str}", ("str", base64urlstr));
         auto data_str = base64urlstr.substr(config::bls_signature_prefix.size());
         return fc::crypto::blslib::deserialize_base64url<std::array<uint8_t, 192>>(data_str);
      } FC_RETHROW_EXCEPTIONS( warn, "error parsing bls_signature", ("str", base64urlstr ) )
   }

   bls_signature::bls_signature(const std::string& base64urlstr)
      : _affine_non_montgomery_le(sig_parse_base64url(base64urlstr))
      , _jacobian_montgomery_le(to_jacobian_montgomery_le(_affine_non_montgomery_le)) {
   }

   std::string bls_signature::to_string() const {
      std::string data_str = fc::crypto::blslib::serialize_base64url<std::array<uint8_t, 192>>(_affine_non_montgomery_le);
      return config::bls_signature_prefix + data_str;
   }

   bls_aggregate_signature::bls_aggregate_signature(const std::string& base64urlstr)
      : _jacobian_montgomery_le(bls_signature::to_jacobian_montgomery_le(sig_parse_base64url(base64urlstr))) {
   }

   std::string bls_aggregate_signature::to_string() const {
      std::array<uint8_t, 192> affine_non_montgomery_le = _jacobian_montgomery_le.toAffineBytesLE(bls12_381::from_mont::yes);
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

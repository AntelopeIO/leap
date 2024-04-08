#include <fc/crypto/bls_public_key.hpp>
#include <fc/crypto/common.hpp>
#include <fc/exception/exception.hpp>
#include <fc/crypto/bls_common.hpp>

namespace fc::crypto::blslib {

   inline std::array<uint8_t, 96> deserialize_base64url(const std::string& base64urlstr) {
      auto res = std::mismatch(config::bls_public_key_prefix.begin(), config::bls_public_key_prefix.end(), base64urlstr.begin());
      FC_ASSERT(res.first == config::bls_public_key_prefix.end(), "BLS Public Key has invalid format : ${str}", ("str", base64urlstr));
      auto data_str = base64urlstr.substr(config::bls_public_key_prefix.size());
      return fc::crypto::blslib::deserialize_base64url<std::array<uint8_t, 96>>(data_str);
   }

   bls12_381::g1 bls_public_key::from_affine_bytes_le(const std::array<uint8_t, 96>& affine_non_montgomery_le) {
      std::optional<bls12_381::g1> g1 =
         bls12_381::g1::fromAffineBytesLE(affine_non_montgomery_le, {.check_valid = true, .to_mont = true});
      FC_ASSERT(g1);
      return *g1;
   }

   inline std::array<uint8_t, 96> from_span(std::span<const uint8_t, 96> affine_non_montgomery_le) {
      std::array<uint8_t, 96> r;
      std::ranges::copy(affine_non_montgomery_le, r.begin());
      return r;
   }

   bls_public_key::bls_public_key(std::span<const uint8_t, 96> affine_non_montgomery_le)
      : _affine_non_montgomery_le(from_span(affine_non_montgomery_le))
      , _jacobian_montgomery_le(from_affine_bytes_le(_affine_non_montgomery_le)) {
   }

   bls_public_key::bls_public_key(const std::string& base64urlstr)
      : _affine_non_montgomery_le(deserialize_base64url(base64urlstr))
      , _jacobian_montgomery_le(from_affine_bytes_le(_affine_non_montgomery_le)) {
   }

   std::string bls_public_key::to_string() const {
      std::string data_str = fc::crypto::blslib::serialize_base64url<std::array<uint8_t, 96>>(_affine_non_montgomery_le);
      return config::bls_public_key_prefix + data_str;
   }

} // fc::crypto::blslib

namespace fc {

   void to_variant(const crypto::blslib::bls_public_key& var, variant& vo) {
      vo = var.to_string();
   }

   void from_variant(const variant& var, crypto::blslib::bls_public_key& vo) {
      vo = crypto::blslib::bls_public_key(var.as_string());
   }

} // fc

#pragma once
#include <fc/crypto/bls_signature.hpp>
#include <fc/reflect/reflect.hpp>
#include <fc/reflect/variant.hpp>
#include <bls12-381/bls12-381.hpp>
#include <algorithm>

namespace fc::crypto::blslib {

   namespace config {
      const std::string bls_public_key_prefix = "PUB_BLS_";
   };

   // Immutable after construction (although operator= is provided).
   //   Atributes are not const because FC_REFLECT only works for non-const members.
   // Provides an efficient wrapper around bls12_381::g1.
   // Serialization form:
   //   Non-Montgomery form and little-endian encoding for the field elements.
   //   Affine form for the group element (the z component is 1 and not included in the serialization).
   //   Binary serialization encodes x component first followed by y component.
   // Cached g1 in Jacobian Montgomery is used for efficient BLS math.
   // Keeping the serialized data allows for efficient serialization without the expensive conversion
   // from Jacobian Montgomery to Affine Non-Montgomery.
   class bls_public_key : fc::reflect_init {
   public:
      bls_public_key() = default;
      bls_public_key(bls_public_key&&) = default;
      bls_public_key(const bls_public_key&) = default;
      bls_public_key& operator=(const bls_public_key& rhs) = default;
      bls_public_key& operator=(bls_public_key&& rhs) = default;

      // throws if unable to convert to valid bls12_381::g1
      explicit bls_public_key(std::span<const uint8_t, 96> affine_non_montgomery_le);

      // affine non-montgomery base64url with bls_public_key_prefix
      explicit bls_public_key(const std::string& base64urlstr);

      // affine non-montgomery base64url with bls_public_key_prefix
      std::string to_string() const;

      const bls12_381::g1&            jacobian_montgomery_le() const { return _jacobian_montgomery_le; }
      const std::array<uint8_t, 96>&  affine_non_montgomery_le() const { return _affine_non_montgomery_le; }

      bool equal(const bls_public_key& pkey) const {
         return _jacobian_montgomery_le.equal(pkey._jacobian_montgomery_le);
      }

      auto operator<=>(const bls_public_key& rhs) const {
         return _affine_non_montgomery_le <=> rhs._affine_non_montgomery_le;
      }
      auto operator==(const bls_public_key& rhs) const {
         return _affine_non_montgomery_le == rhs._affine_non_montgomery_le;
      }

   private:
      friend struct fc::reflector<bls_public_key>;
      friend struct fc::reflector_init_visitor<bls_public_key>;
      friend struct fc::has_reflector_init<bls_public_key>;
      void reflector_init();

      std::array<uint8_t, 96> _affine_non_montgomery_le{};
      bls12_381::g1           _jacobian_montgomery_le; // cached g1
   };

}  // fc::crypto::blslib

namespace fc {
   void to_variant(const crypto::blslib::bls_public_key& var, variant& vo);
   void from_variant(const variant& var, crypto::blslib::bls_public_key& vo);
} // namespace fc

// Do not reflect cached g1, serialized form is Affine non-Montgomery little-endian
FC_REFLECT(crypto::blslib::bls_public_key, (_affine_non_montgomery_le) )

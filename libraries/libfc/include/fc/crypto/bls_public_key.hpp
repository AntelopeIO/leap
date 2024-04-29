#pragma once
#include <fc/reflect/variant.hpp>
#include <fc/io/varint.hpp>
#include <fc/exception/exception.hpp>
#include <bls12-381/bls12-381.hpp>



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
   //   Binary serialization encodes size(96), x component, followed by y component.
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

      template<typename T>
      friend T& operator<<(T& ds, const bls_public_key& sig) {
         // Serialization as variable length array when it is stored as a fixed length array. This makes for easier deserialization by external tools
         fc::raw::pack(ds, fc::unsigned_int(static_cast<uint32_t>(sizeof(sig._affine_non_montgomery_le))));
         ds.write(reinterpret_cast<const char*>(sig._affine_non_montgomery_le.data()), sizeof(sig._affine_non_montgomery_le));
         return ds;
      }

      template<typename T>
      friend T& operator>>(T& ds, bls_public_key& sig) {
         // Serialization as variable length array when it is stored as a fixed length array. This makes for easier deserialization by external tools
         fc::unsigned_int size;
         fc::raw::unpack( ds, size );
         FC_ASSERT(size.value == sizeof(sig._affine_non_montgomery_le));
         ds.read(reinterpret_cast<char*>(sig._affine_non_montgomery_le.data()), sizeof(sig._affine_non_montgomery_le));
         sig._jacobian_montgomery_le = from_affine_bytes_le(sig._affine_non_montgomery_le);
         return ds;
      }

      static bls12_381::g1 from_affine_bytes_le(const std::array<uint8_t, 96>& affine_non_montgomery_le);
   private:
      std::array<uint8_t, 96> _affine_non_montgomery_le{};
      bls12_381::g1           _jacobian_montgomery_le; // cached g1
   };

}  // fc::crypto::blslib

namespace fc {
   void to_variant(const crypto::blslib::bls_public_key& var, variant& vo);
   void from_variant(const variant& var, crypto::blslib::bls_public_key& vo);
} // namespace fc

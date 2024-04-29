#pragma once
#include <fc/reflect/variant.hpp>
#include <fc/io/varint.hpp>
#include <fc/exception/exception.hpp>
#include <bls12-381/bls12-381.hpp>


namespace fc::crypto::blslib {

   namespace config {
      const std::string bls_signature_prefix = "SIG_BLS_";
   };

   // Immutable after construction (although operator= is provided).
   // Provides an efficient wrapper around bls12_381::g2.
   // Serialization form:
   //   Non-Montgomery form and little-endian encoding for the field elements.
   //   Affine form for the group element (the z component is 1 and not included in the serialization).
   //   Binary serialization encodes size(192), x component, followed by y component.
   // Cached g2 in Jacobian Montgomery is used for efficient BLS math.
   // Keeping the serialized data allows for efficient serialization without the expensive conversion
   // from Jacobian Montgomery to Affine Non-Montgomery.
   class bls_signature {
   public:
      bls_signature() = default;
      bls_signature(bls_signature&&) = default;
      bls_signature(const bls_signature&) = default;
      bls_signature& operator=(const bls_signature&) = default;
      bls_signature& operator=(bls_signature&&) = default;

      // throws if unable to convert to valid bls12_381::g2
      explicit bls_signature(std::span<const uint8_t, 192> affine_non_montgomery_le);

      // affine non-montgomery base64url with bls_signature_prefix
      explicit bls_signature(const std::string& base64urlstr);

      // affine non-montgomery base64url with bls_signature_prefix
      std::string to_string() const;

      const bls12_381::g2&            jacobian_montgomery_le() const { return _jacobian_montgomery_le; }
      const std::array<uint8_t, 192>& affine_non_montgomery_le() const { return _affine_non_montgomery_le; }

      bool equal(const bls_signature& sig) const {
         return _jacobian_montgomery_le.equal(sig._jacobian_montgomery_le);
      }

      template<typename T>
      friend T& operator<<(T& ds, const bls_signature& sig) {
         // Serialization as variable length array when it is stored as a fixed length array. This makes for easier deserialization by external tools
         fc::raw::pack(ds, fc::unsigned_int(static_cast<uint32_t>(sizeof(sig._affine_non_montgomery_le))));
         ds.write(reinterpret_cast<const char*>(sig._affine_non_montgomery_le.data()), sizeof(sig._affine_non_montgomery_le));
         return ds;
      }

      // Could use FC_REFLECT, but to make it obvious serialization matches bls_aggregate_signature implement via operator
      template<typename T>
      friend T& operator>>(T& ds, bls_signature& sig) {
         // Serialization as variable length array when it is stored as a fixed length array. This makes for easier deserialization by external tools
         fc::unsigned_int size;
         fc::raw::unpack( ds, size );
         FC_ASSERT(size.value == sizeof(sig._affine_non_montgomery_le));
         ds.read(reinterpret_cast<char*>(sig._affine_non_montgomery_le.data()), sizeof(sig._affine_non_montgomery_le));
         sig._jacobian_montgomery_le = to_jacobian_montgomery_le(sig._affine_non_montgomery_le);
         return ds;
      }

      static bls12_381::g2 to_jacobian_montgomery_le(const std::array<uint8_t, 192>& affine_non_montgomery_le);
   private:
      std::array<uint8_t, 192> _affine_non_montgomery_le{};
      bls12_381::g2            _jacobian_montgomery_le; // cached g2
   };

   // See bls_signature comment above
   class bls_aggregate_signature {
   public:
      bls_aggregate_signature() = default;
      bls_aggregate_signature(bls_aggregate_signature&&) = default;
      bls_aggregate_signature(const bls_aggregate_signature&) = default;
      bls_aggregate_signature& operator=(const bls_aggregate_signature&) = default;
      bls_aggregate_signature& operator=(bls_aggregate_signature&&) = default;

      // affine non-montgomery base64url with bls_signature_prefix
      explicit bls_aggregate_signature(const std::string& base64urlstr);

      explicit bls_aggregate_signature(const bls_signature& sig)
         : _jacobian_montgomery_le(sig.jacobian_montgomery_le()) {}

      // aggregate signature into this
      void aggregate(const bls_signature& sig) {
         _jacobian_montgomery_le.addAssign(sig.jacobian_montgomery_le());
      }
      // aggregate signature into this
      void aggregate(const bls_aggregate_signature& sig) {
         _jacobian_montgomery_le.addAssign(sig.jacobian_montgomery_le());
      }

      // affine non-montgomery base64url with bls_signature_prefix
      // Expensive as conversion from Jacobian Montgomery to Affine Non-Montgomery needed
      std::string to_string() const;

      const bls12_381::g2& jacobian_montgomery_le() const { return _jacobian_montgomery_le; }

      bool equal( const bls_aggregate_signature& sig) const {
         return _jacobian_montgomery_le.equal(sig._jacobian_montgomery_le);
      }

      template<typename T>
      friend T& operator<<(T& ds, const bls_aggregate_signature& sig) {
         std::array<uint8_t, 192> affine_non_montgomery_le =
            sig._jacobian_montgomery_le.toAffineBytesLE(bls12_381::from_mont::yes);
         // Serialization as variable length array when it is stored as a fixed length array.
         // This makes for easier deserialization by external tools
         fc::raw::pack(ds, fc::unsigned_int(static_cast<uint32_t>(sizeof(affine_non_montgomery_le))));
         ds.write(reinterpret_cast<const char*>(affine_non_montgomery_le.data()), sizeof(affine_non_montgomery_le));
         return ds;
      }

      // Could use FC_REFLECT, but to make it obvious serialization matches bls_signature implement via operator
      template<typename T>
      friend T& operator>>(T& ds, bls_aggregate_signature& sig) {
         // Serialization as variable length array when it is stored as a fixed length array.
         // This makes for easier deserialization by external tools
         fc::unsigned_int size;
         fc::raw::unpack( ds, size );
         std::array<uint8_t, 192> affine_non_montgomery_le;
         FC_ASSERT(size.value == sizeof(affine_non_montgomery_le));
         ds.read(reinterpret_cast<char*>(affine_non_montgomery_le.data()), sizeof(affine_non_montgomery_le));
         sig._jacobian_montgomery_le = bls_signature::to_jacobian_montgomery_le(affine_non_montgomery_le);
         return ds;
      }

   private:
      bls12_381::g2  _jacobian_montgomery_le;
   };

}  // fc::crypto::blslib

namespace fc {

   void to_variant(const crypto::blslib::bls_signature& var, variant& vo);
   void from_variant(const variant& var, crypto::blslib::bls_signature& vo);
   void to_variant(const crypto::blslib::bls_aggregate_signature& var, variant& vo);
   void from_variant(const variant& var, crypto::blslib::bls_aggregate_signature& vo);

} // namespace fc

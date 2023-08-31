#pragma once
#include <fc/static_variant.hpp>
#include <fc/crypto/elliptic.hpp>
#include <fc/crypto/elliptic_r1.hpp>
#include <fc/crypto/elliptic_webauthn.hpp>
#include <fc/crypto/bls_public_key.hpp>
#include <fc/reflect/reflect.hpp>
#include <fc/reflect/variant.hpp>
#include <bls12-381/bls12-381.hpp>

#include <functional>

namespace fc::crypto::blslib {

   namespace config {
      const std::string bls_signature_prefix = "SIG_BLS_";
   };
   
   class bls_signature
   {
      public:

         bls_signature() = default;
         bls_signature( bls_signature&& ) = default;
         bls_signature( const bls_signature& ) = default;
         explicit bls_signature( const bls12_381::g2& sig ){_sig = sig;}
         explicit bls_signature(const std::string& base58str);

         bls_signature& operator= (const bls_signature& ) = default;
         std::string to_string(const yield_function_t& yield = yield_function_t()) const;
         friend bool operator == ( const bls_signature& p1, const bls_signature& p2);

         bls12_381::g2 _sig;

   }; // bls_signature

}  // fc::crypto::blslib

// for std::unordered_set<bls_signature>
namespace std {
   template <>
   struct hash<fc::crypto::blslib::bls_signature> {
      size_t operator()(const fc::crypto::blslib::bls_signature& obj) const {
         size_t seed = 0;
         const auto& x_c0_d = obj._sig.x.c0.d;
         for (const auto& val : x_c0_d) seed ^= val;
         const auto& x_c1_d = obj._sig.x.c1.d;
         for (const auto& val : x_c1_d) seed ^= val;
         return seed;
      }
   };
}

namespace fc {
   void to_variant(const crypto::blslib::bls_signature& var, variant& vo, const yield_function_t& yield = yield_function_t());

   void from_variant(const variant& var, crypto::blslib::bls_signature& vo);
} // namespace fc

FC_REFLECT(bls12_381::fp, (d))
FC_REFLECT(bls12_381::fp2, (c0)(c1))
FC_REFLECT(bls12_381::g2, (x)(y)(z))
FC_REFLECT(crypto::blslib::bls_signature, (_sig) )

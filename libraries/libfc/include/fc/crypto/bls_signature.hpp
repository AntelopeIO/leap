#pragma once
#include <fc/static_variant.hpp>
#include <fc/crypto/elliptic.hpp>
#include <fc/crypto/elliptic_r1.hpp>
#include <fc/crypto/elliptic_webauthn.hpp>
#include <fc/crypto/bls_public_key.hpp>
#include <fc/reflect/reflect.hpp>
#include <fc/reflect/variant.hpp>
#include <bls12-381/bls12-381.hpp>

namespace fc::crypto::blslib {

   using namespace std;

   namespace config {
      constexpr const char* bls_signature_base_prefix = "SIG";
      constexpr const char* bls_signature_prefix = "BLS";
   };
   
   class bls_signature
   {
      public:

         bls_signature() = default;
         bls_signature( bls_signature&& ) = default;
         bls_signature( const bls_signature& ) = default;
         bls_signature& operator= (const bls_signature& ) = default;

         bls_signature( bls12_381::g2 sig ){
            _sig = sig;
         }

         explicit bls_signature(const string& base58str);
         std::string to_string(const yield_function_t& yield = yield_function_t()) const;


         bls12_381::g2 _sig;

      private:

         friend bool operator == ( const bls_signature& p1, const bls_signature& p2);
         friend struct reflector<bls_signature>;
         friend class bls_private_key;
         friend class bls_public_key;
   }; // bls_signature

}  // fc::crypto::blslib

namespace fc {
   void to_variant(const crypto::blslib::bls_signature& var, variant& vo, const yield_function_t& yield = yield_function_t());

   void from_variant(const variant& var, crypto::blslib::bls_signature& vo);
} // namespace fc

FC_REFLECT(bls12_381::fp, (d))
FC_REFLECT(bls12_381::fp2, (c0)(c1))
FC_REFLECT(bls12_381::g2, (x)(y)(z))
FC_REFLECT(crypto::blslib::bls_signature, (_sig) )

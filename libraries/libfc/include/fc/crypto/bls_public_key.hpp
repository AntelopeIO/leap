#pragma once
#include <fc/crypto/elliptic.hpp>
#include <fc/crypto/elliptic_r1.hpp>
#include <fc/crypto/elliptic_webauthn.hpp>
#include <fc/crypto/bls_signature.hpp>
#include <fc/reflect/reflect.hpp>
#include <fc/reflect/variant.hpp>
#include <fc/static_variant.hpp>
#include <bls12-381/bls12-381.hpp>

namespace fc::crypto::blslib {

   using namespace std;

   namespace config {
      constexpr const char* bls_public_key_base_prefix = "PUB";
      constexpr const char* bls_public_key_prefix = "BLS";
   };

   class bls_public_key
   {
      public:
 
         bls_public_key() = default;
         bls_public_key( bls_public_key&& ) = default;
         bls_public_key( const bls_public_key& ) = default;
         bls_public_key& operator= (const bls_public_key& ) = default;

         bls_public_key( bls12_381::g1 pkey ){
            _pkey = pkey;
         }

         explicit bls_public_key(const string& base58str);

         std::string to_string(const yield_function_t& yield = yield_function_t()) const;

         bls12_381::g1 _pkey;

      private:
         

         friend std::ostream& operator<< (std::ostream& s, const bls_public_key& k);
         friend struct reflector<bls_public_key>;
         friend class bls_private_key;
   }; // bls_public_key

}  // fc::crypto::blslib

namespace fc {
   void to_variant(const crypto::blslib::bls_public_key& var, variant& vo, const yield_function_t& yield = yield_function_t());

   void from_variant(const variant& var, crypto::blslib::bls_public_key& vo);
} // namespace fc

FC_REFLECT(bls12_381::g1, (x)(y)(z))
FC_REFLECT(crypto::blslib::bls_public_key, (_pkey) )

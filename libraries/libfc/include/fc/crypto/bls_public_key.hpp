#pragma once
#include <fc/crypto/elliptic.hpp>
#include <fc/crypto/elliptic_r1.hpp>
#include <fc/crypto/elliptic_webauthn.hpp>
#include <fc/crypto/bls_signature.hpp>
#include <fc/reflect/reflect.hpp>
#include <fc/reflect/variant.hpp>
#include <fc/static_variant.hpp>
#include <bls12-381/bls12-381.hpp>

namespace fc { namespace crypto { namespace blslib {

   using namespace std;

   namespace config {
      constexpr const char* bls_public_key_legacy_prefix = "EOS";
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

/*         bls_public_key( G1Element pkey ){
            _pkey = pkey.Serialize();
         }
*/
         //bls_public_key( const bls_signature& c, const sha256& digest, bool check_canonical = true );

/*         bls_public_key( storage_type&& other_storage )
         :_storage(forward<storage_type>(other_storage))
         {}
*/
         bool valid()const;

         size_t which()const;

         // serialize to/from string
         explicit bls_public_key(const string& base58str);
         //std::string to_string() const;
         //std::string to_string() ;

         std::string to_string(const fc::yield_function_t& yield = fc::yield_function_t()) const;

         //storage_type _storage;
         bls12_381::g1 _pkey;

      private:
         

         friend std::ostream& operator<< (std::ostream& s, const bls_public_key& k);
      friend bool operator == ( const bls_public_key& p1, const bls_public_key& p2) { return p1._pkey.equal(p2._pkey); }
      friend bool operator != ( const bls_public_key& p1, const bls_public_key& p2) { return !p1._pkey.equal(p2._pkey);  }
      friend bool operator < ( const bls_public_key& p1, const bls_public_key& p2) { return p1._pkey.affine().x.cmp(p2._pkey.affine().x) < 0; }
         friend struct reflector<bls_public_key>;
         friend class bls_private_key;
   }; // bls_public_key

} } }  // fc::crypto::blslib

namespace fc {
   void to_variant(const crypto::blslib::bls_public_key& var, variant& vo, const fc::yield_function_t& yield = fc::yield_function_t());

   void from_variant(const variant& var, crypto::blslib::bls_public_key& vo);
} // namespace fc

FC_REFLECT(bls12_381::g1, (x)(y)(z))
FC_REFLECT(fc::crypto::blslib::bls_public_key, (_pkey) )

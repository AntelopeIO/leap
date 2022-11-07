#pragma once
#include <fc/crypto/elliptic.hpp>
#include <fc/crypto/elliptic_r1.hpp>
#include <fc/crypto/bls_public_key.hpp>
#include <fc/crypto/rand.hpp>
#include <fc/reflect/reflect.hpp>
#include <fc/reflect/variant.hpp>
#include <fc/static_variant.hpp>
#include <bls.hpp>

namespace fc { namespace crypto { namespace blslib {

   using namespace bls;

   namespace config {
      constexpr const char* bls_private_key_base_prefix = "PVT";
      constexpr const char* bls_private_key_prefix = "BLS";
   };

   class bls_private_key
   {
      public:

         bls_private_key() = default;
         bls_private_key( bls_private_key&& ) = default;
         bls_private_key( const bls_private_key& ) = default;
         bls_private_key& operator= (const bls_private_key& ) = default;

         bls_private_key( vector<uint8_t> seed ){
            _seed = seed;
         }

         bls_public_key     get_public_key() const;
         bls_signature      sign( vector<uint8_t> message ) const;
         sha512         generate_shared_secret( const bls_public_key& pub ) const;

         std::string to_string(const fc::yield_function_t& yield = fc::yield_function_t()) const;

         static bls_private_key generate() {

            char* r = (char*) malloc(32);

            rand_bytes(r, 32);
            
            vector<uint8_t> v(r, r+32);

            return bls_private_key(v);
         }

/*         template< typename KeyType = r1::private_key_shim >
         static bls_private_key generate_r1() {
            return bls_private_key(storage_type(KeyType::generate()));
         }*/

         static bls_private_key regenerate( vector<uint8_t> seed ) {
            return bls_private_key(seed);
         }

         // serialize to/from string
         explicit bls_private_key(const string& base58str);

         std::string serialize();

      private:
         vector<uint8_t> _seed;

         friend bool operator == ( const bls_private_key& p1, const bls_private_key& p2);
         friend bool operator != ( const bls_private_key& p1, const bls_private_key& p2);
         friend bool operator < ( const bls_private_key& p1, const bls_private_key& p2);
         friend struct reflector<bls_private_key>;
   }; // bls_private_key

} } }  // fc::crypto::blslib

namespace fc {
   void to_variant(const crypto::blslib::bls_private_key& var, variant& vo, const fc::yield_function_t& yield = fc::yield_function_t());

   void from_variant(const variant& var, crypto::blslib::bls_private_key& vo);
} // namespace fc

FC_REFLECT(fc::crypto::blslib::bls_private_key, (_seed) )

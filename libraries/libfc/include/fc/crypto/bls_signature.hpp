#pragma once
#include <fc/static_variant.hpp>
#include <fc/crypto/elliptic.hpp>
#include <fc/crypto/elliptic_r1.hpp>
#include <fc/crypto/elliptic_webauthn.hpp>
#include <fc/crypto/bls_public_key.hpp>
#include <fc/reflect/reflect.hpp>
#include <fc/reflect/variant.hpp>
//#include <bls.hpp>

namespace fc { namespace crypto { namespace blslib {

   using namespace std;

   namespace config {
      constexpr const char* bls_signature_base_prefix = "SIG";
      constexpr const char* bls_signature_prefix = "BLS";
   };

   class bls_signature
   {
      public:
         //using storage_type = ecc::signature_shim;//std::variant<ecc::signature_shim, r1::signature_shim, webauthn::signature>;

         bls_signature() = default;
         bls_signature( bls_signature&& ) = default;
         bls_signature( const bls_signature& ) = default;
         bls_signature& operator= (const bls_signature& ) = default;

         bls_signature( std::vector<uint8_t> sig ){
            _sig = sig;
         }

/*         bls_signature( G2Element sig ){
            _sig = sig.Serialize();
         }
*/
         // serialize to/from string
         explicit bls_signature(const string& base58str);
         std::string to_string(const fc::yield_function_t& yield = fc::yield_function_t()) const;

         size_t which() const;

         size_t variable_size() const;


         std::vector<uint8_t> _sig;

      private:

         //storage_type _storage;

/*         bls_signature( storage_type&& other_storage )
         :_storage(std::forward<storage_type>(other_storage))
         {}
*/
         //friend bool operator == ( const bls_signature& p1, const bls_signature& p2);
         //friend bool operator != ( const bls_signature& p1, const bls_signature& p2);
        //friend bool operator < ( const bls_signature& p1, const bls_signature& p2);
         friend std::size_t hash_value(const bls_signature& b); //not cryptographic; for containers
         friend bool operator == ( const bls_signature& p1, const bls_signature& p2);
         friend struct reflector<bls_signature>;
         friend class bls_private_key;
         friend class bls_public_key;
   }; // bls_public_key

   //size_t hash_value(const bls_signature& b);

} } }  // fc::crypto::blslib

namespace fc {
   void to_variant(const crypto::blslib::bls_signature& var, variant& vo, const fc::yield_function_t& yield = fc::yield_function_t());

   void from_variant(const variant& var, crypto::blslib::bls_signature& vo);
} // namespace fc

namespace std {
   template <> struct hash<fc::crypto::blslib::bls_signature> {
      std::size_t operator()(const fc::crypto::blslib::bls_signature& k) const {
         //return fc::crypto::hash_value(k);
         return 0;
      }
   };
} // std

FC_REFLECT(fc::crypto::blslib::bls_signature, (_sig) )

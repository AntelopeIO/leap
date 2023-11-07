#pragma once
#include <fc/crypto/bls_public_key.hpp>
#include <fc/crypto/bls_signature.hpp>
#include <fc/reflect/reflect.hpp>
#include <fc/reflect/variant.hpp>

namespace fc::crypto::blslib {

   namespace config {
      const std::string bls_private_key_prefix = "PVT_BLS_";
   };

   class bls_private_key
   {
      public:

         bls_private_key() = default;
         bls_private_key( bls_private_key&& ) = default;
         bls_private_key( const bls_private_key& ) = default;
         explicit bls_private_key(const std::vector<uint8_t>& seed ) {
            _sk = bls12_381::secret_key(seed);
         }
         explicit bls_private_key(const std::string& base64str);

         bls_private_key& operator=( const bls_private_key& ) = default;

         std::string to_string(const yield_function_t& yield = yield_function_t()) const;

         bls_public_key     get_public_key() const;
         bls_signature      sign( const std::vector<uint8_t>& message ) const;
         bls_signature      proof_of_possession() const;

         static bls_private_key generate();

      private:
         std::array<uint64_t, 4> _sk;
         friend bool operator == ( const bls_private_key& pk1, const bls_private_key& pk2);
         friend struct reflector<bls_private_key>;
   }; // bls_private_key

} // fc::crypto::blslib

namespace fc {
   void to_variant(const crypto::blslib::bls_private_key& var, variant& vo, const yield_function_t& yield = yield_function_t());

   void from_variant(const variant& var, crypto::blslib::bls_private_key& vo);
} // namespace fc

FC_REFLECT(crypto::blslib::bls_private_key, (_sk) )

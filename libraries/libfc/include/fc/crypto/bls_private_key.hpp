#pragma once
#include <fc/crypto/bls_public_key.hpp>
#include <fc/reflect/reflect.hpp>
#include <fc/reflect/variant.hpp>

namespace fc::crypto::blslib {

   namespace config {
      constexpr const char* bls_private_key_base_prefix = "PVT";
      constexpr const char* bls_private_key_prefix = "BLS";
      //constexpr const char* bls_private_key_prefix[] = {"BLS"};
   };

   class bls_private_key
   {
      public:

         bls_private_key() = default;
         bls_private_key( bls_private_key&& ) = default;
         bls_private_key( const bls_private_key& ) = default;
         bls_private_key& operator=( const bls_private_key& ) = default;
         explicit bls_private_key( std::vector<uint8_t> seed ) {
            _sk = bls12_381::secret_key(seed);
         }

         explicit bls_private_key(const string& base58str);
         std::string to_string(const yield_function_t& yield = yield_function_t()) const;

         bls_public_key     get_public_key() const;
         bls_signature      sign( const vector<uint8_t>& message ) const;

         static bls_private_key generate();

         static bls_private_key regenerate( vector<uint8_t> seed ) {
            return bls_private_key(std::move(seed));
         }

      private:
         //std::vector<uint8_t> _seed;
         std::array<uint64_t, 4> _sk;

         friend struct reflector<bls_private_key>;
   }; // bls_private_key

} // fc::crypto::blslib

namespace fc {
   void to_variant(const crypto::blslib::bls_private_key& var, variant& vo, const yield_function_t& yield = yield_function_t());

   void from_variant(const variant& var, crypto::blslib::bls_private_key& vo);
} // namespace fc

FC_REFLECT(crypto::blslib::bls_private_key, (_sk) )

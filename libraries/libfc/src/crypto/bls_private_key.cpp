#include <fc/crypto/bls_private_key.hpp>
#include <fc/crypto/rand.hpp>
#include <fc/utility.hpp>
#include <fc/exception/exception.hpp>

namespace fc::crypto::blslib {

   bls_public_key bls_private_key::get_public_key() const
   {
      auto sk = bls12_381::secret_key(_seed);
      bls12_381::g1 pk = bls12_381::public_key(sk);
      return bls_public_key(pk);
   }

   bls_signature bls_private_key::sign( const vector<uint8_t>& message ) const
   {
      std::array<uint64_t, 4> sk = bls12_381::secret_key(_seed);
      bls12_381::g2 sig = bls12_381::sign(sk, message);
      return bls_signature(sig);
   }

   bls_private_key bls_private_key::generate() {
      std::vector<uint8_t> v(32);
      rand_bytes(reinterpret_cast<char*>(&v[0]), 32);
      return bls_private_key(v);
   }

   static vector<uint8_t> priv_parse_base58(const string& base58str)
   {
      std::vector<char> v1 = fc::from_base58(base58str);

      FC_ASSERT(v1.size() == 32);

      std::vector<uint8_t> v2(32);

      std::copy(v1.begin(), v1.end(), v2.begin());

      return v2;
   }

   bls_private_key::bls_private_key(const std::string& base58str)
   :_seed(priv_parse_base58(base58str))
   {}

   std::string bls_private_key::to_string(const fc::yield_function_t& yield) const
   {

      std::vector<char> v2(32);
      std::copy(_seed.begin(), _seed.end(), v2.begin());

      std::string data_str = fc::to_base58(v2, yield);

      return data_str;

   }

} // fc::crypto::blslib

namespace fc
{
   void to_variant(const fc::crypto::blslib::bls_private_key& var, variant& vo, const fc::yield_function_t& yield)
   {
      vo = var.to_string(yield);
   }

   void from_variant(const variant& var, fc::crypto::blslib::bls_private_key& vo)
   {
      vo = fc::crypto::blslib::bls_private_key(var.as_string());
   }

} // fc

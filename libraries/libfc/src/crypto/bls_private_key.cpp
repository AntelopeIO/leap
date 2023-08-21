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

   string to_wif(vector<uint8_t> secret, const fc::yield_function_t& yield )
   {
      FC_ASSERT(secret.size() == 32);

      std::array<uint8_t, 32> v2;

      std::copy(secret.begin(), secret.end(), v2.begin());

      const size_t size_of_data_to_hash = 32 + 1;
      const size_t size_of_hash_bytes = 4;
      char data[size_of_data_to_hash + size_of_hash_bytes];
      data[0] = (char)0x80; // this is the Bitcoin MainNet code
      memcpy(&data[1], (const char*)&v2, 32);
      sha256 digest = sha256::hash(data, size_of_data_to_hash);
      digest = sha256::hash(digest);
      memcpy(data + size_of_data_to_hash, (char*)&digest, size_of_hash_bytes);

      return to_base58(data, sizeof(data), yield);
   }

   std::vector<uint8_t> from_wif( const string& wif_key )
   {
      auto wif_bytes = from_base58(wif_key);
      FC_ASSERT(wif_bytes.size() >= 5);

      auto key_bytes = vector<char>(wif_bytes.begin() + 1, wif_bytes.end() - 4);
      fc::sha256 check = fc::sha256::hash(wif_bytes.data(), wif_bytes.size() - 4);
      fc::sha256 check2 = fc::sha256::hash(check);

      FC_ASSERT(memcmp( (char*)&check, wif_bytes.data() + wif_bytes.size() - 4, 4 ) == 0 ||
                memcmp( (char*)&check2, wif_bytes.data() + wif_bytes.size() - 4, 4 ) == 0 );

      std::vector<uint8_t> v2(32);
      std::copy(key_bytes.begin(), key_bytes.end(), v2.begin());

      return v2;
   }

   static vector<uint8_t> priv_parse_base58(const string& base58str)
   {  
      //cout << base58str << "\n";
      
      return from_wif(base58str);
   }

   bls_private_key::bls_private_key(const std::string& base58str)
   :_seed(priv_parse_base58(base58str))
   {}

   std::string bls_private_key::to_string(const fc::yield_function_t& yield) const
   {

      FC_ASSERT(_seed.size() == 32);
      string wif = to_wif(_seed, yield);

      //cout << wif << "\n";

      return wif;
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

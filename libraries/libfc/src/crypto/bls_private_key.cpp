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

   template<typename Data>
   Data from_wif( const string& wif_key )
   {
   /*   auto wif_bytes = from_base58(wif_key);
      FC_ASSERT(wif_bytes.size() >= 5);
      auto key_bytes = vector<char>(wif_bytes.begin() + 1, wif_bytes.end() - 4);
      fc::sha256 check = fc::sha256::hash(wif_bytes.data(), wif_bytes.size() - 4);
      fc::sha256 check2 = fc::sha256::hash(check);

      FC_ASSERT(memcmp( (char*)&check, wif_bytes.data() + wif_bytes.size() - 4, 4 ) == 0 ||
                memcmp( (char*)&check2, wif_bytes.data() + wif_bytes.size() - 4, 4 ) == 0 );

      return Data(fc::variant(key_bytes).as<typename Data::data_type>());*/
   }

   static vector<uint8_t> priv_parse_base58(const string& base58str)
   {
      const auto pivot = base58str.find('_');
/*
      if (pivot == std::string::npos) {
         // wif import
         using default_type = std::variant_alternative_t<0, bls_private_key::storage_type>;
         return bls_private_key::storage_type(from_wif<default_type>(base58str));
      } else {
         constexpr auto prefix = config::private_key_base_prefix;
         const auto prefix_str = base58str.substr(0, pivot);
         FC_ASSERT(prefix == prefix_str, "Private Key has invalid prefix: ${str}", ("str", base58str)("prefix_str", prefix_str));

         auto data_str = base58str.substr(pivot + 1);
         FC_ASSERT(!data_str.empty(), "Private Key has no data: ${str}", ("str", base58str));
         return base58_str_parser<bls_private_key::storage_type, config::private_key_prefix>::apply(data_str);
      }*/
   }

   bls_private_key::bls_private_key(const std::string& base58str)
   :_seed(priv_parse_base58(base58str))
   {}

   std::string bls_private_key::to_string(const fc::yield_function_t& yield) const
   {

      /*PrivateKey pk = AugSchemeMPL().KeyGen(_seed);

      vector<uint8_t> pkBytes   pk.Serialize()

      auto data_str = Util::HexStr(pkBytes);
      return std::string(config::private_key_base_prefix) + "_" + data_str;*/
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

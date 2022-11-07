#include <fc/crypto/bls_private_key.hpp>
#include <fc/utility.hpp>
#include <fc/exception/exception.hpp>

namespace fc { namespace crypto { namespace blslib {

   using namespace std;

   bls_public_key bls_private_key::get_public_key() const
   {  
      G1Element pk = AugSchemeMPL().KeyGen(_seed).GetG1Element();

      return bls_public_key(pk.Serialize());
   }

   bls_signature bls_private_key::sign( vector<uint8_t> message ) const
   {  
  
   PrivateKey sk = AugSchemeMPL().KeyGen(_seed);

      G2Element s = PopSchemeMPL().Sign(sk, message);
      return bls_signature(s.Serialize());
   }

   /*struct public_key_visitor : visitor<bls_public_key::storage_type> {
      template<typename KeyType>
      bls_public_key::storage_type operator()(const KeyType& key) const
      {
        //return bls_public_key::storage_type(key.get_public_key());
      }
   };

   struct sign_visitor : visitor<bls_signature::storage_type> {
      sign_visitor( const sha256& digest, bool require_canonical )
      :_digest(digest)
      ,_require_canonical(require_canonical)
      {}

      template<typename KeyType>
      bls_signature::storage_type operator()(const KeyType& key) const
      {
         return bls_signature::storage_type(key.sign(_digest, _require_canonical));
      }

      const sha256&  _digest;
      bool           _require_canonical;
   };

   bls_signature bls_private_key::sign( vector<uint8_t> message ) const
   {
      //return bls_signature(std::visit(sign_visitor(digest, require_canonical), _seed));
   }

   struct generate_shared_secret_visitor : visitor<sha512> {
      generate_shared_secret_visitor( const bls_public_key::storage_type& pub_storage )
      :_pub_storage(pub_storage)
      {}

      template<typename KeyType>
      sha512 operator()(const KeyType& key) const
      {
         using PublicKeyType = typename KeyType::public_key_type;
         return key.generate_shared_secret(std::template get<PublicKeyType>(_pub_storage));
      }

      const bls_public_key::storage_type&  _pub_storage;
   };

   sha512 bls_private_key::generate_shared_secret( const bls_public_key& pub ) const

   template<typename Data>
   string to_wif( const Data& secret, const fc::yield_function_t& yield )
   {
   {
      return std::visit(generate_shared_secret_visitor(pub._storage), _seed);
   }*/
  /*    const size_t size_of_data_to_hash = sizeof(typename Data::data_type) + 1;
      const size_t size_of_hash_bytes = 4;
      char data[size_of_data_to_hash + size_of_hash_bytes];
      data[0] = (char)0x80; // this is the Bitcoin MainNet code
      memcpy(&data[1], (const char*)&secret.serialize(), sizeof(typename Data::data_type));
      sha256 digest = sha256::hash(data, size_of_data_to_hash);
      digest = sha256::hash(digest);
      memcpy(data + size_of_data_to_hash, (char*)&digest, size_of_hash_bytes);
      return to_base58(data, sizeof(data), yield);
   }
*/

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
   /*   const auto pivot = base58str.find('_');

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

/*   std::string bls_private_key::serialize(){

      PrivateKey sk = AugSchemeMPL().KeyGen(_seed);

      return Util::HexStr(sk.Serialize());
   }*/

   std::ostream& operator<<(std::ostream& s, const bls_private_key& k) {
      s << "bls_private_key(" << k.to_string() << ')';
      return s;
   }
/*
   bool operator == ( const bls_private_key& p1, const bls_private_key& p2) {

      return eq_comparator<vector<char>>::apply(p1._seed, p2._seed);
   }

   bool operator < ( const bls_private_key& p1, const bls_private_key& p2){


      return less_comparator<vector<char>>::apply(p1._seed, p2._seed);
   }*/
} } }  // fc::crypto::blslib

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

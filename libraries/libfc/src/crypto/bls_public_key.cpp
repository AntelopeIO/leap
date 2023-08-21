#include <fc/crypto/bls_public_key.hpp>
#include <fc/crypto/common.hpp>
#include <fc/exception/exception.hpp>

namespace fc { namespace crypto { namespace blslib {

   static bls12_381::g1 parse_base58(const std::string& base58str)
   {  
      
      std::vector<char> v1 = fc::from_base58(base58str);

      FC_ASSERT(v1.size() == 48);
      std::array<uint8_t, 48> v2;
      std::copy(v1.begin(), v1.end(), v2.begin());
      std::optional<bls12_381::g1> g1 = bls12_381::g1::fromCompressedBytesBE(v2);
      FC_ASSERT(g1);
      return *g1;
   }

   bls_public_key::bls_public_key(const std::string& base58str)
   :_pkey(parse_base58(base58str))
   {}

   std::string bls_public_key::to_string(const fc::yield_function_t& yield)const {

      std::vector<char> v2;
      std::array<uint8_t, 48> bytes = _pkey.toCompressedBytesBE();
      std::copy(bytes.begin(), bytes.end(), std::back_inserter(v2));

      std::string data_str = fc::to_base58(v2, yield);
      
      //return std::string(config::bls_public_key_base_prefix) + "_" + data_str;
      return data_str;


   }

   std::ostream& operator<<(std::ostream& s, const bls_public_key& k) {
      s << "bls_public_key(" << k.to_string() << ')';
      return s;
   }

} } }  // fc::crypto::blslib

namespace fc
{
   using namespace std;
   void to_variant(const fc::crypto::blslib::bls_public_key& var, fc::variant& vo, const fc::yield_function_t& yield)
   {
      vo = var.to_string(yield);
   }

   void from_variant(const fc::variant& var, fc::crypto::blslib::bls_public_key& vo)
   {
      vo = fc::crypto::blslib::bls_public_key(var.as_string());
   }
} // fc

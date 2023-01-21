#include <fc/crypto/bls_public_key.hpp>
#include <fc/crypto/common.hpp>
#include <fc/exception/exception.hpp>
#include <bls.hpp>

namespace fc { namespace crypto { namespace blslib {

   static std::vector<uint8_t> parse_base58(const std::string& base58str)
   {  

      std::vector<char> v1 = fc::from_base58(base58str);

      std::vector<uint8_t> v2;
      std::copy(v1.begin(), v1.end(), std::back_inserter(v2));

      return v2;

   }

   bls_public_key::bls_public_key(const std::string& base58str)
   :_pkey(parse_base58(base58str))
   {}


   std::string bls_public_key::to_string(const fc::yield_function_t& yield)const {

      std::vector<char> v2;
      std::copy(_pkey.begin(), _pkey.end(), std::back_inserter(v2));

      std::string data_str = fc::to_base58(v2, yield);

      //std::string data_str = Util::HexStr(_pkey);

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
#include <fc/crypto/bls_public_key.hpp>
#include <fc/crypto/common.hpp>
#include <fc/exception/exception.hpp>

namespace fc::crypto::blslib {

   static bls12_381::g1 parse_base58(const std::string& base58str)
   {  
      
      const auto pivot = base58str.find('_');
      FC_ASSERT(pivot != std::string::npos, "No delimiter in string, cannot determine data type: ${str}", ("str", base58str));

      const auto base_prefix_str = base58str.substr(0, 3);
      FC_ASSERT(config::bls_public_key_base_prefix == base_prefix_str, "BLS Public Key has invalid base prefix: ${str}", ("str", base58str)("base_prefix_str", base_prefix_str));
      
      const auto prefix_str = base58str.substr(pivot + 1, 3);
      FC_ASSERT(config::bls_public_key_prefix == prefix_str, "BLS Public Key has invalid prefix: ${str}", ("str", base58str)("prefix_str", prefix_str));

      auto data_str = base58str.substr(8);

      std::vector<char> bytes = from_base58(data_str);

      FC_ASSERT(bytes.size() == 48);
      
      std::array<uint8_t, 48> v2;
      std::copy(bytes.begin(), bytes.end(), v2.begin());
      std::optional<bls12_381::g1> g1 = bls12_381::g1::fromCompressedBytesBE(v2);
      FC_ASSERT(g1);
      return *g1;
   }

   bls_public_key::bls_public_key(const std::string& base58str)
   :_pkey(parse_base58(base58str))
   {}

   std::string bls_public_key::to_string(const yield_function_t& yield)const {

      std::vector<char> v2;
      std::array<uint8_t, 48> bytes = _pkey.toCompressedBytesBE();
      std::copy(bytes.begin(), bytes.end(), std::back_inserter(v2));

      std::string data_str = to_base58(v2, yield);
      
      return std::string(config::bls_public_key_base_prefix) + "_" + std::string(config::bls_public_key_prefix) + "_" + data_str;

   }

   std::ostream& operator<<(std::ostream& s, const bls_public_key& k) {
      s << "bls_public_key(" << k.to_string() << ')';
      return s;
   }

} // fc::crypto::blslib

namespace fc
{
   using namespace std;
   void to_variant(const crypto::blslib::bls_public_key& var, variant& vo, const yield_function_t& yield)
   {
      vo = var.to_string(yield);
   }

   void from_variant(const variant& var, crypto::blslib::bls_public_key& vo)
   {
      vo = crypto::blslib::bls_public_key(var.as_string());
   }
} // fc

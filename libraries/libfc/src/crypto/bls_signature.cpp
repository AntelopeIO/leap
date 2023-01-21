#include <fc/crypto/bls_signature.hpp>
#include <fc/crypto/common.hpp>
#include <fc/exception/exception.hpp>

namespace fc { namespace crypto { namespace blslib {

   static vector<uint8_t> sig_parse_base58(const std::string& base58str)
   { try {

      std::vector<char> v1 = fc::from_base58(base58str);

      std::vector<uint8_t> v2;
      std::copy(v1.begin(), v1.end(), std::back_inserter(v2));

      return v2;

   } FC_RETHROW_EXCEPTIONS( warn, "error parsing bls_signature", ("str", base58str ) ) }

   bls_signature::bls_signature(const std::string& base58str)
     :_sig(sig_parse_base58(base58str))
   {}

   size_t bls_signature::which() const {
      //return _storage.index();
   }

   std::string bls_signature::to_string(const fc::yield_function_t& yield) const
   {

      std::vector<char> v2;
      std::copy(_sig.begin(), _sig.end(), std::back_inserter(v2));

      std::string data_str = fc::to_base58(v2, yield);

      return data_str;

   }

   std::ostream& operator<<(std::ostream& s, const bls_signature& k) {
      s << "bls_signature(" << k.to_string() << ')';
      return s;
   }

} } }  // fc::crypto::blslib

namespace fc
{
   void to_variant(const fc::crypto::blslib::bls_signature& var, fc::variant& vo, const fc::yield_function_t& yield)
   {
      vo = var.to_string(yield);
   }

   void from_variant(const fc::variant& var, fc::crypto::blslib::bls_signature& vo)
   {
      vo = fc::crypto::blslib::bls_signature(var.as_string());
   }
} // fc
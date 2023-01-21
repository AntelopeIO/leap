#include <fc/crypto/bls_private_key.hpp>
#include <fc/utility.hpp>
#include <fc/exception/exception.hpp>
#include <bls.hpp>

namespace fc { namespace crypto { namespace blslib {

   //using namespace std;

   bls_public_key bls_private_key::get_public_key() const
   {  
      bls::G1Element pk = bls::AugSchemeMPL().KeyGen(_seed).GetG1Element();

      return bls_public_key(pk.Serialize());
   }

   bls_signature bls_private_key::sign( vector<uint8_t> message ) const
   {  

   bls::PrivateKey sk = bls::AugSchemeMPL().KeyGen(_seed);

      bls::G2Element s = bls::PopSchemeMPL().Sign(sk, message);
      return bls_signature(s.Serialize());
   }

   bls_private_key::bls_private_key(const std::string& base58str){}

   std::string bls_private_key::to_string(const fc::yield_function_t& yield) const
   {

      bls::PrivateKey pk = bls::AugSchemeMPL().KeyGen(_seed);
      vector<uint8_t> pkBytes = pk.Serialize();
      auto data_str = bls::Util::HexStr(pkBytes);
      return data_str;/**/
   }

   std::ostream& operator<<(std::ostream& s, const bls_private_key& k) {
      s << "bls_private_key(" << k.to_string() << ')';
      return s;
   }

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
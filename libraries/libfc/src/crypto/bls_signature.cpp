#include <fc/crypto/bls_signature.hpp>
#include <fc/crypto/common.hpp>
#include <fc/exception/exception.hpp>

namespace fc { namespace crypto { namespace blslib {

   struct hash_visitor : public fc::visitor<size_t> {
/*      template<typename SigType>
      size_t operator()(const SigType& sig) const {
         static_assert(sizeof(sig._data.data) == 65, "sig size is expected to be 65");
         //signatures are two bignums: r & s. Just add up least significant digits of the two
         return *(size_t*)&sig._data.data[32-sizeof(size_t)] + *(size_t*)&sig._data.data[64-sizeof(size_t)];
      }

      size_t operator()(const webauthn::bls_signature& sig) const {
         return sig.get_hash();
      }*/
   };

   static vector<uint8_t> sig_parse_base58(const std::string& base58str)
   { try {


      const auto pivot = base58str.find('_');
      auto base_str = base58str.substr(pivot + 1);
      const auto pivot2 = base_str.find('_');
      auto data_str = base_str.substr(pivot2 + 1);

      std::vector<char> v1 = fc::from_base58(data_str);

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


   //template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
   //template<class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

   size_t bls_signature::variable_size() const {
     /* return std::visit(overloaded {
         [&](const auto& k1r1) {
            return static_cast<size_t>(0);
         },
         [&](const webauthn::bls_signature& wa) {
            return static_cast<size_t>(wa.variable_size());
         }
      }, _storage);*/
   }

   std::string bls_signature::to_string(const fc::yield_function_t& yield) const
   {

      std::vector<char> v2;
      std::copy(_sig.begin(), _sig.end(), std::back_inserter(v2));

      std::string data_str = fc::to_base58(v2, yield);

      return std::string(config::bls_signature_base_prefix) + "_" + std::string(config::bls_signature_prefix) + "_" + data_str;

   }

   std::ostream& operator<<(std::ostream& s, const bls_signature& k) {
      s << "bls_signature(" << k.to_string() << ')';
      return s;
   }
/*
   bool operator == ( const bls_signature& p1, const bls_signature& p2) {
      return eq_comparator<bls_signature::storage_type>::apply(p1._storage, p2._storage);
   }

   bool operator != ( const bls_signature& p1, const bls_signature& p2) {
      return !eq_comparator<bls_signature::storage_type>::apply(p1._storage, p2._storage);
   }

   bool operator < ( const bls_signature& p1, const bls_signature& p2)
   {
      return less_comparator<bls_signature::storage_type>::apply(p1._storage, p2._storage);
   }
*/
   size_t hash_value(const bls_signature& b) {
     //  return std::visit(hash_visitor(), b._storage);
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

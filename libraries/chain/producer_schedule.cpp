#include <eosio/chain/producer_schedule.hpp>

namespace eosio::chain {

fc::variant producer_authority::get_abi_variant() const {
      auto authority_variant = std::visit([](const auto& a){
         fc::variant value;
         fc::to_variant(a, value);

         fc::variant type = std::string(std::decay_t<decltype(a)>::abi_type_name());

         return fc::variants {
               std::move(type),
               std::move(value)
         };
      }, authority);

      return fc::mutable_variant_object()
            ("producer_name", producer_name)
            ("authority", std::move(authority_variant));
}

shared_producer_authority::shared_producer_authority(const producer_authority& pa) :
   producer_name(pa.producer_name),
   authority(std::visit([]<class T>(const T& a) { return T(a);}, pa.authority))
{
}

shared_producer_authority& shared_producer_authority::operator=(const producer_authority& pa) {
   producer_name = pa.producer_name;
   authority = std::visit([]<class T>(const T& a) { return T(a);}, pa.authority);
   return *this;
}

shared_block_signing_authority_v0::shared_block_signing_authority_v0(const block_signing_authority_v0& bsa) :
   threshold(bsa.threshold),
   keys(bsa.keys)
{
}

shared_block_signing_authority_v0& shared_block_signing_authority_v0::operator= (const block_signing_authority_v0 & bsa) {
   threshold = bsa.threshold;
   keys = bsa.keys;
   return *this;
}

shared_producer_authority_schedule::shared_producer_authority_schedule(const producer_authority_schedule& pas) :
   version(pas.version),
   producers(pas.producers)
{
}
   
shared_producer_authority_schedule& shared_producer_authority_schedule::operator=(const producer_authority_schedule& pas) {
   version = pas.version;
   producers = pas.producers;
   return *this;
}

} /// eosio::chain

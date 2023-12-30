#include <eosio/chain/hotstuff/instant_finality_extension.hpp>
#include <eosio/chain/exceptions.hpp>

namespace eosio::chain {

   void instant_finality_extension::reflector_init() {
      static_assert( fc::raw::has_feature_reflector_init_on_unpacked_reflected_types,
                     "instant_finality_extension expects FC to support reflector_init" );
      static_assert( extension_id() == 2, "instant_finality_extension extension id must be 2" );
      static_assert( enforce_unique(), "instant_finality_extension must enforce uniqueness");
   }

}  // eosio::chain

#pragma once

#include <eosio/chain/types.hpp>
#include <eosio/chain/hotstuff/finalizer_authority.hpp>

namespace eosio::chain {

   struct finalizer_policy {
      uint32_t                         generation = 0; ///< sequentially incrementing version number
      uint64_t                         threshold = 0;  ///< vote weight threshold to finalize blocks
      std::vector<finalizer_authority> finalizers; ///< Instant Finality voter set
   };

   using finalizer_policy_ptr = std::shared_ptr<finalizer_policy>;

} /// eosio::chain

FC_REFLECT( eosio::chain::finalizer_policy, (generation)(threshold)(finalizers) )

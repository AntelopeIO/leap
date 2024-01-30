#pragma once

#include <eosio/chain/types.hpp>
#include <eosio/chain/hotstuff/finalizer_authority.hpp>

namespace eosio::chain {

   struct finalizer_policy {
      uint32_t                         generation = 0; ///< sequentially incrementing version number
      uint64_t                         threshold = 0;  ///< vote weight threshold to finalize blocks
      std::vector<finalizer_authority> finalizers; ///< Instant Finality voter set

      // max accumulated weak weight before becoming weak_final
      uint64_t max_weak_sum_before_weak_final() const {
         uint64_t sum = std::accumulate( finalizers.begin(), finalizers.end(), 0,
            [](uint64_t acc, const finalizer_authority& f) {
               return acc + f.weight;
            }
         );

         return (sum - threshold);
      }
   };

   using finalizer_policy_ptr = std::shared_ptr<finalizer_policy>;

} /// eosio::chain

FC_REFLECT( eosio::chain::finalizer_policy, (generation)(threshold)(finalizers) )

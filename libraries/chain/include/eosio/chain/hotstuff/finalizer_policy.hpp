#pragma once

#include <eosio/chain/types.hpp>

namespace eosio::chain {

   struct finalizer_authority;

   struct finalizer_policy {
      finalizer_policy();
      ~finalizer_policy();

      finalizer_policy(const finalizer_policy&);
      finalizer_policy(finalizer_policy&&) noexcept;

      finalizer_policy& operator=(const finalizer_policy&);
      finalizer_policy& operator=(finalizer_policy&&) noexcept;

      uint32_t                         generation = 0; ///< sequentially incrementing version number
      uint64_t                         threshold = 0;  ///< vote weight threshold to finalize blocks
      std::vector<finalizer_authority> finalizers; ///< Instant Finality voter set
   };

   using finalizer_policy_ptr = std::shared_ptr<finalizer_policy>;

   /**
    * Block Header Extension Compatibility
    */
   struct finalizer_policy_extension : finalizer_policy {
      static constexpr uint16_t extension_id() { return 2; } // TODO 3 instead?
      static constexpr bool     enforce_unique() { return true; }
   };

} /// eosio::chain

FC_REFLECT( eosio::chain::finalizer_policy, (generation)(threshold)(finalizers) )
FC_REFLECT_DERIVED( eosio::chain::finalizer_policy_extension, (eosio::chain::finalizer_policy), )
#pragma once

#include <eosio/chain/types.hpp>

namespace eosio::chain {

   struct finalizer_authority;

   struct finalizer_set {
      finalizer_set();
      ~finalizer_set();

      finalizer_set(const finalizer_set&);
      finalizer_set(finalizer_set&&) noexcept;

      finalizer_set& operator=(const finalizer_set&);
      finalizer_set& operator=(finalizer_set&&) noexcept;

      uint32_t                         generation = 0; ///< sequentially incrementing version number
      uint64_t                         fthreshold = 0;  ///< vote fweight threshold to finalize blocks
      std::vector<finalizer_authority> finalizers; ///< Instant Finality voter set
   };

   using finalizer_set_ptr = std::shared_ptr<finalizer_set>;

   /**
    * Block Header Extension Compatibility
    */
   struct hs_finalizer_set_extension : finalizer_set {
      static constexpr uint16_t extension_id() { return 2; } // TODO 3 instead?
      static constexpr bool     enforce_unique() { return true; }
   };

} /// eosio::chain

FC_REFLECT( eosio::chain::finalizer_set, (generation)(fthreshold)(finalizers) )
FC_REFLECT_DERIVED( eosio::chain::hs_finalizer_set_extension, (eosio::chain::finalizer_set), )
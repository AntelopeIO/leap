#pragma once

#include <eosio/chain/types.hpp>

namespace eosio::chain {

   struct proposal_info_t {
      uint32_t last_qc_block_height {0};  ///< The block height of the most recent ancestor block that has a QC justification
      bool     is_last_qc_strong {false}; ///< whether the QC for the block referenced by last_qc_block_height is strong or weak.
   };

   using proposal_info_ptr = std::shared_ptr<proposal_info_t>;

   /**
    * Block Header Extension Compatibility
    */
   struct proposal_info_extension : proposal_info_t {
      static constexpr uint16_t extension_id()   { return 3; } 
      static constexpr bool     enforce_unique() { return true; }
   };

} /// eosio::chain

FC_REFLECT( eosio::chain::proposal_info_t, (last_qc_block_height)(is_last_qc_strong) )
FC_REFLECT_DERIVED( eosio::chain::proposal_info_extension, (eosio::chain::proposal_info_t), )
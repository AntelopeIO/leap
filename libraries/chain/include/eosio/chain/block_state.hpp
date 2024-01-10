#pragma once

#include <eosio/chain/block_header_state.hpp>
#include <eosio/chain/block.hpp>
#include <eosio/chain/transaction_metadata.hpp>
#include <eosio/chain/action_receipt.hpp>

namespace eosio::chain {

   struct block_state : public block_header_state {  // block_header_state provides parent link
      // ------ data members -------------------------------------------------------------
      signed_block_ptr           block;
      bool                       validated;             // We have executed the block's trxs and verified that action merkle root (block id) matches.
      digest_type                finalizer_digest;
      pending_quorum_certificate pending_qc;            // where we accumulate votes we receive
      std::optional<valid_quorum_certificate> valid_qc; // qc received from the network

   
      // ------ data members caching information available elsewhere ----------------------
      block_id_type              cached_id;   // cache of block_header_state::header.calculate_id() (indexed on this field)
      header_extension_multimap  header_exts; // redundant with the data stored in header

      // ------ functions -----------------------------------------------------------------
      const block_id_type&   id()                const { return cached_id; }
      const block_id_type&   previous()          const { return block_header_state::previous(); }
      uint32_t               block_num()         const { return block_header_state::block_num(); }
      block_timestamp_type   timestamp()         const { return block_header_state::timestamp(); }
      const extensions_type& header_extensions() const { return block_header_state::header.header_extensions; }
      bool                   is_valid()          const { return validated; } 
      void                   set_valid(bool b)         { validated = b; }
      uint32_t               irreversible_blocknum() const { return 0; } // [greg todo] equivalent of dpos_irreversible_blocknum
      
      protocol_feature_activation_set_ptr get_activated_protocol_features() const { return block_header_state::activated_protocol_features; }
      deque<transaction_metadata_ptr>     extract_trxs_metas() { return {}; }; //  [greg todo] see impl in block_state_legacy.hpp

      bool aggregate_vote(const hs_vote_message& vote); // aggregate vote into pending_qc
   };

using block_state_ptr = std::shared_ptr<block_state>;
   
} // namespace eosio::chain

// [greg todo] which members need to be serialized to disk when saving fork_db
FC_REFLECT_DERIVED( eosio::chain::block_state, (eosio::chain::block_header_state), (block)(validated) )

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
   block_id_type              id;          // cache of block_header_state::header.calculate_id() (indexed on this field)
   header_extension_multimap  header_exts; // redundant with the data stored in header

   // ------ functions -----------------------------------------------------------------
   deque<transaction_metadata_ptr>      extract_trxs_metas() { return {}; }; //  [greg todo] see impl in block_state_legacy.hpp
   
   const block_id_type&  previous()  const { return block_header_state::previous(); }
   uint32_t              block_num() const { return block_header_state::block_num(); }
      block_header_state*   get_bhs()         { return static_cast<block_header_state*>(this); }
   block_timestamp_type  timestamp() const { return block_header_state::timestamp(); }
   extensions_type       header_extensions() { return block_header_state::_header.header_extensions; }
   protocol_feature_activation_set_ptr get_activated_protocol_features() { return block_header_state::_activated_protocol_features; }
    // [greg todo] equivalent of block_state_legacy_common::dpos_irreversible_blocknum - ref in fork_database.cpp
   uint32_t              irreversible_blocknum() const { return 0; }

   // [greg todo] equivalent of block_state_legacy::validated - ref in fork_database.cpp
   bool                  is_valid() const { return validated; } 
   
};

using block_state_ptr = std::shared_ptr<block_state>;
   
} // namespace eosio::chain

// [greg todo] which members need to be serialized to disk when saving fork_db
FC_REFLECT_DERIVED( eosio::chain::block_state, (eosio::chain::block_header_state), (block)(validated) )

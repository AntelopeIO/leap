#pragma once
#include <eosio/chain/block_header.hpp>
#include <eosio/chain/incremental_merkle.hpp>
#include <eosio/chain/protocol_feature_manager.hpp>
#include <eosio/chain/hotstuff/hotstuff.hpp>
#include <eosio/chain/hotstuff/finalizer_policy.hpp>
#include <eosio/chain/chain_snapshot.hpp>
#include <future>

namespace eosio::chain {

namespace detail { struct schedule_info; };

// totem for dpos_irreversible_blocknum after hotstuff is activated
// This value implicitly means that fork_database will prefer hotstuff blocks over dpos blocks

struct proposer_policy {
   constexpr static uint32_t   current_schema_version = 1;
   const uint8_t               schema_version = current_schema_version;
   
   // TODO: seems useful for light clients, not necessary for nodeos
   block_timestamp_type        active_time; // block when schedule will become active
   producer_authority_schedule proposer_schedule;
};
using proposer_policy_ptr = std::shared_ptr<proposer_policy>;

struct building_block_input {
   block_id_type                     parent_id;
   block_timestamp_type              timestamp;
   account_name                      producer;
   vector<digest_type>               new_protocol_feature_activations;
};
      
// this struct can be extracted from a building block
struct block_header_state_input : public building_block_input {
   digest_type                       transaction_mroot;    // Comes from std::get<checksum256_type>(building_block::trx_mroot_or_receipt_digests)
   digest_type                       action_mroot;         // Compute root from  building_block::action_receipt_digests
   std::optional<proposer_policy>    new_proposer_policy;  // Comes from building_block::new_proposer_policy
   std::optional<finalizer_policy>   new_finalizer_policy; // Comes from building_block::new_finalizer_policy
   std::optional<quorum_certificate> qc;                   // Comes from traversing branch from parent and calling get_best_qc()
                                                           // assert(qc->block_num <= num_from_id(previous));
   // ... ?
};

struct block_header_state_core {
   uint32_t                last_final_block_height = 0;     // last irreversible (final) block.
   std::optional<uint32_t> final_on_strong_qc_block_height; // will become final if this header achives a strong QC.
   std::optional<uint32_t> last_qc_block_height;            //
   uint32_t                finalizer_policy_generation;     // 

   block_header_state_core next(uint32_t last_qc_block_height, bool is_last_qc_strong) const;
};

struct block_header_state {
   // ------ data members ------------------------------------------------------------
   block_id_type                       id;
   block_header                        header;
   protocol_feature_activation_set_ptr activated_protocol_features;

   block_header_state_core             core;
   incremental_merkle_tree             proposal_mtree;
   incremental_merkle_tree             finality_mtree;

   finalizer_policy_ptr                finalizer_policy; // finalizer set + threshold + generation, supports `digest()`
   proposer_policy_ptr                 proposer_policy;  // producer authority schedule, supports `digest()`

   flat_map<uint32_t, proposer_policy_ptr>  proposer_policies;
   flat_map<uint32_t, finalizer_policy_ptr> finalizer_policies;

   // ------ functions -----------------------------------------------------------------
   digest_type           compute_finalizer_digest() const;
   block_timestamp_type  timestamp() const { return header.timestamp; }
   account_name          producer() const  { return header.producer; }
   const block_id_type&  previous() const  { return header.previous; }
   uint32_t              block_num() const { return block_header::num_from_id(previous()) + 1; }
   
   block_header_state next(const block_header_state_input& data) const;
   
   // block descending from this need the provided qc in the block extension
   bool is_needed(const quorum_certificate& qc) const {
      return !core.last_qc_block_height || qc.block_height > *core.last_qc_block_height;
   }

   protocol_feature_activation_set_ptr  get_prev_activated_protocol_features() const { return {}; } //  [greg todo] 
   flat_set<digest_type> get_activated_protocol_features() const { return activated_protocol_features->protocol_features; }
   detail::schedule_info prev_pending_schedule() const;
   uint32_t active_schedule_version() const;
   std::optional<producer_authority_schedule>& new_pending_producer_schedule() { static std::optional<producer_authority_schedule> x; return x; } //  [greg todo] 
   signed_block_header make_block_header(const checksum256_type& transaction_mroot,
                                         const checksum256_type& action_mroot,
                                         const std::optional<producer_authority_schedule>& new_producers,
                                         vector<digest_type>&& new_protocol_feature_activations,
                                         const protocol_feature_set& pfs) const;
   uint32_t increment_finalizer_policy_generation() { return ++core.finalizer_policy_generation; }
};

using block_header_state_ptr = std::shared_ptr<block_header_state>;

}

// [greg todo] which members need to be serialized to disk when saving fork_db
// obviously many are missing below.
FC_REFLECT( eosio::chain::block_header_state,  (id))

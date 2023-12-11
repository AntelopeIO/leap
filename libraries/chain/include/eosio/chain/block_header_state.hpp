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
   block_id_type                     previous;
   block_timestamp_type              timestamp;
   account_name                      producer;
   vector<digest_type>               new_protocol_feature_activations;
};
      
// this struct can be extracted from a building block
struct assembled_block_input : public building_block_input {
   digest_type                       _transaction_mroot;
   digest_type                       _action_mroot;
   std::optional<proposer_policy>    _new_proposer_policy;
   std::optional<finalizer_policy>   _new_finalizer_policy; // set by set_finalizer host function??
   std::optional<quorum_certificate> _qc;                   // assert(qc.block_height <= num_from_id(previous));

   std::optional<finalizer_policy> new_finalizer_policy() const { return _new_finalizer_policy; }
};

struct block_header_state_core {
   uint32_t                last_final_block_height = 0;     // last irreversible (final) block.
   std::optional<uint32_t> final_on_strong_qc_block_height; // will become final if this header achives a strong QC.
   std::optional<uint32_t> last_qc_block_height;            //
   uint32_t                finalizer_policy_generation;     // 

   block_header_state_core next(uint32_t last_qc_block_height, bool is_last_qc_strong) const;
};

struct block_header_state {
   block_id_type                       _id;
   block_header                        _header;
   protocol_feature_activation_set_ptr _activated_protocol_features;

   block_header_state_core             _core;
   incremental_merkle_tree             _proposal_mtree;
   incremental_merkle_tree             _finality_mtree;

   finalizer_policy_ptr                _finalizer_policy; // finalizer set + threshold + generation, supports `digest()`
   proposer_policy_ptr                 _proposer_policy;  // producer authority schedule, supports `digest()`

   flat_map<uint32_t, proposer_policy_ptr>  _proposer_policies;
   flat_map<uint32_t, finalizer_policy_ptr> _finalizer_policies;

   digest_type           compute_finalizer_digest() const;
   block_timestamp_type  timestamp() const;
   account_name          producer() const;
   block_id_type         previous() const;
   uint32_t              block_num() const { return block_header::num_from_id(previous()) + 1; }
   
   block_header_state next(const assembled_block_input& data) const;
   
   // block descending from this need the provided qc in the block extension
   bool is_needed(const quorum_certificate& qc) const {
      return !_core.last_qc_block_height || qc.block_height > *_core.last_qc_block_height;
   }

   protocol_feature_activation_set_ptr  get_prev_activated_protocol_features() const;
   flat_set<digest_type> get_activated_protocol_features() const { return _activated_protocol_features->protocol_features; }
   uint32_t pending_irreversible_blocknum() const;
   uint32_t irreversible_blocknum() const;
   detail::schedule_info prev_pending_schedule() const;
   uint32_t active_schedule_version() const;
   std::optional<producer_authority_schedule>& new_pending_producer_schedule();
   signed_block_header make_block_header(const checksum256_type& transaction_mroot,
                                         const checksum256_type& action_mroot,
                                         const std::optional<producer_authority_schedule>& new_producers,
                                         vector<digest_type>&& new_protocol_feature_activations,
                                         const protocol_feature_set& pfs) const;
   uint32_t increment_finalizer_policy_generation() { return ++_core.finalizer_policy_generation; }
};

using block_header_state_ptr = std::shared_ptr<block_header_state>;

}
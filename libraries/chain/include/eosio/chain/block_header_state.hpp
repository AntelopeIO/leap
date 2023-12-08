#pragma once
#include <eosio/chain/block_header.hpp>
#include <eosio/chain/incremental_merkle.hpp>
#include <eosio/chain/protocol_feature_manager.hpp>
#include <eosio/chain/hotstuff/hotstuff.hpp>
#include <eosio/chain/hotstuff/finalizer_policy.hpp>
#include <eosio/chain/chain_snapshot.hpp>
#include <future>

namespace eosio::chain {

// totem for dpos_irreversible_blocknum after hotstuff is activated
// This value implicitly means that fork_database will prefer hotstuff blocks over dpos blocks
constexpr uint32_t hs_dpos_irreversible_blocknum = std::numeric_limits<uint32_t>::max();

struct proposer_policy {}; // temporary placeholder
using proposer_policy_ptr = std::shared_ptr<proposer_policy>;

struct building_block_input {
   block_id_type                     previous;
   block_timestamp_type              timestamp;
   account_name                      producer;
   vector<digest_type>               new_protocol_feature_activations;
};
      
// this struct can be extracted from a building block
struct assembled_block_input : public building_block_input {
   digest_type                       transaction_mroot;
   digest_type                       action_mroot;
   std::optional<proposer_policy>    new_proposer_policy;
   std::optional<finalizer_policy>   new_finalizer_policy;
   std::optional<quorum_certificate> qc;                 // assert(qc.block_height <= num_from_id(previous));
};

struct block_header_state_core {
   uint32_t                last_final_block_height = 0;     // last irreversible (final) block.
   std::optional<uint32_t> final_on_strong_qc_block_height; // will become final if this header achives a strong QC.
   std::optional<uint32_t> last_qc_block_height;            //
   uint32_t                finalizer_policy_generation;     // ?

   block_header_state_core next(uint32_t last_qc_block_height, bool is_last_qc_strong) const;
};


struct block_header_state {
   block_id_type                       id;
   block_header                        header;
   protocol_feature_activation_set_ptr activated_protocol_features;

   block_header_state_core             core;
   incremental_merkle_tree             proposal_mtree;
   incremental_merkle_tree             finality_mtree;

   finalizer_policy_ptr                finalizer_policy; // finalizer set + threshold + generation, supports `digest()`
   proposer_policy_ptr                 proposer_policy;  // producer authority schedule, supports `digest()`
   
   flat_map<uint32_t, proposer_policy_ptr> proposer_policies;
   flat_map<uint32_t, finalizer_policy_ptr> finalizer_policies;

   digest_type           compute_finalizer_digest() const;
   block_timestamp_type  timestamp() const;
   account_name          producer() const;
   block_id_type         previous() const;
   uint32_t              block_num() const { return block_header::num_from_id(previous()) + 1; }
   
   block_header_state next(block_timestamp_type when, const assembled_block_input& data) const;
   
   // block descending from this need the provided qc in the block extension
   bool is_needed(const quorum_certificate& qc) const {
      return !core.last_qc_block_height || qc.block_height > *core.last_qc_block_height;
   }
};

}
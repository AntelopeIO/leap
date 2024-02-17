#pragma once
#include <eosio/chain/block_header.hpp>
#include <eosio/chain/incremental_merkle.hpp>
#include <eosio/chain/protocol_feature_manager.hpp>
#include <eosio/chain/hotstuff/hotstuff.hpp>
#include <eosio/chain/hotstuff/finalizer_policy.hpp>
#include <eosio/chain/hotstuff/instant_finality_extension.hpp>
#include <eosio/chain/chain_snapshot.hpp>
#include <future>

namespace eosio::chain {

namespace detail { struct schedule_info; };

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
   std::shared_ptr<proposer_policy>  new_proposer_policy;  // Comes from building_block::new_proposer_policy
   std::optional<finalizer_policy>   new_finalizer_policy; // Comes from building_block::new_finalizer_policy
   std::optional<qc_claim_t>         qc_claim;             // Comes from traversing branch from parent and calling get_best_qc()
                                                           // assert(qc->block_num <= num_from_id(previous));
};

struct block_header_state_core {
   uint32_t                last_final_block_num{0};        // last irreversible (final) block.
   std::optional<uint32_t> final_on_strong_qc_block_num;   // will become final if this header achives a strong QC.
   std::optional<uint32_t> last_qc_block_num;              // The block number of the most recent ancestor block that has a QC justification
   block_timestamp_type    last_qc_block_timestamp;        // The block timestamp of the most recent ancestor block that has a QC justification
   uint32_t                finalizer_policy_generation{0}; //

   block_header_state_core next(qc_claim_t incoming) const;
};

struct block_header_state {
   // ------ data members ------------------------------------------------------------
   block_id_type                       block_id;
   block_header                        header;
   protocol_feature_activation_set_ptr activated_protocol_features;

   block_header_state_core             core;
   incremental_merkle_tree             proposal_mtree;
   incremental_merkle_tree             finality_mtree;

   finalizer_policy_ptr                active_finalizer_policy; // finalizer set + threshold + generation, supports `digest()`
   proposer_policy_ptr                 active_proposer_policy;  // producer authority schedule, supports `digest()`

   // block time when proposer_policy will become active
   flat_map<block_timestamp_type, proposer_policy_ptr>  proposer_policies;
   flat_map<uint32_t, finalizer_policy_ptr> finalizer_policies;


   // ------ data members caching information available elsewhere ----------------------
   header_extension_multimap           header_exts;     // redundant with the data stored in header


   // ------ functions -----------------------------------------------------------------
   // [if todo] https://github.com/AntelopeIO/leap/issues/2080
   const block_id_type&  id() const { return block_id; }
   digest_type           compute_finalizer_digest() const { return block_id; };
   block_timestamp_type  timestamp() const { return header.timestamp; }
   account_name          producer() const  { return header.producer; }
   const block_id_type&  previous() const  { return header.previous; }
   uint32_t              block_num() const { return block_header::num_from_id(previous()) + 1; }
   block_timestamp_type  last_qc_block_timestamp() const { return core.last_qc_block_timestamp; }
   const producer_authority_schedule& active_schedule_auth()  const { return active_proposer_policy->proposer_schedule; }

   block_header_state next(block_header_state_input& data) const;
   block_header_state next(const signed_block_header& h, validator_t& validator) const;

   // block descending from this need the provided qc in the block extension
   bool is_needed(const quorum_certificate& qc) const {
      return !core.last_qc_block_num || qc.block_num > *core.last_qc_block_num;
   }

   flat_set<digest_type> get_activated_protocol_features() const { return activated_protocol_features->protocol_features; }
   const vector<digest_type>& get_new_protocol_feature_activations() const;
   producer_authority get_scheduled_producer(block_timestamp_type t) const;
};

using block_header_state_ptr = std::shared_ptr<block_header_state>;

}

FC_REFLECT( eosio::chain::block_header_state_core,
            (last_final_block_num)(final_on_strong_qc_block_num)(last_qc_block_num)(last_qc_block_timestamp)(finalizer_policy_generation))
FC_REFLECT( eosio::chain::block_header_state,
            (block_id)(header)(activated_protocol_features)(core)(proposal_mtree)(finality_mtree)
            (active_finalizer_policy)(active_proposer_policy)(proposer_policies)(finalizer_policies)(header_exts))

#pragma once
#include <eosio/chain/block_header.hpp>
#include <eosio/chain/finality_core.hpp>
#include <eosio/chain/protocol_feature_manager.hpp>
#include <eosio/chain/hotstuff/hotstuff.hpp>
#include <eosio/chain/hotstuff/finalizer_policy.hpp>
#include <eosio/chain/hotstuff/instant_finality_extension.hpp>
#include <eosio/chain/chain_snapshot.hpp>
#include <future>

namespace eosio::chain {

namespace snapshot_detail {
  struct snapshot_block_state_v7;
}

namespace detail { struct schedule_info; };

// Light header protocol version, separate from protocol feature version
constexpr uint32_t light_header_protocol_version_major = 1;
constexpr uint32_t light_header_protocol_version_minor = 0;

struct building_block_input {
   block_id_type                     parent_id;
   block_timestamp_type              parent_timestamp;
   block_timestamp_type              timestamp;
   account_name                      producer;
   vector<digest_type>               new_protocol_feature_activations;
};

// this struct can be extracted from a building block
struct block_header_state_input : public building_block_input {
   digest_type                       transaction_mroot;    // Comes from std::get<checksum256_type>(building_block::trx_mroot_or_receipt_digests)
   std::shared_ptr<proposer_policy>  new_proposer_policy;  // Comes from building_block::new_proposer_policy
   std::optional<finalizer_policy>   new_finalizer_policy; // Comes from building_block::new_finalizer_policy
   qc_claim_t                        most_recent_ancestor_with_qc; // Comes from traversing branch from parent and calling get_best_qc()
   digest_type                       finality_mroot_claim;
};

struct block_header_state {
   // ------ data members ------------------------------------------------------------
   block_id_type                       block_id;
   block_header                        header;
   protocol_feature_activation_set_ptr activated_protocol_features;

   finality_core                       core;                    // thread safe, not modified after creation

   finalizer_policy_ptr                active_finalizer_policy; // finalizer set + threshold + generation, supports `digest()`
   proposer_policy_ptr                 active_proposer_policy;  // producer authority schedule, supports `digest()`

   // block time when proposer_policy will become active
   flat_map<block_timestamp_type, proposer_policy_ptr>  proposer_policies;
   flat_map<uint32_t, finalizer_policy_ptr> finalizer_policies;


   // ------ data members caching information available elsewhere ----------------------
   header_extension_multimap           header_exts;     // redundant with the data stored in header


   // ------ functions -----------------------------------------------------------------
   const block_id_type&  id()             const { return block_id; }
   const digest_type     finality_mroot() const { return header.is_proper_svnn_block() ? header.action_mroot : digest_type{}; }
   block_timestamp_type  timestamp()      const { return header.timestamp; }
   account_name          producer()       const { return header.producer; }
   const block_id_type&  previous()       const { return header.previous; }
   uint32_t              block_num()      const { return block_header::num_from_id(previous()) + 1; }
   block_timestamp_type  last_qc_block_timestamp() const {
      auto last_qc_block_num  = core.latest_qc_claim().block_num;
      return core.get_block_reference(last_qc_block_num).timestamp;
   }
   const producer_authority_schedule& active_schedule_auth()  const { return active_proposer_policy->proposer_schedule; }
   const protocol_feature_activation_set_ptr& get_activated_protocol_features() const { return activated_protocol_features; }

   block_header_state next(block_header_state_input& data) const;
   block_header_state next(const signed_block_header& h, validator_t& validator) const;

   digest_type compute_base_digest() const;
   digest_type compute_finality_digest() const;

   // Returns true if the block is a Proper Savanna Block
   bool is_proper_svnn_block() const;

   // block descending from this need the provided qc in the block extension
   bool is_needed(const qc_claim_t& qc_claim) const {
      return qc_claim > core.latest_qc_claim();
   }

   const vector<digest_type>& get_new_protocol_feature_activations() const;
   const producer_authority& get_scheduled_producer(block_timestamp_type t) const;
};

using block_header_state_ptr = std::shared_ptr<block_header_state>;

}

FC_REFLECT( eosio::chain::block_header_state, (block_id)(header)
            (activated_protocol_features)(core)(active_finalizer_policy)
            (active_proposer_policy)(proposer_policies)(finalizer_policies)(header_exts))

#include <eosio/chain/block_header_state.hpp>
#include <eosio/chain/block_header_state_utils.hpp>
#include <eosio/chain/hotstuff/instant_finality_extension.hpp>
#include <eosio/chain/hotstuff/proposer_policy.hpp>
#include <eosio/chain/exceptions.hpp>
#include <limits>

namespace eosio::chain {

// this is a versioning scheme that is separate from protocol features that only
// gets updated if a protocol feature causes a breaking change to light block
// header validation

// data for finality_digest
struct finality_digest_data_v0_t {
   uint32_t    major_version{light_header_protocol_version_major};
   uint32_t    minor_version{light_header_protocol_version_minor};
   uint32_t    active_finalizer_policy_generation {0};
   digest_type finality_tree_digest;
   digest_type base_and_active_finalizer_policy_digest;
};

// data for base_and_active_finalizer_policy_digest
struct base_and_active_finalizer_policy_digest_data_t {
   digest_type active_finalizer_policy_digest;
   digest_type base_digest;
};

// data for base_digest
struct base_digest_data_t {
   block_header                             header;
   finality_core                            core;
   flat_map<uint32_t, finalizer_policy_ptr> finalizer_policies;
   proposer_policy_ptr                      active_proposer_policy;
   flat_map<block_timestamp_type, proposer_policy_ptr>  proposer_policies;
   protocol_feature_activation_set_ptr      activated_protocol_features;
};

digest_type block_header_state::compute_finalizer_digest() const {
   auto active_finalizer_policy_digest = fc::sha256::hash(*active_finalizer_policy);

   base_digest_data_t base_digest_data {
      .header                      = header,
      .core                        = core,
      .finalizer_policies          = finalizer_policies,
      .active_proposer_policy      = active_proposer_policy,
      .proposer_policies           = proposer_policies,
      .activated_protocol_features = activated_protocol_features
   };
   auto base_digest = fc::sha256::hash(base_digest_data);

   base_and_active_finalizer_policy_digest_data_t b_afp_digest_data {
      .active_finalizer_policy_digest = active_finalizer_policy_digest,
      .base_digest                    = base_digest
   };
   auto b_afp_digest = fc::sha256::hash(b_afp_digest_data);

   finality_digest_data_v0_t finality_digest_data {
      .active_finalizer_policy_generation = active_finalizer_policy->generation,
      .finality_tree_digest               = finality_mroot(),
      .base_and_active_finalizer_policy_digest = b_afp_digest
   };

   return fc::sha256::hash(finality_digest_data);
}

const producer_authority& block_header_state::get_scheduled_producer(block_timestamp_type t) const {
   return detail::get_scheduled_producer(active_proposer_policy->proposer_schedule.producers, t);
}

const vector<digest_type>& block_header_state::get_new_protocol_feature_activations()const {
   return detail::get_new_protocol_feature_activations(header_exts);
}

#warning Add last_proposed_finalizer_policy_generation to snapshot_block_header_state_v3, see header file TODO
   
block_header_state block_header_state::next(block_header_state_input& input) const {
   block_header_state result;

   // header
   // ------
   result.header = {
      .timestamp         = input.timestamp, // [greg todo] do we have to do the slot++ stuff from the legacy version?
      .producer          = input.producer,
      .confirmed         = 0,
      .previous          = input.parent_id,
      .transaction_mroot = input.transaction_mroot,
      .action_mroot      = input.finality_mroot_claim ? *input.finality_mroot_claim : digest_type{},
      .schedule_version  = block_header::proper_svnn_block_flag
   };

   // activated protocol features
   // ---------------------------
   if (!input.new_protocol_feature_activations.empty()) {
      result.activated_protocol_features = std::make_shared<protocol_feature_activation_set>(
         *activated_protocol_features, input.new_protocol_feature_activations);
   } else {
      result.activated_protocol_features = activated_protocol_features;
   }

   // proposer policy
   // ---------------
   result.active_proposer_policy = active_proposer_policy;

   if(!proposer_policies.empty()) {
      auto it = proposer_policies.begin();
      // +1 since this is called after the block is built, this will be the active schedule for the next block
      if (it->first.slot <= input.timestamp.slot + 1) {
         result.active_proposer_policy = it->second;
         result.header.schedule_version = block_header::proper_svnn_block_flag;
         result.active_proposer_policy->proposer_schedule.version = result.header.schedule_version;
         result.proposer_policies = { ++it, proposer_policies.end() };
      } else {
         result.proposer_policies = proposer_policies;
      }
   }

   if (input.new_proposer_policy) {
      // called when assembling the block
      result.proposer_policies[input.new_proposer_policy->active_time] = input.new_proposer_policy;
   }

   // finalizer policy
   // ----------------
   result.active_finalizer_policy = active_finalizer_policy;

   // [greg todo] correct support for new finalizer_policy activation using finalizer_policies map
   //   if (input.new_finalizer_policy)
   //      ++input.new_finalizer_policy->generation;

   // finality_core
   // -------------
   block_ref parent_block {
      .block_id  = input.parent_id,
      .timestamp = input.parent_timestamp
   };
   result.core = core.next(parent_block, input.most_recent_ancestor_with_qc);

   // finality extension
   // ------------------
   instant_finality_extension new_if_ext {input.most_recent_ancestor_with_qc,
                                          std::move(input.new_finalizer_policy),
                                          std::move(input.new_proposer_policy)};
   uint16_t if_ext_id = instant_finality_extension::extension_id();
   emplace_extension(result.header.header_extensions, if_ext_id, fc::raw::pack(new_if_ext));
   result.header_exts.emplace(if_ext_id, std::move(new_if_ext));

   // add protocol_feature_activation extension
   // -----------------------------------------
   if (!input.new_protocol_feature_activations.empty()) {
      uint16_t ext_id = protocol_feature_activation::extension_id();
      protocol_feature_activation pfa_ext{.protocol_features = std::move(input.new_protocol_feature_activations)};

      emplace_extension(result.header.header_extensions, ext_id, fc::raw::pack(pfa_ext));
      result.header_exts.emplace(ext_id, std::move(pfa_ext));
   }

   // Finally update block id from header
   // -----------------------------------
   result.block_id = result.header.calculate_id();

   return result;
}

/**
 *  Transitions the current header state into the next header state given the supplied signed block header.
 *
 *  Given a signed block header, generate the expected template based upon the header time,
 *  then validate that the provided header matches the template.
 */
block_header_state block_header_state::next(const signed_block_header& h, validator_t& validator) const {
   auto producer = detail::get_scheduled_producer(active_proposer_policy->proposer_schedule.producers, h.timestamp).producer_name;
   
   EOS_ASSERT( h.previous == block_id, unlinkable_block_exception, "previous mismatch ${p} != ${id}", ("p", h.previous)("id", block_id) );
   EOS_ASSERT( h.producer == producer, wrong_producer, "wrong producer specified" );
   EOS_ASSERT( h.confirmed == 0, block_validate_exception, "invalid confirmed ${c}", ("c", h.confirmed) );
   EOS_ASSERT( !h.new_producers, producer_schedule_exception, "Block header contains legacy producer schedule outdated by activation of WTMsig Block Signatures" );

   auto exts = h.validate_and_extract_header_extensions();

   // retrieve protocol_feature_activation from incoming block header extension
   // -------------------------------------------------------------------------
   vector<digest_type> new_protocol_feature_activations;
   if( exts.count(protocol_feature_activation::extension_id() > 0) ) {
      auto  pfa_entry = exts.lower_bound(protocol_feature_activation::extension_id());
      auto& pfa_ext   = std::get<protocol_feature_activation>(pfa_entry->second);
      new_protocol_feature_activations = std::move(pfa_ext.protocol_features);
      validator( timestamp(), activated_protocol_features->protocol_features, new_protocol_feature_activations );
   }

   // retrieve instant_finality_extension data from block header extension
   // --------------------------------------------------------------------
   EOS_ASSERT(exts.count(instant_finality_extension::extension_id()) > 0, invalid_block_header_extension,
              "Instant Finality Extension is expected to be present in all block headers after switch to IF");
   auto  if_entry = exts.lower_bound(instant_finality_extension::extension_id());
   auto& if_ext   = std::get<instant_finality_extension>(if_entry->second);

   building_block_input bb_input{
      .parent_id        = block_id,
      .parent_timestamp = timestamp(),
      .timestamp        = h.timestamp,
      .producer         = producer,
      .new_protocol_feature_activations = std::move(new_protocol_feature_activations)
   };

   digest_type action_mroot; // digest_type default value is empty

   if (h.is_proper_svnn_block()) {
      // if there is no Finality Tree Root associated to the block,
      // then this needs to validate that h.action_mroot is the empty digest
      if (h.action_mroot.empty()) {
         auto next_core_metadata = core.next_metadata(if_ext.qc_claim);
         EOS_ASSERT(core.is_genesis_block_num(next_core_metadata.final_on_strong_qc_block_num),
            block_validate_exception,
            "next block's action_mroot is empty but the block associated with its final_on_strong_qc_block_numa (${n}) is not genesis block",
            ("n", next_core_metadata.final_on_strong_qc_block_num));
      }

      action_mroot = h.action_mroot;
   };

   block_header_state_input bhs_input{
      bb_input,
      h.transaction_mroot,
      if_ext.new_proposer_policy,
      if_ext.new_finalizer_policy,
      if_ext.qc_claim,
      action_mroot // for finality_mroot_claim
   };

   return next(bhs_input);
}

} // namespace eosio::chain

FC_REFLECT( eosio::chain::finality_digest_data_v0_t, (major_version)(minor_version)(active_finalizer_policy_generation)(finality_tree_digest)(base_and_active_finalizer_policy_digest) )
FC_REFLECT( eosio::chain::base_and_active_finalizer_policy_digest_data_t, (active_finalizer_policy_digest)(base_digest) )
FC_REFLECT( eosio::chain::base_digest_data_t, (header)(core)(finalizer_policies)(active_proposer_policy)(proposer_policies)(activated_protocol_features) )

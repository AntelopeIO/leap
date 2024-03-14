#include <eosio/chain/block_header_state.hpp>
#include <eosio/chain/block_header_state_utils.hpp>
#include <eosio/chain/hotstuff/instant_finality_extension.hpp>
#include <eosio/chain/hotstuff/proposer_policy.hpp>
#include <eosio/chain/exceptions.hpp>
#include <limits>

namespace eosio::chain {

// moved this warning out of header so it only uses once
#warning TDDO https://github.com/AntelopeIO/leap/issues/2080
// digest_type           compute_finalizer_digest() const { return id; };


const producer_authority& block_header_state::get_scheduled_producer(block_timestamp_type t) const {
   return detail::get_scheduled_producer(active_proposer_policy->proposer_schedule.producers, t);
}

const vector<digest_type>& block_header_state::get_new_protocol_feature_activations()const {
   return detail::get_new_protocol_feature_activations(header_exts);
}

#warning Add last_proposed_finalizer_policy_generation to snapshot_block_header_state_v3, see header file TODO

void finish_next(const block_header_state& prev,
                 block_header_state& result,
                 vector<digest_type> new_protocol_feature_activations,
                 std::shared_ptr<proposer_policy> new_proposer_policy,
                 qc_claim_t qc_claim) {

   // activated protocol features
   // ---------------------------
   if (!new_protocol_feature_activations.empty()) {
      result.activated_protocol_features = std::make_shared<protocol_feature_activation_set>(
         *prev.activated_protocol_features, std::move(new_protocol_feature_activations));
   } else {
      result.activated_protocol_features = prev.activated_protocol_features;
   }

   // proposer policy
   // ---------------
   result.active_proposer_policy = prev.active_proposer_policy;

   if(!prev.proposer_policies.empty()) {
      auto it = prev.proposer_policies.begin();
      // +1 since this is called after the block is built, this will be the active schedule for the next block
      if (it->first.slot <= result.header.timestamp.slot + 1) {
         result.active_proposer_policy = it->second;
         result.header.schedule_version = prev.header.schedule_version + 1;
         result.active_proposer_policy->proposer_schedule.version = result.header.schedule_version;
         result.proposer_policies = { ++it, prev.proposer_policies.end() };
      } else {
         result.proposer_policies = prev.proposer_policies;
      }
   }

   if (new_proposer_policy) {
      // called when assembling the block
      result.proposer_policies[new_proposer_policy->active_time] = std::move(new_proposer_policy);
   }

   // finalizer policy
   // ----------------
   result.active_finalizer_policy = prev.active_finalizer_policy;

   // finality_core
   // -----------------------
   block_ref parent_block {
      .block_id  = prev.block_id,
      .timestamp = prev.timestamp()
   };
   result.core = prev.core.next(parent_block, qc_claim);

   // Finally update block id from header
   // -----------------------------------
   result.block_id = result.header.calculate_id();
}
   
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
      .action_mroot      = input.action_mroot,
      .schedule_version  = header.schedule_version
   };

   instant_finality_extension new_if_ext {input.most_recent_ancestor_with_qc,
                                          std::move(input.new_finalizer_policy),
                                          input.new_proposer_policy};

   uint16_t if_ext_id = instant_finality_extension::extension_id();
   emplace_extension(result.header.header_extensions, if_ext_id, fc::raw::pack(new_if_ext));
   result.header_exts.emplace(if_ext_id, std::move(new_if_ext));

   // add protocol_feature_activation extension
   // -----------------------------------------
   if (!input.new_protocol_feature_activations.empty()) {
      uint16_t ext_id = protocol_feature_activation::extension_id();
      protocol_feature_activation pfa_ext{.protocol_features = input.new_protocol_feature_activations};

      emplace_extension(result.header.header_extensions, ext_id, fc::raw::pack(pfa_ext));
      result.header_exts.emplace(ext_id, std::move(pfa_ext));
   }

   finish_next(*this, result, std::move(input.new_protocol_feature_activations), std::move(input.new_proposer_policy), input.most_recent_ancestor_with_qc);

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
   EOS_ASSERT( !h.new_producers, producer_schedule_exception, "Block header contains legacy producer schedule outdated by activation of WTMsig Block Signatures" );

   block_header_state result;
   result.header = static_cast<const block_header&>(h);
   result.header_exts = h.validate_and_extract_header_extensions();
   auto& exts = result.header_exts;

   // retrieve protocol_feature_activation from incoming block header extension
   // -------------------------------------------------------------------------
   vector<digest_type> new_protocol_feature_activations;
   if( exts.count(protocol_feature_activation::extension_id() > 0) ) {
      auto  pfa_entry = exts.lower_bound(protocol_feature_activation::extension_id());
      auto& pfa_ext   = std::get<protocol_feature_activation>(pfa_entry->second);
      new_protocol_feature_activations = pfa_ext.protocol_features;
      validator( timestamp(), activated_protocol_features->protocol_features, new_protocol_feature_activations );
   }

   // retrieve instant_finality_extension data from block header extension
   // --------------------------------------------------------------------
   EOS_ASSERT(exts.count(instant_finality_extension::extension_id()) > 0, invalid_block_header_extension,
              "Instant Finality Extension is expected to be present in all block headers after switch to IF");
   auto  if_entry = exts.lower_bound(instant_finality_extension::extension_id());
   auto& if_ext   = std::get<instant_finality_extension>(if_entry->second);

   finish_next(*this, result, std::move(new_protocol_feature_activations), if_ext.new_proposer_policy, if_ext.qc_claim);

   return result;
}

} // namespace eosio::chain

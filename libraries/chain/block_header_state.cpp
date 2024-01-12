#include <eosio/chain/block_header_state.hpp>
#include <eosio/chain/block_header_state_utils.hpp>
#include <eosio/chain/hotstuff/instant_finality_extension.hpp>
#include <eosio/chain/hotstuff/proposer_policy.hpp>
#include <eosio/chain/exceptions.hpp>
#include <limits>

namespace eosio::chain {

producer_authority block_header_state::get_scheduled_producer(block_timestamp_type t) const {
   return detail::get_scheduled_producer(active_proposer_policy->proposer_schedule.producers, t);
}

#warning Add last_proposed_finalizer_policy_generation to snapshot_block_header_state_v3, see header file TODO
   
block_header_state_core block_header_state_core::next(const uint32_t last_qc_block_num, bool is_last_qc_strong) const {
   // no state change if last_qc_block_num is the same
   if (last_qc_block_num == this->last_qc_block_num) {
      return {*this};
   }

   EOS_ASSERT(last_qc_block_num > this->last_qc_block_num, block_validate_exception,
              "new last_qc_block_num must be greater than old last_qc_block_num");

   auto old_last_qc_block_num            = this->last_qc_block_num;
   auto old_final_on_strong_qc_block_num = this->final_on_strong_qc_block_num;

   block_header_state_core result{*this};

   if (is_last_qc_strong) {
      // last QC is strong. We can progress forward.

      // block with old final_on_strong_qc_block_num becomes irreversible
      if (old_final_on_strong_qc_block_num.has_value()) {
         result.last_final_block_num = *old_final_on_strong_qc_block_num;
      }

      // next block which can become irreversible is the block with
      // old last_qc_block_num
      if (old_last_qc_block_num.has_value()) {
         result.final_on_strong_qc_block_num = *old_last_qc_block_num;
      }
   } else {
      // new final_on_strong_qc_block_num should not be present
      result.final_on_strong_qc_block_num.reset();

      // new last_final_block_num should be the same as the old last_final_block_num
   }

   // new last_qc_block_num is always the input last_qc_block_num.
   result.last_qc_block_num = last_qc_block_num;

   return result;
}


block_header_state block_header_state::next(block_header_state_input& input) const {
   block_header_state result;

   // header
   // ------
   result.header = block_header {
      .timestamp         = input.timestamp, // [greg todo] do we have to do the slot++ stuff from the legacy version?
      .producer          = input.producer,
      .confirmed         = hs_block_confirmed, // todo: consider 0 instead
      .previous          = input.parent_id,
      .transaction_mroot = input.transaction_mroot,
      .action_mroot      = input.action_mroot,
      .schedule_version  = header.schedule_version
   };

   result.active_finalizer_policy = active_finalizer_policy;
   result.active_proposer_policy = active_proposer_policy;

   if(!proposer_policies.empty()) {
      auto it = proposer_policies.begin();
      if (it->first <= input.timestamp) {
         result.active_proposer_policy = it->second;
         result.header.schedule_version = header.schedule_version + 1;
         result.active_proposer_policy->proposer_schedule.version = result.header.schedule_version;
         result.proposer_policies = { ++it, proposer_policies.end() };
      } else {
         result.proposer_policies = proposer_policies;
      }
   }
   if (input.new_proposer_policy) {
      // called when assembling the block
      result.proposer_policies[result.header.timestamp] = input.new_proposer_policy;
   }

   // core
   // ----
   if (input.qc_info)
      result.core = core.next(input.qc_info->last_qc_block_num, input.qc_info->is_last_qc_strong);
   else
      result.core = core;

   if (!input.new_protocol_feature_activations.empty()) {
      result.activated_protocol_features = std::make_shared<protocol_feature_activation_set>(
         *activated_protocol_features, input.new_protocol_feature_activations);
   } else {
      result.activated_protocol_features = activated_protocol_features;
   }

   // add block header extensions
   // ---------------------------
   if (input.new_finalizer_policy)
      ++input.new_finalizer_policy->generation;

   std::optional<qc_info_t> qc_info = input.qc_info;
   if (!qc_info) {
      // [greg todo]: copy the one from the previous block (look in header.header_extensions)
   }
   
   emplace_extension(result.header.header_extensions, instant_finality_extension::extension_id(),
                     fc::raw::pack(instant_finality_extension{qc_info,
                                                              std::move(input.new_finalizer_policy),
                                                              std::move(input.new_proposer_policy)}));
               
   return result;
}

/**
 *  Transitions the current header state into the next header state given the supplied signed block header.
 *
 *  Given a signed block header, generate the expected template based upon the header time,
 *  then validate that the provided header matches the template.
 *
 *  If the header specifies new_producers then apply them accordingly.
 */
block_header_state block_header_state::next(const signed_block_header& h, const protocol_feature_set& pfs,
                                            validator_t& validator) const {
   auto producer = detail::get_scheduled_producer(active_proposer_policy->proposer_schedule.producers, h.timestamp).producer_name;
   
   EOS_ASSERT( h.previous == id, unlinkable_block_exception, "previous mismatch" );
   EOS_ASSERT( h.producer == producer, wrong_producer, "wrong producer specified" );

   auto exts = h.validate_and_extract_header_extensions();

   // handle protocol_feature_activation from incoming block
   // ------------------------------------------------------
   vector<digest_type> new_protocol_feature_activations;
   if( exts.count(protocol_feature_activation::extension_id() > 0) ) {
      const auto& entry = exts.lower_bound(protocol_feature_activation::extension_id());
      new_protocol_feature_activations = std::move(std::get<protocol_feature_activation>(entry->second).protocol_features);
   }

   // retrieve instant_finality_extension data from block extension
   // -------------------------------------------------------------
   EOS_ASSERT(exts.count(instant_finality_extension::extension_id() > 0), misc_exception,
              "Instant Finality Extension is expected to be present in all block headers after switch to IF");
   const auto& if_entry = exts.lower_bound(instant_finality_extension::extension_id());
   const auto& if_ext   = std::get<instant_finality_extension>(if_entry->second);

   building_block_input bb_input{
      .parent_id = id,
      .timestamp = h.timestamp,
      .producer  = producer,
      .new_protocol_feature_activations = std::move(new_protocol_feature_activations)
   };

   block_header_state_input bhs_input{
      bb_input,      h.transaction_mroot, h.action_mroot, if_ext.new_proposer_policy, if_ext.new_finalizer_policy,
      if_ext.qc_info};

   return next(bhs_input);
}

} // namespace eosio::chain
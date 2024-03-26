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
struct finality_digest_data_v1 {
   uint32_t    major_version{light_header_protocol_version_major};
   uint32_t    minor_version{light_header_protocol_version_minor};
   uint32_t    active_finalizer_policy_generation {0};
   digest_type finality_tree_digest;
   digest_type active_finalizer_policy_and_base_digest;
};

// compute base_digest explicitly because of pointers involved.
digest_type block_header_state::compute_base_digest() const {
   digest_type::encoder enc;

   fc::raw::pack( enc, header );
   fc::raw::pack( enc, core );

   for (const auto& fp_pair : finalizer_policies) {
      fc::raw::pack( enc, fp_pair.first );
      assert(fp_pair.second);
      fc::raw::pack( enc, *fp_pair.second );
   }

   assert(active_proposer_policy);
   fc::raw::pack( enc, *active_proposer_policy );

   for (const auto& pp_pair : proposer_policies) {
      assert(pp_pair.second);
      fc::raw::pack( enc, *pp_pair.second );
   }

   if (activated_protocol_features) {
      fc::raw::pack( enc, *activated_protocol_features );
   }

   return enc.result();
}

digest_type block_header_state::compute_finality_digest() const {
   assert(active_finalizer_policy);
   auto active_finalizer_policy_digest = fc::sha256::hash(*active_finalizer_policy);
   auto base_digest = compute_base_digest();

   std::pair<const digest_type&, const digest_type&> active_and_base{ active_finalizer_policy_digest, base_digest };
   auto afp_base_digest = fc::sha256::hash(active_and_base);

   finality_digest_data_v1 finality_digest_data {
      .active_finalizer_policy_generation      = active_finalizer_policy->generation,
      .finality_tree_digest                    = finality_mroot(),
      .active_finalizer_policy_and_base_digest = afp_base_digest
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

void finish_next(const block_header_state& prev,
                 block_header_state& next_header_state,
                 vector<digest_type> new_protocol_feature_activations,
                 std::shared_ptr<proposer_policy> new_proposer_policy,
                 qc_claim_t qc_claim) {

   // activated protocol features
   // ---------------------------
   if (!new_protocol_feature_activations.empty()) {
      next_header_state.activated_protocol_features = std::make_shared<protocol_feature_activation_set>(
         *prev.activated_protocol_features, std::move(new_protocol_feature_activations));
   } else {
      next_header_state.activated_protocol_features = prev.activated_protocol_features;
   }

   // proposer policy
   // ---------------
   next_header_state.active_proposer_policy = prev.active_proposer_policy;

   if(!prev.proposer_policies.empty()) {
      auto it = prev.proposer_policies.begin();
      // +1 since this is called after the block is built, this will be the active schedule for the next block
      if (it->first.slot <= next_header_state.header.timestamp.slot + 1) {
         next_header_state.active_proposer_policy = it->second;
         next_header_state.proposer_policies = { ++it, prev.proposer_policies.end() };
      } else {
         next_header_state.proposer_policies = prev.proposer_policies;
      }
   }

   if (new_proposer_policy) {
      // called when assembling the block
      next_header_state.proposer_policies[new_proposer_policy->active_time] = std::move(new_proposer_policy);
   }

   // finalizer policy
   // ----------------
   next_header_state.active_finalizer_policy = prev.active_finalizer_policy;

   // finality_core
   // -------------
   block_ref parent_block {
      .block_id  = prev.block_id,
      .timestamp = prev.timestamp()
   };
   next_header_state.core = prev.core.next(parent_block, qc_claim);

   // Finally update block id from header
   // -----------------------------------
   next_header_state.block_id = next_header_state.header.calculate_id();
}
   
block_header_state block_header_state::next(block_header_state_input& input) const {
   block_header_state next_header_state;

   // header
   // ------
   next_header_state.header = {
      .timestamp         = input.timestamp,
      .producer          = input.producer,
      .confirmed         = 0,
      .previous          = input.parent_id,
      .transaction_mroot = input.transaction_mroot,
      .action_mroot      = input.finality_mroot_claim,
      .schedule_version  = block_header::proper_svnn_schedule_version
   };

   // finality extension
   // ------------------
   instant_finality_extension new_if_ext {input.most_recent_ancestor_with_qc,
                                          std::move(input.new_finalizer_policy),
                                          input.new_proposer_policy};

   uint16_t if_ext_id = instant_finality_extension::extension_id();
   emplace_extension(next_header_state.header.header_extensions, if_ext_id, fc::raw::pack(new_if_ext));
   next_header_state.header_exts.emplace(if_ext_id, std::move(new_if_ext));

   // add protocol_feature_activation extension
   // -----------------------------------------
   if (!input.new_protocol_feature_activations.empty()) {
      uint16_t ext_id = protocol_feature_activation::extension_id();
      protocol_feature_activation pfa_ext{.protocol_features = input.new_protocol_feature_activations};

      emplace_extension(next_header_state.header.header_extensions, ext_id, fc::raw::pack(pfa_ext));
      next_header_state.header_exts.emplace(ext_id, std::move(pfa_ext));
   }

   finish_next(*this, next_header_state, std::move(input.new_protocol_feature_activations), std::move(input.new_proposer_policy), input.most_recent_ancestor_with_qc);

   return next_header_state;
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

   block_header_state next_header_state;
   next_header_state.header = static_cast<const block_header&>(h);
   next_header_state.header_exts = h.validate_and_extract_header_extensions();
   auto& exts = next_header_state.header_exts;

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

   if (h.is_proper_svnn_block()) {
      // if there is no Finality Tree Root associated with the block,
      // then this needs to validate that h.action_mroot is the empty digest
      auto next_core_metadata = core.next_metadata(if_ext.qc_claim);
      bool no_finality_tree_associated = core.is_genesis_block_num(next_core_metadata.final_on_strong_qc_block_num);

      EOS_ASSERT(no_finality_tree_associated == h.action_mroot.empty(), block_validate_exception,
                 "No Finality Tree Root associated with the block, does not match with empty action_mroot: "
                 "(${n}), action_mroot empty (${e}), final_on_strong_qc_block_num (${f})",
                 ("n", no_finality_tree_associated)("e", h.action_mroot.empty())("f", next_core_metadata.final_on_strong_qc_block_num));
   };

   finish_next(*this, next_header_state, std::move(new_protocol_feature_activations), if_ext.new_proposer_policy, if_ext.qc_claim);

   return next_header_state;
}

} // namespace eosio::chain

FC_REFLECT( eosio::chain::finality_digest_data_v1, (major_version)(minor_version)(active_finalizer_policy_generation)(finality_tree_digest)(active_finalizer_policy_and_base_digest) )

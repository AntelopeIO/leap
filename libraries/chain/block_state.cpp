#include <eosio/chain/block_state.hpp>
#include <eosio/chain/block_header_state_utils.hpp>
#include <eosio/chain/block_state_legacy.hpp>
#include <eosio/chain/hotstuff/finalizer.hpp>
#include <eosio/chain/snapshot_detail.hpp>
#include <eosio/chain/exceptions.hpp>

#include <fc/crypto/bls_utils.hpp>

namespace eosio::chain {

block_state::block_state(const block_header_state& prev, signed_block_ptr b, const protocol_feature_set& pfs,
                         const validator_t& validator, bool skip_validate_signee)
   : block_header_state(prev.next(*b, validator))
   , block(std::move(b))
   , strong_digest(compute_finality_digest())
   , weak_digest(create_weak_digest(strong_digest))
   , pending_qc(prev.active_finalizer_policy->finalizers.size(), prev.active_finalizer_policy->threshold, prev.active_finalizer_policy->max_weak_sum_before_weak_final())
{
   // ASSUMPTION FROM controller_impl::apply_block = all untrusted blocks will have their signatures pre-validated here
   if( !skip_validate_signee ) {
      auto sigs = detail::extract_additional_signatures(block);
      const auto& valid_block_signing_authority = prev.get_scheduled_producer(timestamp()).authority;
      verify_signee(sigs, valid_block_signing_authority);
   }
}

block_state::block_state(const block_header_state&                bhs,
                         deque<transaction_metadata_ptr>&&        trx_metas,
                         deque<transaction_receipt>&&             trx_receipts,
                         const std::optional<valid_t>&            valid,
                         const std::optional<quorum_certificate>& qc,
                         const signer_callback_type&              signer,
                         const block_signing_authority&           valid_block_signing_authority,
                         const digest_type&                       action_mroot)
   : block_header_state(bhs)
   , block(std::make_shared<signed_block>(signed_block_header{bhs.header}))
   , strong_digest(compute_finality_digest())
   , weak_digest(create_weak_digest(strong_digest))
   , pending_qc(bhs.active_finalizer_policy->finalizers.size(), bhs.active_finalizer_policy->threshold, bhs.active_finalizer_policy->max_weak_sum_before_weak_final())
   , valid(valid)
   , pub_keys_recovered(true) // called by produce_block so signature recovery of trxs must have been done
   , cached_trxs(std::move(trx_metas))
   , action_mroot(action_mroot)
{
   block->transactions = std::move(trx_receipts);

   if( qc ) {
      dlog("integrate qc ${qc} into block ${bn} ${id}", ("qc", qc->to_qc_claim())("bn", block_num())("id", id()));
      emplace_extension(block->block_extensions, quorum_certificate_extension::extension_id(), fc::raw::pack( *qc ));
   }

   sign(signer, valid_block_signing_authority);
}

// Used for transition from dpos to Savanna.
block_state_ptr block_state::create_if_genesis_block(const block_state_legacy& bsp) {
   assert(bsp.action_mroot_savanna);

   auto result_ptr = std::make_shared<block_state>();
   auto &result = *result_ptr;

   // set block_header_state data ----
   result.block_id = bsp.id();
   result.header = bsp.header;
   result.activated_protocol_features = bsp.activated_protocol_features;
   result.core = finality_core::create_core_for_genesis_block(bsp.block_num());

   assert(bsp.block->contains_header_extension(instant_finality_extension::extension_id())); // required by transition mechanism
   instant_finality_extension if_ext = bsp.block->extract_header_extension<instant_finality_extension>();
   assert(if_ext.new_finalizer_policy); // required by transition mechanism
   result.active_finalizer_policy = std::make_shared<finalizer_policy>(*if_ext.new_finalizer_policy);
   result.active_proposer_policy = std::make_shared<proposer_policy>();
   result.active_proposer_policy->active_time = bsp.timestamp();
   result.active_proposer_policy->proposer_schedule = bsp.active_schedule;
   result.proposer_policies = {};  // none pending at IF genesis block
   result.finalizer_policies = {}; // none pending at IF genesis block
   result.header_exts = bsp.header_exts;

   // set block_state data ----
   result.block = bsp.block;
   result.strong_digest = result.compute_finality_digest(); // all block_header_state data populated in result at this point
   result.weak_digest = create_weak_digest(result.strong_digest);

   // TODO: https://github.com/AntelopeIO/leap/issues/2057
   // TODO: Do not aggregate votes on blocks created from block_state_legacy. This can be removed when #2057 complete.
   result.pending_qc = pending_quorum_certificate{result.active_finalizer_policy->finalizers.size(), result.active_finalizer_policy->threshold, result.active_finalizer_policy->max_weak_sum_before_weak_final()};

   // build leaf_node and validation_tree
   valid_t::finality_leaf_node_t leaf_node {
      .block_num       = bsp.block_num(),
      .finality_digest = result.strong_digest,
      .action_mroot    = *bsp.action_mroot_savanna
   };
   // construct valid structure
   incremental_merkle_tree validation_tree;
   validation_tree.append(fc::sha256::hash(leaf_node));
   result.valid = valid_t {
      .validation_tree   = validation_tree,
      .validation_mroots = { validation_tree.get_root() }
   };

   result.validated.store(bsp.is_valid());
   result.pub_keys_recovered = bsp._pub_keys_recovered;
   result.cached_trxs = bsp._cached_trxs;
   result.action_mroot = *bsp.action_mroot_savanna;
   result.base_digest = {}; // calculated on demand in get_finality_data()

   return result_ptr;
}

block_state::block_state(snapshot_detail::snapshot_block_state_v7&& sbs)
   : block_header_state {
         .block_id                    = sbs.block_id,
         .header                      = std::move(sbs.header),
         .activated_protocol_features = std::move(sbs.activated_protocol_features),
         .core                        = std::move(sbs.core),
         .active_finalizer_policy     = std::move(sbs.active_finalizer_policy),
         .active_proposer_policy      = std::move(sbs.active_proposer_policy),
         .proposer_policies           = std::move(sbs.proposer_policies),
         .finalizer_policies          = std::move(sbs.finalizer_policies)
      }
   , strong_digest(compute_finality_digest())
   , weak_digest(create_weak_digest(strong_digest))
   , pending_qc(active_finalizer_policy->finalizers.size(), active_finalizer_policy->threshold,
                active_finalizer_policy->max_weak_sum_before_weak_final()) // just in case we receive votes
   , valid(std::move(sbs.valid))
{
   header_exts = header.validate_and_extract_header_extensions();
}

deque<transaction_metadata_ptr> block_state::extract_trxs_metas() {
   pub_keys_recovered = false;
   auto result = std::move(cached_trxs);
   cached_trxs.clear();
   return result;
}

void block_state::set_trxs_metas( deque<transaction_metadata_ptr>&& trxs_metas, bool keys_recovered ) {
   pub_keys_recovered = keys_recovered;
   cached_trxs = std::move( trxs_metas );
}

// Called from net threads
vote_status block_state::aggregate_vote(const vote_message& vote) {
   const auto& finalizers = active_finalizer_policy->finalizers;
   auto it = std::find_if(finalizers.begin(),
                          finalizers.end(),
                          [&](const auto& finalizer) { return finalizer.public_key == vote.finalizer_key; });

   if (it != finalizers.end()) {
      auto index = std::distance(finalizers.begin(), it);
      auto digest = vote.strong ? strong_digest.to_uint8_span() : std::span<const uint8_t>(weak_digest);
      return pending_qc.add_vote(block_num(),
                                 vote.strong,
                                 digest,
                                 index,
                                 vote.finalizer_key,
                                 vote.sig,
                                 finalizers[index].weight);
   } else {
      wlog( "finalizer_key (${k}) in vote is not in finalizer policy", ("k", vote.finalizer_key) );
      return vote_status::unknown_public_key;
   }
}

bool block_state::has_voted(const bls_public_key& key) const {
   const auto& finalizers = active_finalizer_policy->finalizers;
   auto it = std::find_if(finalizers.begin(),
                          finalizers.end(),
                          [&](const auto& finalizer) { return finalizer.public_key == key; });

   if (it != finalizers.end()) {
      auto index = std::distance(finalizers.begin(), it);
      return pending_qc.has_voted(index);
   }
   return false;
}

// Called from net threads
void block_state::verify_qc(const valid_quorum_certificate& qc) const {
   const auto& finalizers = active_finalizer_policy->finalizers;
   auto num_finalizers = finalizers.size();

   // utility to accumulate voted weights
   auto weights = [&] ( const hs_bitset& votes_bitset ) -> uint64_t {
      uint64_t sum = 0;
      auto n = std::min(num_finalizers, votes_bitset.size());
      for (auto i = 0u; i < n; ++i) {
         if( votes_bitset[i] ) { // ith finalizer voted
            sum += finalizers[i].weight;
         }
      }
      return sum;
   };

   // compute strong and weak accumulated weights
   auto strong_weights = qc._strong_votes ? weights( *qc._strong_votes ) : 0;
   auto weak_weights = qc._weak_votes ? weights( *qc._weak_votes ) : 0;

   // verfify quorum is met
   if( qc.is_strong() ) {
      EOS_ASSERT( strong_weights >= active_finalizer_policy->threshold,
                  invalid_qc_claim,
                  "strong quorum is not met, strong_weights: ${s}, threshold: ${t}",
                  ("s", strong_weights)("t", active_finalizer_policy->threshold) );
   } else {
      EOS_ASSERT( strong_weights + weak_weights >= active_finalizer_policy->threshold,
                  invalid_qc_claim,
                  "weak quorum is not met, strong_weights: ${s}, weak_weights: ${w}, threshold: ${t}",
                  ("s", strong_weights)("w", weak_weights)("t", active_finalizer_policy->threshold) );
   }

   // no reason to use bls_public_key wrapper
   std::vector<bls12_381::g1> pubkeys;
   pubkeys.reserve(2);
   std::vector<std::vector<uint8_t>> digests;
   digests.reserve(2);

   // utility to aggregate public keys for verification
   auto aggregate_pubkeys = [&](const auto& votes_bitset) -> bls12_381::g1 {
      const auto n = std::min(num_finalizers, votes_bitset.size());
      std::vector<bls12_381::g1> pubkeys_to_aggregate;
      pubkeys_to_aggregate.reserve(n);
      for(auto i = 0u; i < n; ++i) {
         if (votes_bitset[i]) { // ith finalizer voted
            pubkeys_to_aggregate.emplace_back(finalizers[i].public_key.jacobian_montgomery_le());
         }
      }

      return bls12_381::aggregate_public_keys(pubkeys_to_aggregate);
   };

   // aggregate public keys and digests for strong and weak votes
   if( qc._strong_votes ) {
      pubkeys.emplace_back(aggregate_pubkeys(*qc._strong_votes));
      digests.emplace_back(std::vector<uint8_t>{strong_digest.data(), strong_digest.data() + strong_digest.data_size()});
   }

   if( qc._weak_votes ) {
      pubkeys.emplace_back(aggregate_pubkeys(*qc._weak_votes));
      digests.emplace_back(std::vector<uint8_t>{weak_digest.begin(), weak_digest.end()});
   }

   // validate aggregated signature
   EOS_ASSERT( bls12_381::aggregate_verify(pubkeys, digests, qc._sig.jacobian_montgomery_le()),
               invalid_qc_claim, "signature validation failed" );
}

valid_t block_state::new_valid(const block_header_state& next_bhs, const digest_type& action_mroot, const digest_type& strong_digest) const {
   assert(valid);
   assert(next_bhs.core.last_final_block_num() >= core.last_final_block_num());

   // Copy parent's validation_tree and validation_mroots.
   auto start = next_bhs.core.last_final_block_num() - core.last_final_block_num();
   valid_t next_valid {
      .validation_tree = valid->validation_tree,
      // Trim roots from the front end, up to block number `next_bhs.core.last_final_block_num()`
      .validation_mroots = { valid->validation_mroots.cbegin() + start, valid->validation_mroots.cend() }
   };

   // construct block's finality leaf node.
   valid_t::finality_leaf_node_t leaf_node{
      .block_num       = next_bhs.block_num(),
      .finality_digest = strong_digest,
      .action_mroot    = action_mroot
   };
   auto leaf_node_digest = fc::sha256::hash(leaf_node);

   // append new finality leaf node digest to validation_tree
   next_valid.validation_tree.append(leaf_node_digest);

   // append the root of the new Validation Tree to validation_mroots.
   next_valid.validation_mroots.emplace_back(next_valid.validation_tree.get_root());

   // post condition of validation_mroots
   assert(next_valid.validation_mroots.size() == (next_bhs.block_num() - next_bhs.core.last_final_block_num() + 1));

   return next_valid;
}

digest_type block_state::get_validation_mroot(block_num_type target_block_num) const {
   if (!valid) {
      return digest_type{};
   }

   assert(valid->validation_mroots.size() > 0);
   assert(core.last_final_block_num() <= target_block_num &&
          target_block_num < core.last_final_block_num() + valid->validation_mroots.size());
   assert(target_block_num - core.last_final_block_num() < valid->validation_mroots.size());

   return valid->validation_mroots[target_block_num - core.last_final_block_num()];
}

digest_type block_state::get_finality_mroot_claim(const qc_claim_t& qc_claim) const {
   auto next_core_metadata = core.next_metadata(qc_claim);

   // For proper IF blocks that do not have an associated Finality Tree defined
   if (core.is_genesis_block_num(next_core_metadata.final_on_strong_qc_block_num)) {
      return digest_type{};
   }

   return get_validation_mroot(next_core_metadata.final_on_strong_qc_block_num);
}

finality_data_t block_state::get_finality_data() {
   if (!base_digest) {
      base_digest = compute_base_digest(); // cache it
   }
   return {
      // other fields take the default values set by finality_data_t definition
      .active_finalizer_policy_generation = active_finalizer_policy->generation,
      .action_mroot = action_mroot,
      .base_digest  = *base_digest
   };
}

void inject_additional_signatures( signed_block& b, const std::vector<signature_type>& additional_signatures)
{
   if (!additional_signatures.empty()) {
      // as an optimization we don't copy this out into the legitimate extension structure as it serializes
      // the same way as the vector of signatures
      static_assert(fc::reflector<additional_block_signatures_extension>::total_member_count == 1);
      static_assert(std::is_same_v<decltype(additional_block_signatures_extension::signatures), std::vector<signature_type>>);

      emplace_extension(b.block_extensions, additional_block_signatures_extension::extension_id(), fc::raw::pack( additional_signatures ));
   }
}

void block_state::sign(const signer_callback_type& signer, const block_signing_authority& valid_block_signing_authority ) {
   auto sigs = signer( block_id );

   EOS_ASSERT(!sigs.empty(), no_block_signatures, "Signer returned no signatures");
   block->producer_signature = sigs.back(); // last is producer signature, rest are additional signatures to inject in the block extension
   sigs.pop_back();

   verify_signee(sigs, valid_block_signing_authority);
   inject_additional_signatures(*block, sigs);
}

void block_state::verify_signee(const std::vector<signature_type>& additional_signatures, const block_signing_authority& valid_block_signing_authority) const {
   auto num_keys_in_authority = std::visit([](const auto &a){ return a.keys.size(); }, valid_block_signing_authority);
   EOS_ASSERT(1 + additional_signatures.size() <= num_keys_in_authority, wrong_signing_key,
              "number of block signatures (${num_block_signatures}) exceeds number of keys (${num_keys}) in block signing authority: ${authority}",
              ("num_block_signatures", 1 + additional_signatures.size())
              ("num_keys", num_keys_in_authority)
              ("authority", valid_block_signing_authority)
   );

   std::set<public_key_type> keys;
   keys.emplace(fc::crypto::public_key( block->producer_signature, block_id, true ));

   for (const auto& s: additional_signatures) {
      auto res = keys.emplace(s, block_id, true);
      EOS_ASSERT(res.second, wrong_signing_key, "block signed by same key twice: ${key}", ("key", *res.first));
   }

   bool is_satisfied = false;
   size_t relevant_sig_count = 0;

   std::tie(is_satisfied, relevant_sig_count) = producer_authority::keys_satisfy_and_relevant(keys, valid_block_signing_authority);

   EOS_ASSERT(relevant_sig_count == keys.size(), wrong_signing_key,
              "block signed by unexpected key: ${signing_keys}, expected: ${authority}. ${c} != ${s}",
              ("signing_keys", keys)("authority", valid_block_signing_authority)("c", relevant_sig_count)("s", keys.size()));

   EOS_ASSERT(is_satisfied, wrong_signing_key,
              "block signatures ${signing_keys} do not satisfy the block signing authority: ${authority}",
              ("signing_keys", keys)("authority", valid_block_signing_authority));
}

} /// eosio::chain

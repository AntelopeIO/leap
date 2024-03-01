#include <eosio/chain/block_state.hpp>
#include <eosio/chain/block_header_state_utils.hpp>
#include <eosio/chain/block_state_legacy.hpp>
#include <eosio/chain/hotstuff/finalizer.hpp>
#include <eosio/chain/exceptions.hpp>

#include <fc/crypto/bls_utils.hpp>

namespace eosio::chain {

block_state::block_state(const block_header_state& prev, signed_block_ptr b, const protocol_feature_set& pfs,
                         const validator_t& validator, bool skip_validate_signee)
   : block_header_state(prev.next(*b, validator))
   , block(std::move(b))
   , strong_digest(compute_finalizer_digest())
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

block_state::block_state(const block_header_state& bhs, deque<transaction_metadata_ptr>&& trx_metas,
                         deque<transaction_receipt>&& trx_receipts, const std::optional<quorum_certificate>& qc,
                         const signer_callback_type& signer, const block_signing_authority& valid_block_signing_authority)
   : block_header_state(bhs)
   , block(std::make_shared<signed_block>(signed_block_header{bhs.header}))
   , strong_digest(compute_finalizer_digest())
   , weak_digest(create_weak_digest(strong_digest))
   , pending_qc(bhs.active_finalizer_policy->finalizers.size(), bhs.active_finalizer_policy->threshold, bhs.active_finalizer_policy->max_weak_sum_before_weak_final())
   , pub_keys_recovered(true) // called by produce_block so signature recovery of trxs must have been done
   , cached_trxs(std::move(trx_metas))
{
   block->transactions = std::move(trx_receipts);

   if( qc ) {
      emplace_extension(block->block_extensions, quorum_certificate_extension::extension_id(), fc::raw::pack( *qc ));
   }

   sign(signer, valid_block_signing_authority);
}

// Used for transition from dpos to instant-finality
block_state::block_state(const block_state_legacy& bsp) {
   block_header_state::block_id = bsp.id();
   header = bsp.header;
   core = finality_core::create_core_for_genesis_block(bsp.block_num()); // [if todo] instant transition is not acceptable
   activated_protocol_features = bsp.activated_protocol_features;

   auto if_ext_id = instant_finality_extension::extension_id();
   std::optional<block_header_extension> ext = bsp.block->extract_header_extension(if_ext_id);
   assert(ext); // required by current transition mechanism
   const auto& if_extension = std::get<instant_finality_extension>(*ext);
   assert(if_extension.new_finalizer_policy); // required by current transition mechanism
   active_finalizer_policy = std::make_shared<finalizer_policy>(*if_extension.new_finalizer_policy);
   // TODO: https://github.com/AntelopeIO/leap/issues/2057
   // TODO: Do not aggregate votes on blocks created from block_state_legacy. This can be removed when #2057 complete.
   pending_qc = pending_quorum_certificate{active_finalizer_policy->finalizers.size(), active_finalizer_policy->threshold, active_finalizer_policy->max_weak_sum_before_weak_final()};
   active_proposer_policy = std::make_shared<proposer_policy>();
   active_proposer_policy->active_time = bsp.timestamp();
   active_proposer_policy->proposer_schedule = bsp.active_schedule;
   header_exts = bsp.header_exts;
   block = bsp.block;
   validated = bsp.is_valid();
   pub_keys_recovered = bsp._pub_keys_recovered;
   cached_trxs = bsp._cached_trxs;
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

   std::vector<bls_public_key> pubkeys;
   std::vector<std::vector<uint8_t>> digests;

   // utility to aggregate public keys for verification
   auto aggregate_pubkeys = [&](const auto& votes_bitset) -> bls_public_key {
      const auto n = std::min(num_finalizers, votes_bitset.size());
      std::vector<bls_public_key> pubkeys_to_aggregate;
      pubkeys_to_aggregate.reserve(n);
      for(auto i = 0u; i < n; ++i) {
         if (votes_bitset[i]) { // ith finalizer voted
            pubkeys_to_aggregate.emplace_back(finalizers[i].public_key);
         }
      }

      return fc::crypto::blslib::aggregate(pubkeys_to_aggregate);
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
   EOS_ASSERT( fc::crypto::blslib::aggregate_verify( pubkeys, digests, qc._sig ),
               invalid_qc_claim, "signature validation failed" );
}

std::optional<quorum_certificate> block_state::get_best_qc() const {
   // if pending_qc does not have a valid QC, consider valid_qc only
   if( !pending_qc.is_quorum_met() ) {
      if( valid_qc ) {
         return quorum_certificate{ block_num(), *valid_qc };
      } else {
         return std::nullopt;
      }
   }

   // extract valid QC from pending_qc
   valid_quorum_certificate valid_qc_from_pending = pending_qc.to_valid_quorum_certificate();

   // if valid_qc does not have value, consider valid_qc_from_pending only
   if( !valid_qc ) {
      return quorum_certificate{ block_num(), valid_qc_from_pending };
   }

   // Both valid_qc and valid_qc_from_pending have value. Compare them and select a better one.
   // Strong beats weak. Tie break by valid_qc.
   const auto& best_qc =
      valid_qc->is_strong() == valid_qc_from_pending.is_strong() ?
      *valid_qc : // tie broke by valid_qc
      valid_qc->is_strong() ? *valid_qc : valid_qc_from_pending; // strong beats weak
   return quorum_certificate{ block_num(), best_qc };
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

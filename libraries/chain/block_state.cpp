#include <eosio/chain/block_state.hpp>
#include <eosio/chain/block_header_state_utils.hpp>
#include <eosio/chain/block_state_legacy.hpp>
#include <eosio/chain/exceptions.hpp>

#include <fc/crypto/bls_utils.hpp>

namespace eosio::chain {

block_state::block_state(const block_header_state& prev, signed_block_ptr b, const protocol_feature_set& pfs,
                         const validator_t& validator, bool skip_validate_signee)
   : block_header_state(prev.next(*b, pfs, validator))
   , block(std::move(b))
   , strong_digest(compute_finalizer_digest())
   , weak_digest(compute_finalizer_digest())
   , pending_qc(prev.active_finalizer_policy->finalizers.size(), prev.active_finalizer_policy->threshold)
{}

block_state::block_state(const block_header_state& bhs, deque<transaction_metadata_ptr>&& trx_metas,
                         deque<transaction_receipt>&& trx_receipts, const std::optional<quorum_certificate>& qc)
   : block_header_state(bhs)
   , block(std::make_shared<signed_block>(signed_block_header{bhs.header})) // [greg todo] do we need signatures?
   , strong_digest(compute_finalizer_digest())
   , weak_digest(compute_finalizer_digest())
   , pending_qc(bhs.active_finalizer_policy->finalizers.size(), bhs.active_finalizer_policy->threshold)
   , pub_keys_recovered(true) // probably not needed
   , cached_trxs(std::move(trx_metas))
{
   block->transactions = std::move(trx_receipts);

   if( qc ) {
      emplace_extension(block->block_extensions, quorum_certificate_extension::extension_id(), fc::raw::pack( *qc ));
   }
}

// Used for transition from dpos to instant-finality
block_state::block_state(const block_state_legacy& bsp) {
   block_header_state::id = bsp.id();
   header = bsp.header;
   core.last_final_block_num = bsp.block_num(); // [if todo] instant transition is not acceptable
   activated_protocol_features = bsp.activated_protocol_features;

   auto if_ext_id = instant_finality_extension::extension_id();
   std::optional<block_header_extension> ext = bsp.block->extract_header_extension(if_ext_id);
   assert(ext); // required by current transition mechanism
   const auto& if_extension = std::get<instant_finality_extension>(*ext);
   assert(if_extension.new_finalizer_policy); // required by current transition mechanism
   active_finalizer_policy = std::make_shared<finalizer_policy>(*if_extension.new_finalizer_policy);
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
std::pair<bool, std::optional<uint32_t>> block_state::aggregate_vote(const vote_message& vote) {
   const auto& finalizers = active_finalizer_policy->finalizers;
   auto it = std::find_if(finalizers.begin(),
                          finalizers.end(),
                          [&](const auto& finalizer) { return finalizer.public_key == vote.finalizer_key; });

   if (it != finalizers.end()) {
      auto index = std::distance(finalizers.begin(), it);
      const digest_type& digest = vote.strong ? strong_digest : weak_digest;
      auto [valid, strong] = pending_qc.add_vote(vote.strong,
#warning TODO change to use std::span if possible
                                 std::vector<uint8_t>{digest.data(), digest.data() + digest.data_size()},
                                 index,
                                 vote.finalizer_key,
                                 vote.sig);
      return {valid, strong ? core.final_on_strong_qc_block_num : std::optional<uint32_t>{}};
   } else {
      wlog( "finalizer_key (${k}) in vote is not in finalizer policy", ("k", vote.finalizer_key) );
      return {};
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
                  block_validate_exception,
                  "strong quorum is not met, strong_weights: ${s}, threshold: ${t}",
                  ("s", strong_weights)("t", active_finalizer_policy->threshold) );
   } else {
      EOS_ASSERT( strong_weights + weak_weights >= active_finalizer_policy->threshold,
                  block_validate_exception,
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
      digests.emplace_back(std::vector<uint8_t>{weak_digest.data(), weak_digest.data() + weak_digest.data_size()});
   }

   // validate aggregated signature
   EOS_ASSERT( fc::crypto::blslib::aggregate_verify( pubkeys, digests, qc._sig ),  block_validate_exception, "signature validation failed" );
}

std::optional<quorum_certificate> block_state::get_best_qc() const {
   auto block_number = block_num();

   // if pending_qc does not have a valid QC, consider valid_qc only
   if( !pending_qc.is_quorum_met() ) {
      if( valid_qc ) {
         return quorum_certificate{ block_number, *valid_qc };
      } else {
         return std::nullopt;;
      }
   }

   // extract valid QC from pending_qc
   valid_quorum_certificate valid_qc_from_pending = pending_qc.to_valid_quorum_certificate();

   // if valid_qc does not have value, consider valid_qc_from_pending only
   if( !valid_qc ) {
      return quorum_certificate{ block_number, valid_qc_from_pending };
   }

   // Both valid_qc and valid_qc_from_pending have value. Compare them and select a better one.
   // Strong beats weak. Tie break by valid_qc.
   const auto& best_qc =
      valid_qc->is_strong() == valid_qc_from_pending.is_strong() ?
      *valid_qc : // tie broke by valid_qc
      valid_qc->is_strong() ? *valid_qc : valid_qc_from_pending; // strong beats weak
   return quorum_certificate{ block_number, best_qc };
}   
} /// eosio::chain

#include <eosio/chain/block_state.hpp>
#include <eosio/chain/block_header_state_utils.hpp>
#include <eosio/chain/block_state_legacy.hpp>
#include <eosio/chain/exceptions.hpp>

namespace eosio::chain {

block_state::block_state(const block_header_state& prev, signed_block_ptr b, const protocol_feature_set& pfs,
                         const validator_t& validator, bool skip_validate_signee)
   : block_header_state(prev.next(*b, pfs, validator))
   , block(std::move(b))
{}

block_state::block_state(const block_header_state& bhs, deque<transaction_metadata_ptr>&& trx_metas,
                         deque<transaction_receipt>&& trx_receipts)
   : block_header_state(bhs)
   , block(std::make_shared<signed_block>(signed_block_header{bhs.header})) // [greg todo] do we need signatures?
   , pub_keys_recovered(true) // probably not needed
   , cached_trxs(std::move(trx_metas))
{
   block->transactions = std::move(trx_receipts);
}

// Used for transition from dpos to instant-finality
block_state::block_state(const block_state_legacy& bsp) {
   block_header_state::id = bsp.id();
   header = bsp.header;
   activated_protocol_features = bsp.activated_protocol_features;
   std::optional<block_header_extension> ext = bsp.block->extract_header_extension(instant_finality_extension::extension_id());
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

   
} /// eosio::chain

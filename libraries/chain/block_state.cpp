#include <eosio/chain/block_state.hpp>
#include <eosio/chain/block_header_state_utils.hpp>
#include <eosio/chain/exceptions.hpp>

namespace eosio::chain {

block_state::block_state(const block_header_state& prev, signed_block_ptr b, const protocol_feature_set& pfs,
                         const validator_t& validator, bool skip_validate_signee)
   : block_header_state(prev.next(*b, pfs, validator))
   , block(std::move(b))
{}

#if 0
block_state::block_state(pending_block_header_state&& cur,
                         signed_block_ptr&& b,
                         deque<transaction_metadata_ptr>&& trx_metas,
                         const protocol_feature_set& pfs,
                         const validator_t& validator,
                         const signer_callback_type& signer
   )
   :block_header_state( inject_additional_signatures( std::move(cur), *b, pfs, validator, signer ) )
   ,block( std::move(b) )
   ,_pub_keys_recovered( true ) // called by produce_block so signature recovery of trxs must have been done
   ,cached_trxs( std::move(trx_metas) )
{}
#endif

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

#include <eosio/chain_plugin/trx_finality_status_processing.hpp>


using namespace eosio;

namespace eosio::chain_apis {

   trx_finality_status_processing::trx_finality_status_processing( uint64_t max_storage )
   {
   }

   void trx_finality_status_processing::signal_applied_transactions( const chain::signals_processor::trx_deque& trxs, const chain::block_state_ptr& bsp ) {
      const fc::time_point now = fc::time_point::now();
      const fc::time_point status_expiry = now + _success_duration;
      const std::optional<uint32_t> no_block_num;
      // use the head block num if we are in a block, otherwise don't provide block number for speculative blocks
      const auto& optional_block_num = bsp ? _head_block_num : no_block_num;
      if (bsp && *_head_block_num <= *_last_proc_block_num) {
         handle_rollback();
      }

      for (const auto& trx_pair : trxs) {
         const auto& trx_id = std::get<0>(trxs)->id;
         auto iter = _storage.find(trx_id);
         if (iter != _storage.cend()) {
            _storage.modify( iter, [&optional_block_num]( const finality_status_object& obj ) {
               obj.block_num = optional_block_num;
            } );
         }
         else {
            _storage.insert(finality_status_object{.trx_id = trx_id, .trx_expiry = std::get<1>(trxs)->expiration(), .status_expiry = status_expiry, .block_num = optional_block_num});
         }
      }

      status_expiry(now, bsp->header.timestamp);

      if (bsp) {
         _last_proc_block_num = _head_block_num;
      }
   }

   void trx_finality_status_processing::signal_irreversible_block( const chain::block_state_ptr& bsp ) {
      _irr_block_num = bsp->block->block_num();
      _irr_timestamp = bsp->block->timestamp.to_time_point();
   }

   void trx_finality_status_processing::signal_block_start( uint32_t block_num ) {
      _head_block_num = block_num;
   }

   void trx_finality_status_processing::handle_rollback() {
      const auto& indx = _storage._index.get<by_block_num>();
      std::deque<chain::transaction_id_type> trxs;
      auto iter = indx.lower_bound(*_head_block_num);
      trxs.insert(trxs.cend(), iter, indx.cend());
      for (const auto& trx : trxs) {
         _storage.modify( iter, [&optional_block_num]( finality_status_object& obj ) {
            obj.block_num = std::optional<uint32_t>{};
            obj.forked_out = true;
         } );
      }
   }

   void trx_finality_status_processing::status_expiry(const fc::time_point& now, const fc::time_point& head) {
   }

}

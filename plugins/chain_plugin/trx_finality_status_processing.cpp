#include <eosio/chain_plugin/trx_finality_status_processing.hpp>
#include <eosio/chain_plugin/finality_status_object.hpp>


using namespace eosio;
using namespace eosio::finality_status;

namespace eosio::chain_apis {

   struct trx_finality_status_processing_impl {
      trx_finality_status_processing_impl( uint64_t max_storage, const fc::microseconds& success_duration, const fc::microseconds& failure_duration )
      : _max_storage(max_storage),
        _success_duration(success_duration),
        _failure_duration(failure_duration) {}

      void signal_applied_transaction( const chain::transaction_trace_ptr& trace, const chain::packed_transaction_ptr& ptrx );

      void signal_accepted_block( const chain::signed_block_ptr& block, const chain::block_id_type& id );

      void handle_rollback();

      bool status_expiry_of_trxs(const fc::time_point& now);

      // free up 10% of memory if _max_storage is exceeded
      // returns true if storage was freed
      bool ensure_storage();

      void determine_earliest_tracked_block_id();

      const uint64_t                                   _max_storage;
      fc::tracked_storage<finality_status_multi_index> _storage;
      uint32_t                                         _last_proc_block_num = finality_status::no_block_num;
      chain::block_id_type                             _head_block_id;
      chain::block_timestamp_type                      _head_block_timestamp;
      chain::block_id_type                             _irr_block_id;
      chain::block_timestamp_type                      _irr_block_timestamp;
      chain::block_id_type                             _earliest_tracked_block_id;
      const fc::microseconds                           _success_duration;
      const fc::microseconds                           _failure_duration;
      std::deque<chain::transaction_id_type>           _speculative_trxs;
   };

   trx_finality_status_processing::trx_finality_status_processing( uint64_t max_storage, const fc::microseconds& success_duration, const fc::microseconds& failure_duration )
   : _my(new trx_finality_status_processing_impl(max_storage, success_duration, failure_duration))
   {
   }

   trx_finality_status_processing::~trx_finality_status_processing() = default;

   void trx_finality_status_processing::signal_irreversible_block( const chain::signed_block_ptr& block, const chain::block_id_type& id ) {
      try {
         _my->_irr_block_id = id;
         _my->_irr_block_timestamp = block->timestamp;
      } FC_LOG_AND_DROP(("Failed to signal irreversible block for finality status"));
   }

   void trx_finality_status_processing::signal_block_start( uint32_t block_num ) {
      try {
         // since a new block is started, no block state was received, so the speculative block did not get eventually produced
         _my->_speculative_trxs.clear();
      } FC_LOG_AND_DROP(("Failed to signal block start for finality status"));
   }

   void trx_finality_status_processing::signal_applied_transaction( const chain::transaction_trace_ptr& trace, const chain::packed_transaction_ptr& ptrx ) {
      try {
         _my->signal_applied_transaction(trace, ptrx);
      } FC_LOG_AND_DROP(("Failed to signal applied transaction for finality status"));
   }

   void trx_finality_status_processing::signal_accepted_block( const chain::signed_block_ptr& block, const chain::block_id_type& id ) {
      try {
         _my->signal_accepted_block(block, id);
      } FC_LOG_AND_DROP(("Failed to signal accepted block for finality status"));
   }

   void trx_finality_status_processing_impl::signal_applied_transaction( const chain::transaction_trace_ptr& trace, const chain::packed_transaction_ptr& ptrx ) {
      const fc::time_point now = fc::time_point::now();
      // use the head block num if we are in a block, otherwise don't provide block number for speculative blocks
      chain::block_id_type block_id;
      chain::block_timestamp_type block_timestamp;
      bool modified = false;
      if (trace->producer_block_id) {
         block_id = *trace->producer_block_id;
         const bool block_changed = block_id != _head_block_id;
         if (block_changed) {
            _head_block_id = block_id;
            _head_block_timestamp = trace->block_time;
         }
         block_timestamp = _head_block_timestamp;

         const auto head_block_num = chain::block_header::num_from_id(_head_block_id);
         if (block_changed && head_block_num <= _last_proc_block_num) {
            handle_rollback();
            modified = true;
         }

         _last_proc_block_num = head_block_num;

         if (status_expiry_of_trxs(now)) {
            modified = true;
         }
      }

      if (!trace->receipt) return;
      if (trace->receipt->status != chain::transaction_receipt_header::executed) {
         return;
      }
      if (trace->scheduled) return;
      if (chain::is_onblock(*trace)) return;

      if (!trace->producer_block_id) {
         _speculative_trxs.push_back(trace->id);
      }

      if(ensure_storage()) {
         modified = true;
      }

      const auto& trx_id = trace->id;
      auto iter = _storage.find(trx_id);
      if (iter != _storage.index().cend()) {
         _storage.modify( iter, [&block_id,&block_timestamp]( finality_status_object& obj ) {
            obj.block_id = block_id;
            obj.block_timestamp = block_timestamp;
            obj.forked_out = false;
         } );
      }
      else {
         _storage.insert(
            finality_status_object{.trx_id = trx_id,
                                   .trx_expiry = ptrx->expiration().to_time_point(),
                                   .received = now,
                                   .block_id = block_id,
                                   .block_timestamp = block_timestamp});
      }

      if (modified || _earliest_tracked_block_id == chain::block_id_type{}) {
         determine_earliest_tracked_block_id();
      }
   }

   void trx_finality_status_processing_impl::signal_accepted_block( const chain::signed_block_ptr& block, const chain::block_id_type& id ) {
      // if this block had any transactions, then we have processed everything we need to already
      if (id == _head_block_id) {
         return;
      }

      _head_block_id = id;
      _head_block_timestamp = block->timestamp;

      const auto head_block_num = chain::block_header::num_from_id(_head_block_id);
      if (head_block_num <= _last_proc_block_num) {
         handle_rollback();
      }

      const fc::time_point now = fc::time_point::now();
      bool status_expiry = status_expiry_of_trxs(now);
      if (status_expiry) {
         determine_earliest_tracked_block_id();
      }

      // if this approve block was preceded by speculative transactions then we produced the block, update trx state.
      auto mod = [&block_id=_head_block_id,&block_timestamp=_head_block_timestamp]( finality_status_object& obj ) {
         obj.block_id = block_id;
         obj.block_timestamp = block_timestamp;
         obj.forked_out = false;
      };
      for (const auto& trx_id : _speculative_trxs) {
         auto iter = _storage.find(trx_id);
         FC_ASSERT( iter != _storage.index().cend(),
                    "CODE ERROR: Should not have speculative transactions that have not already"
                    "been identified prior to the block being accepted. trx id: ${trx_id}",
                    ("trx_id", trx_id) );
         _storage.modify( iter, mod );
      }
      _speculative_trxs.clear();

      _last_proc_block_num = head_block_num;
   }


   void trx_finality_status_processing_impl::handle_rollback() {
      const auto& indx = _storage.index().get<by_block_num>();
      chain::deque<decltype(_storage.index().project<0>(indx.begin()))> trxs;
      for (auto iter = indx.lower_bound(chain::block_header::num_from_id(_head_block_id)); iter != indx.end(); ++iter) {
         trxs.push_back(_storage.index().project<0>(iter));
      }
      for (const auto& trx_iter : trxs) {
         _storage.modify( trx_iter, []( finality_status_object& obj ) {
            obj.forked_out = true;
         } );
      }
   }

   bool trx_finality_status_processing_impl::status_expiry_of_trxs(const fc::time_point& now) {
      const auto& indx = _storage.index().get<by_status_expiry>();
      chain::deque<decltype(_storage.index().project<0>(indx.begin()))> remove_trxs;

      // find the successful (in any block) transactions that are past the failure expiry times
      auto success_iter = indx.lower_bound(boost::make_tuple(true, fc::time_point{}));

      const fc::time_point success_expiry = now - _success_duration;
      const auto success_end = indx.upper_bound(boost::make_tuple(true, success_expiry));
      for (; success_iter != success_end; ++success_iter) {
         remove_trxs.push_back(_storage.index().project<0>(success_iter));
      }

      const fc::time_point fail_expiry = now - _failure_duration;
      const auto fail_end = indx.upper_bound(boost::make_tuple(false, fail_expiry));
      // find the failure (not in a block) transactions that are past the failure expiry time
      for (auto fail_iter = indx.begin(); fail_iter != fail_end; ++fail_iter) {
         remove_trxs.push_back(_storage.index().project<0>(fail_iter));
      }

      for (const auto& trx_iter : remove_trxs) {
         _storage.erase(trx_iter);
      }
      return !remove_trxs.empty();
   }

   bool trx_finality_status_processing_impl::ensure_storage() {
      const int64_t remaining_storage = _max_storage - _storage.memory_size();
      if (remaining_storage > 0) {
         return false;
      }

      auto percentage = [](uint64_t mem) {
         const uint64_t pcnt = 90;
         return (mem * pcnt)/100;
      };
      // determine how much we need to free to get back to at least the desired percentage of the storage
      int64_t storage_to_free = _max_storage - percentage(_max_storage) - remaining_storage;
      ilog("Finality Status exceeded max storage (${max_storage}GB) need to free up ${storage_to_free} GB",
           ("max_storage",_max_storage/1024/1024/1024)
           ("storage_to_free",storage_to_free/1024/1024/1024));
      const auto& block_indx = _storage.index().get<by_block_num>();
      const auto& status_expiry_indx = _storage.index().get<by_status_expiry>();
      using index_iter_type = decltype(_storage.index().project<0>(block_indx.begin()));
      chain::deque<index_iter_type> remove_trxs;

      auto reduce_storage = [&storage_to_free,&remove_trxs,&storage=this->_storage](auto iter) {
         storage_to_free -= iter->memory_size();
         remove_trxs.push_back(storage.index().project<0>(iter));
      };

      auto block_upper_bound = finality_status::no_block_num;
      // start at the beginning of the oldest_failure section and just keep iterating from there
      auto oldest_failure_iter = status_expiry_indx.begin();
      // the end of the oldest failure section
      const auto oldest_failure_end = status_expiry_indx.lower_bound( std::make_tuple( true, fc::time_point{} ) );
      uint32_t earliest_block = finality_status::no_block_num;
      while (storage_to_free > 0) {
         auto oldest_block_iter = block_indx.upper_bound(block_upper_bound);
         if (oldest_block_iter == block_indx.end()) {
            FC_ASSERT( oldest_failure_iter != oldest_failure_end,
                        "CODE ERROR: can not free more storage, but still exceeding limit. "
                        "Total entries: ${total_entries}, storage memory to free: ${storage}, "
                        "entries slated for removal: ${remove_entries}",
                        ("total_entries", _storage.index().size())
                        ("storage", storage_to_free)
                        ("remove_entries", remove_trxs.size()));
            for (; oldest_failure_iter != oldest_failure_end && storage_to_free > 0; ++oldest_failure_iter) {
               reduce_storage(oldest_failure_iter);
            }
            FC_ASSERT( storage_to_free < 1,
                        "CODE ERROR: can not free more storage, but still exceeding limit. "
                        "Total entries: ${total_entries}, storage memory to free: ${storage}, "
                        "entries slated for removal: ${remove_entries}",
                        ("total_entries", _storage.index().size())
                        ("storage", storage_to_free)
                        ("remove_entries", remove_trxs.size()));
            break;
         }
         else {
            const auto block_num = oldest_block_iter->block_num();
            if (earliest_block == finality_status::no_block_num) {
               earliest_block = block_num;
            }
            block_upper_bound = block_num;
            const auto block_timestamp = oldest_block_iter->block_timestamp;
            for (; oldest_block_iter != block_indx.end() && oldest_block_iter->block_num() == block_num; ++oldest_block_iter) {
               reduce_storage(oldest_block_iter);
            }
            const auto oldest_failure_upper_bound = status_expiry_indx.upper_bound( std::make_tuple( false, block_timestamp.to_time_point() ));
            for (; oldest_failure_iter != oldest_failure_upper_bound; ++oldest_failure_iter) {
               reduce_storage(oldest_failure_iter);
            }
         }
      }

      for (const auto& trx_iter : remove_trxs) {
         _storage.erase(trx_iter);
      }

      if (earliest_block != finality_status::no_block_num) {
         ilog( "Finality Status dropped ${trx_count} transactions, which were removed from block # ${block_num_start} to block # ${block_num_end}",
               ("trx_count", remove_trxs.size())("block_num_start", earliest_block)("block_num_end", block_upper_bound) );
      }
      else {
         ilog( "Finality Status dropped ${trx_count} transactions, all were failed transactions", ("trx_count", remove_trxs.size()) );
      }

      return true;
   }

   trx_finality_status_processing::chain_state trx_finality_status_processing::get_chain_state() const {
      return { .head_id = _my->_head_block_id, .head_block_timestamp = _my->_head_block_timestamp, .irr_id = _my->_irr_block_id, .irr_block_timestamp = _my->_irr_block_timestamp, .earliest_tracked_block_id = _my->_earliest_tracked_block_id };
   }

   std::optional<trx_finality_status_processing::trx_state> trx_finality_status_processing::get_trx_state( const chain::transaction_id_type& id ) const {
      auto iter = _my->_storage.find(id);
      if (iter == _my->_storage.index().cend()) {
         return {};
      }

      const char* status;
      if (!iter->is_in_block()) {
         if (fc::time_point::now() >= iter->trx_expiry) {
            status = "FAILED";
         }
         else {
            status = iter->forked_out ? "FORKED_OUT" : "LOCALLY_APPLIED";
         }
      }
      else {
         const auto block_num = iter->block_num();
         const auto lib = chain::block_header::num_from_id(_my->_irr_block_id);
         status = (block_num > lib) ? "IN_BLOCK" : "IRREVERSIBLE";
      }
      return trx_finality_status_processing::trx_state{ .block_id = iter->block_id, .block_timestamp = iter->block_timestamp, .received = iter->received, .expiration = iter->trx_expiry, .status = status };
   }

   size_t trx_finality_status_processing::get_storage_memory_size() const {
      return _my->_storage.memory_size();
   }

   void trx_finality_status_processing_impl::determine_earliest_tracked_block_id() {
      const auto& indx = _storage.index().get<by_status_expiry>();

      // find the lowest value successful block
      auto success_iter = indx.lower_bound(boost::make_tuple(true, fc::time_point{}));
      if (success_iter != indx.cend()) {
         _earliest_tracked_block_id = success_iter->block_id;
      }
   }
}

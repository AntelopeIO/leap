#include <eosio/chain_plugin/trx_finality_status_processing.hpp>
#include <eosio/chain_plugin/finality_status_object.hpp>


using namespace eosio;
using namespace eosio::finality_status;
using cbh = chain::block_header;

namespace eosio::chain_apis {

   struct trx_finality_status_processing_impl {
      trx_finality_status_processing_impl( uint64_t max_storage, const fc::microseconds& success_duration, const fc::microseconds& failure_duration )
      : _max_storage(max_storage),
        _success_duration(success_duration),
        _failure_duration(failure_duration) {}

      void signal_applied_transactions( const chain::signals_processor::trx_deque& trxs, const chain::block_state_ptr& bsp );

      void handle_rollback();

      bool status_expiry_of_trxs(const fc::time_point& now);

      void ensure_storage(const fc::time_point& now);

      const uint64_t                                   _max_storage;
      fc::tracked_storage<finality_status_multi_index> _storage;
      uint32_t                                         _last_proc_block_num = finality_status::no_block_num;
      chain::block_id_type                             _head_block_id;
      chain::block_id_type                             _irr_block_id;
      chain::block_id_type                             _last_tracked_block_id;
      const fc::microseconds                           _success_duration;
      const fc::microseconds                           _failure_duration;
   };

   trx_finality_status_processing::trx_finality_status_processing( uint64_t max_storage, const fc::microseconds& success_duration, const fc::microseconds& failure_duration )
   : _my(new trx_finality_status_processing_impl(max_storage, success_duration, failure_duration))
   {
   }

   trx_finality_status_processing::~trx_finality_status_processing() = default;

   void trx_finality_status_processing::signal_applied_transactions( const chain::signals_processor::trx_deque& trxs, const chain::block_state_ptr& bsp ) {
      _my->signal_applied_transactions(trxs, bsp);
   }

   void trx_finality_status_processing::signal_irreversible_block( const chain::block_state_ptr& bsp ) {
      _my->_irr_block_id = bsp->id;
   }

   void trx_finality_status_processing::signal_block_start( uint32_t block_num ) {
   }

   void trx_finality_status_processing_impl::signal_applied_transactions( const chain::signals_processor::trx_deque& trxs, const chain::block_state_ptr& bsp ) {
      const fc::time_point now = fc::time_point::now();
      // use the head block num if we are in a block, otherwise don't provide block number for speculative blocks
      const auto block_id = bsp ? bsp->id : chain::block_id_type{};
      bool modified = !trxs.empty();
      chain::block_timestamp_type block_timestamp;
      if (bsp) {
         _head_block_id = block_id;
         block_timestamp = bsp->block->timestamp;
         if (chain::block_header::num_from_id(_head_block_id) <= _last_proc_block_num) {
            handle_rollback();
            modified = true;
         }

         // clean up all status expired transactions, to free up storage
         if (status_expiry_of_trxs(now)) {
            modified = true;
         }
      }

      for (const auto& trx_tuple : trxs) {
         const auto trace = std::get<0>(trx_tuple);
         if (!trace->receipt) continue;
         if (trace->receipt->status != chain::transaction_receipt_header::executed) {
            continue;
         }
         if (trace->scheduled) continue;
         if (chain::is_onblock(*trace)) continue;

         ensure_storage(now);
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
                                      .trx_expiry = std::get<1>(trx_tuple)->expiration(),
                                      .received = now,
                                      .block_id = block_id,
                                      .block_timestamp = block_timestamp});
         }
      }
      if (modified) {
         const auto& indx = _storage.index().get<by_block_num>();
         auto iter = indx.cbegin();
         if (iter != indx.cend()) {
            _last_tracked_block_id = iter->block_id;
         }
      }

      if (bsp) {
         _last_proc_block_num = chain::block_header::num_from_id(_head_block_id);
      }
   }

   void trx_finality_status_processing_impl::handle_rollback() {
      const auto& indx = _storage.index().get<by_block_num>();
      std::deque<chain::transaction_id_type> trxs;
      auto iter = indx.lower_bound(_head_block_id);
      std::transform(iter, indx.end(), std::back_inserter(trxs), []( const finality_status_object& obj ) { return obj.trx_id; });
      for (const auto& trx_id : trxs) {
         auto trx_iter = _storage.find(trx_id);
         _storage.modify( trx_iter, []( finality_status_object& obj ) {
            obj.block_id = chain::block_id_type{};
            obj.forked_out = true;
         } );
      }
   }

   bool trx_finality_status_processing_impl::status_expiry_of_trxs(const fc::time_point& now) {
      const auto& indx = _storage.index().get<by_status_expiry>();
      std::deque<chain::transaction_id_type> remove_trxs;
      const fc::time_point success_expiry = now - _success_duration;

      // just need to have any valid block id, since ordering is based off of empty block id or non-empty
      const auto& a_valid_block_id = _head_block_id;

      // find the successful (in any block) transactions that are past the successful expiry time
      auto success_iter = indx.upper_bound(boost::make_tuple(chain::block_id_type{}, fc::time_point{}));
      const auto fail_end = success_iter;
      const auto success_end = indx.upper_bound(boost::make_tuple(a_valid_block_id, success_expiry));
      auto get_id = []( const finality_status_object& obj ) { return obj.trx_id; };
      std::transform(success_iter, success_end, std::back_inserter(remove_trxs), get_id);

      // find the failure (not in a block) transactions that are past the failure expiry time
      auto fail_iter = indx.begin();
      std::transform(fail_iter, fail_end, std::back_inserter(remove_trxs), get_id);

      for (const auto& trx_id : remove_trxs) {
         _storage.erase(trx_id);
      }
      return !remove_trxs.empty();
   }

   void trx_finality_status_processing_impl::ensure_storage(const fc::time_point& now) {
      const int64_t remaining_storage = _max_storage - _storage.memory_size();
      if (remaining_storage > 0) {
         return;
      }

      auto percentage = [](uint64_t mem) {
         const uint64_t pcnt = 90;
         return (mem * pcnt)/100;
      };
      // determine how much we need to free to get back to at least the desired percentage of the storage
      int64_t storage_to_free = _storage.memory_size() - percentage(_max_storage) - remaining_storage;
      const auto& block_indx = _storage.index().get<by_block_num>();
      const auto& status_expiry_indx = _storage.index().get<by_status_expiry>();
      std::deque<chain::transaction_id_type> remove_trxs;

      auto reduce_storage = [&storage_to_free,&remove_trxs](const finality_status_object& obj) {
         storage_to_free -= obj.memory_size();
         remove_trxs.push_back(obj.trx_id);
      };

      auto block_upper_bound = chain::block_id_type{};
      // the end of the oldest failure section
      const auto oldest_failure_end = status_expiry_indx.upper_bound( std::make_tuple( chain::transaction_id_type{}, fc::time_point::now() ) );
      uint32_t earliest_block = finality_status::no_block_num;
      uint32_t latest_block = finality_status::no_block_num;
      while (storage_to_free > 0) {
         auto oldest_block_iter = block_indx.upper_bound(block_upper_bound);
         if (oldest_block_iter == block_indx.end()) {
            auto oldest_failure_iter = status_expiry_indx.begin();
            EOS_ASSERT( oldest_failure_iter != oldest_failure_end, chain::chain_type_exception,
                        "CODE ERROR: can not free more storage, but still exeeding limit. "
                        "Total entries: ${total_entries}, storage memory to free: ${storage}, "
                        "entries slated for removal: ${remove_entries}",
                        ("total_entries", _storage.index().size())
                        ("storage", storage_to_free)
                        ("remove_entries", remove_trxs.size()));
            for (; oldest_failure_iter != oldest_failure_end; ++oldest_failure_iter) {
               reduce_storage(*oldest_failure_iter);
            }
            EOS_ASSERT( storage_to_free < 1, chain::chain_type_exception,
                        "CODE ERROR: can not free more storage, but still exeeding limit. "
                        "Total entries: ${total_entries}, storage memory to free: ${storage}, "
                        "entries slated for removal: ${remove_entries}",
                        ("total_entries", _storage.index().size())
                        ("storage", storage_to_free)
                        ("remove_entries", remove_trxs.size()));
            break;
         }
         else {
            const auto block_num = cbh::num_from_id(oldest_block_iter->block_id);
            if (earliest_block == finality_status::no_block_num) {
               earliest_block = block_num;
            }
            latest_block = block_num;
            const auto block_timestamp = oldest_block_iter->block_timestamp;
            for (; oldest_block_iter != block_indx.end() && cbh::num_from_id(oldest_block_iter->block_id) == block_num; ++oldest_block_iter) {
               reduce_storage(*oldest_block_iter);
            }
            for (auto oldest_failure_iter = status_expiry_indx.upper_bound( std::make_tuple( chain::transaction_id_type{}, block_timestamp.to_time_point() ) );
                 oldest_failure_iter != oldest_failure_end;
                 ++oldest_failure_iter) {
               reduce_storage(*oldest_failure_iter);
            }
         }
      }

      for (const auto& trx_id : remove_trxs) {
         _storage.erase(trx_id);
      }

      if (earliest_block != finality_status::no_block_num) {
         ilog( "Finality Status dropped ${trx_count} transactions, which were removed from block # ${block_num_start} to block # ${block_num_end}",
               ("trx_count", remove_trxs.size())("block_num_start", earliest_block)("block_num_end", latest_block) );
      }
      else {
         ilog( "Finality Status dropped ${trx_count} transactions, all were failed transactions", ("trx_count", remove_trxs.size()) );
      }
   }

   trx_finality_status_processing::chain_state trx_finality_status_processing::get_chain_state() const {
      return { .head_id = _my->_head_block_id, .irr_id = _my->_irr_block_id, .last_tracked_block_id = _my->_last_tracked_block_id };
   }

   std::optional<trx_finality_status_processing::trx_state> trx_finality_status_processing::get_trx_state( const chain::transaction_id_type& id ) const {
      trx_finality_status_processing::trx_state state;
      auto iter = _my->_storage.find(id);
      if (iter == _my->_storage.index().cend()) {
         return {};
      }

      state.block_id == iter->block_id;
      state.block_timestamp = iter->block_timestamp;
      if (iter->block_id == chain::block_id_type{}) {
         if (fc::time_point::now() >= iter->trx_expiry) {
            state.status = "FAILED";
         }
         else if (iter->forked_out) {
            state.status = "FORKED OUT";
         }
         else {
            state.status = "LOCALLY APPLIED";
         }
      }
      else {
         const auto block_num = cbh::num_from_id(iter->block_id);
         const auto lib = cbh::num_from_id(_my->_irr_block_id);
         if (block_num > lib) {
            state.status = "IN BLOCK";
         }
         else {
            state.status = "IRREVERSIBLE";
         }
      }
      return state;
   }
}

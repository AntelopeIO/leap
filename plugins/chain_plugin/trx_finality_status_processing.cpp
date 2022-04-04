#include <eosio/chain_plugin/trx_finality_status_processing.hpp>
#include <eosio/chain_plugin/finality_status_object.hpp>


using namespace eosio;
using namespace eosio::finality_status;

namespace eosio::chain_apis {

   struct trx_finality_status_processing_impl {
      trx_finality_status_processing_impl( uint64_t max_storage )
      : _max_storage(max_storage) {}

      void signal_applied_transactions( const chain::signals_processor::trx_deque& trxs, const chain::block_state_ptr& bsp );

      void handle_rollback();

      void status_expiry_of_trxs(const fc::time_point& now);

      void ensure_storage(const fc::time_point& now);

      const uint64_t                                   _max_storage;
      fc::tracked_storage<finality_status_multi_index> _storage;
      uint32_t                                         _last_proc_block_num = finality_status::no_block_num;
      uint32_t                                         _head_block_num = finality_status::no_block_num;
      std::optional<fc::time_point>                    _head_timestamp;
      uint32_t                                         _irr_block_num = finality_status::no_block_num;
      std::optional<fc::time_point>                    _irr_timestamp;
      const fc::microseconds                           _success_duration;
      const fc::microseconds                           _failure_duration;
   };

   trx_finality_status_processing::trx_finality_status_processing( uint64_t max_storage )
   : _my(new trx_finality_status_processing_impl(max_storage))
   {
   }

   trx_finality_status_processing::~trx_finality_status_processing() {
   }

   void trx_finality_status_processing::signal_applied_transactions( const chain::signals_processor::trx_deque& trxs, const chain::block_state_ptr& bsp ) {
      _my->signal_applied_transactions(trxs, bsp);
   }

   void trx_finality_status_processing::signal_irreversible_block( const chain::block_state_ptr& bsp ) {
      _my->_irr_block_num = bsp->block->block_num();
      _my->_irr_timestamp = bsp->block->timestamp.to_time_point();
   }

   void trx_finality_status_processing::signal_block_start( uint32_t block_num ) {
      _my->_head_block_num = block_num;
   }

   void trx_finality_status_processing_impl::signal_applied_transactions( const chain::signals_processor::trx_deque& trxs, const chain::block_state_ptr& bsp ) {
      const fc::time_point now = fc::time_point::now();
      // use the head block num if we are in a block, otherwise don't provide block number for speculative blocks
      const auto block_num = bsp ? _head_block_num : finality_status::no_block_num;
      if (bsp) {
         if (_head_block_num <= _last_proc_block_num) {
            handle_rollback();
         }

         // clean up all status expired transactions, to free up storage
         status_expiry_of_trxs(now);
      }

      for (const auto& trx_tuple : trxs) {
         ensure_storage(now);
         const auto& trx_id = std::get<0>(trx_tuple)->id;
         auto iter = _storage.find(trx_id);
         if (iter != _storage.index().cend()) {
            _storage.modify( iter, [&block_num]( finality_status_object& obj ) {
               obj.block_num = block_num;
            } );
         }
         else {
            _storage.insert(finality_status_object{.trx_id = trx_id, .trx_expiry = std::get<1>(trx_tuple)->expiration(), .received = now, .block_num = block_num});
         }
      }

      if (bsp) {
         _last_proc_block_num = _head_block_num;
      }
   }

   void trx_finality_status_processing_impl::handle_rollback() {
      const auto& indx = _storage.index().get<by_block_num>();
      std::deque<chain::transaction_id_type> trxs;
      auto iter = indx.lower_bound(_head_block_num);
      std::transform(iter, indx.end(), trxs.end(), []( const finality_status_object& obj ) { return obj.trx_id; });
      for (const auto& trx_id : trxs) {
         auto trx_iter = _storage.find(trx_id);
         _storage.modify( trx_iter, []( finality_status_object& obj ) {
            obj.block_num = finality_status::no_block_num;
            obj.forked_out = true;
         } );
      }
   }

   void trx_finality_status_processing_impl::status_expiry_of_trxs(const fc::time_point& now) {
#warning TODO change by_success_status_expiry to by_status_expiry and remove by_fail_status_expiry
      const auto& indx = _storage.index().get<by_success_status_expiry>();
      std::deque<chain::transaction_id_type> remove_trxs;
      const fc::time_point success_expiry = now - _success_duration;
      const uint32_t first_block = 1;

      // find the successful (in a block) transactions that are past the successful expiry time
      auto iter = indx.lower_bound(boost::make_tuple(first_block, success_expiry));
      auto get_id = []( const finality_status_object& obj ) { return obj.trx_id; };
      std::transform(iter, indx.end(), remove_trxs.end(), get_id);

      // find the failure (not in a block) transactions that are past the failure expiry time
      const fc::time_point failure_expiry = now - _failure_duration;
      // need to verify not in a block since the index will transition from old failed transactions to newer successful transactions
      for (iter = indx.lower_bound(boost::make_tuple(no_block_num, failure_expiry)); iter != indx.end() && iter->block_num == no_block_num; ++iter) {
         remove_trxs.push_back(iter->trx_id);
      }

      for (const auto& trx_id : remove_trxs) {
         _storage.erase(trx_id);
      }
   }

   void trx_finality_status_processing_impl::ensure_storage(const fc::time_point& now) {
#warning TODO logging
      const int64_t remaining_storage = _storage.memory_size() - _max_storage;
      if (remaining_storage > 0) {
         return;
      }

      auto percentage = [](uint64_t mem) {
         const uint64_t pcnt = 90;
         return (mem * pcnt)/100;
      };
      // determine how much we need to free to get back to at least the desired percentage of the storage
      int64_t storage_to_free = _storage.memory_size() - percentage(_max_storage) - remaining_storage;
      const auto& indx = _storage.index().get<by_success_status_expiry>();
      std::deque<chain::transaction_id_type> remove_trxs;

      const auto failure_rev_end = indx.rend();

      auto success_rev_iter = indx.rbegin();
      // find the start (or reversed last) of the valid block number range
      auto failure_rev_iter = boost::make_reverse_iterator(indx.upper_bound(boost::make_tuple(no_block_num, fc::time_point{})));
      // rev iterate to make it the end (instead of last)
      if (failure_rev_iter != failure_rev_end) {
         ++failure_rev_iter;
      }

      const auto success_rev_end = failure_rev_iter;

      const double now_double = now.time_since_epoch().count();
      auto percent_of_time = [now_double](const fc::time_point& time, const  fc::microseconds& duration) {
         return (now_double - time.time_since_epoch().count())/duration.count();
      };
      const double ignore = 1.01; // ignored because it is greater than 100%
      uint32_t earliest_block = finality_status::no_block_num;
      while (storage_to_free > 0 && (success_rev_iter != success_rev_end || failure_rev_iter != failure_rev_end)) {
         const auto success = success_rev_iter != success_rev_end ? percent_of_time(success_rev_iter->received, _success_duration) : ignore;
         const auto failure = failure_rev_iter != failure_rev_end ? percent_of_time(failure_rev_iter->received, _failure_duration) : ignore;
         if (success < failure) {
            storage_to_free -= success_rev_iter->size();
            if (earliest_block == finality_status::no_block_num || success_rev_iter->block_num < earliest_block ) {
               earliest_block = success_rev_iter->block_num;
            }
            remove_trxs.push_back(success_rev_iter++->trx_id);
         }
         else {
            storage_to_free -= failure_rev_iter->size();
            remove_trxs.push_back(failure_rev_iter++->trx_id);
         }
      }

      if (earliest_block != finality_status::no_block_num) {
         ilog( "Finality Status dropped ${trx_count} transactions, earliest block is ${block_num}", ("trx_count", remove_trxs.size())("block_num", earliest_block) );
      }
      else {
         ilog( "Finality Status dropped ${trx_count} transactions, all were failed transactions", ("trx_count", remove_trxs.size()) );
      }

      for (const auto& trx_id : remove_trxs) {
         _storage.erase(trx_id);
      }
   }
}

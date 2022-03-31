#pragma once
#include <eosio/chain/types.hpp>
#include <eosio/chain/block_state.hpp>
#include <eosio/chain/trace.hpp>
#include <eosio/chain_plugin/finality_status_object.hpp>
#include <memory>

namespace eosio::chain_apis {
   /**
    * This class manages the processing related to the transaction finality status feature.
    */
   class trx_finality_status_processing {
   public:

      /**
       * Instantiate a new transaction retry processor
       * @param max_storage - the maximum storage allotted to this feature
       */
      trx_finality_status_processing( uint64_t max_storage );

      void signal_applied_transactions( const chain::signals_processor::trx_deque& trxs, const chain::block_state_ptr& bsp );

      void signal_irreversible_block( const chain::block_state_ptr& bsp );

      void signal_block_start( uint32_t block_num );

   private:
      void handle_rollback();

      void status_expiry(const fc::time_point& now, const fc::time_point& head);

      tracked_storage<finality_status_multi_index> _storage;
      std::optional<uint32_t>                      _last_proc_block_num;
      std::optional<uint32_t>                      _head_block_num;
      std::optional<fc::time_point>                _head_timestamp;
      std::optional<uint32_t>                      _irr_block_num;
      std::optional<fc::time_point>                _irr_timestamp;
      const fc::microseconds                       _success_duration;
      const fc::microseconds                       _failure_duration;
   };

   using trx_finality_status_processing_ptr = std::unique_ptr<trx_finality_status_processing>;
} // namespace eosio::chain_apis

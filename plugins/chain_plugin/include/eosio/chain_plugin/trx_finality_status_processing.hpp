#pragma once
#include <eosio/chain/types.hpp>
#include <eosio/chain/block_state.hpp>
#include <eosio/chain/signals_processor.hpp>
#include <eosio/chain/trace.hpp>

#include <fc/container/tracked_storage.hpp>
#include <memory>

namespace eosio::chain_apis {

   struct trx_finality_status_processing_impl; 
   using trx_finality_status_processing_impl_ptr = std::unique_ptr<trx_finality_status_processing_impl>;
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

      ~trx_finality_status_processing();

      void signal_applied_transactions( const chain::signals_processor::trx_deque& trxs, const chain::block_state_ptr& bsp );

      void signal_irreversible_block( const chain::block_state_ptr& bsp );

      void signal_block_start( uint32_t block_num );

   private:
      trx_finality_status_processing_impl_ptr _my;
   };

   using trx_finality_status_processing_ptr = std::unique_ptr<trx_finality_status_processing>;
} // namespace eosio::chain_apis

#pragma once
#include <eosio/chain/types.hpp>
#include <eosio/chain/block_state.hpp>
#include <eosio/chain/exceptions.hpp>
#include <eosio/chain/trace.hpp>
#include <eosio/chain/block_interface.hpp>
#include <eosio/chain/trx_interface.hpp>

namespace eosio::chain {

class signals_processor {
public:
   /**
    * Class for tracking transactions and which block they belong to.
    * @param block_processor backend processor of block related signals
    * @param in_block_trx_processor backend processor of transactions in speculative blocks
    * @param speculative_block_trx_processor backend processor of transactions in speculative blocks
    * @param local_trx_processor backend processor of transactions applied locally
    */
   signals_processor( block_interface_ptr block_processor,  trx_interface_ptr in_block_trx_processor, trx_interface_ptr speculative_block_trx_processor, trx_interface_ptr local_trx_processor)
   : _block_processor(block_processor),
     _in_block_trx_processor(in_block_trx_processor),
     _speculative_block_trx_processor(speculative_block_trx_processor),
     _local_trx_processor(local_trx_processor),
     _trx_processor(_local_trx_processor) {
      EOS_ASSERT( _block_processor, plugin_config_exception, "signals_processor must be provided a block_processor" );
      EOS_ASSERT( _in_block_trx_processor, plugin_config_exception, "signals_processor must be provided a in_block_trx_processor" );
      EOS_ASSERT( _speculative_block_trx_processor, plugin_config_exception, "signals_processor must be provided a speculative_block_trx_processor" );
      EOS_ASSERT( _local_trx_processor, plugin_config_exception, "signals_processor must be provided a local_trx_processor" );
   }

   /// connect to chain controller applied_transaction signal
   void signal_applied_transaction( const chain::transaction_trace_ptr& trace, const chain::signed_transaction& strx ) {
      if (!_trx_processor) {
         _trx_processor = trace->producer_block_id ? _in_block_trx_processor : _speculative_block_trx_processor;
      }
      _trx_processor->signal_applied_transaction(trace, strx);
   }

   /// connect to chain controller accepted_block signal
   void signal_accepted_block( const chain::block_state_ptr& bsp ) {
      _block_processor->signal_accepted_block(bsp);
      _trx_processor = _local_trx_processor;
   }

   /// connect to chain controller irreversible_block signal
   void signal_irreversible_block( const chain::block_state_ptr& bsp ) {
      _block_processor->signal_irreversible_block(bsp);
   }


   /// connect to chain controller block_start signal
   void signal_block_start( uint32_t block_num ) {
      _block_processor->signal_block_start(block_num);
      // either it is a speculative block or real block. need to determine when we get a transaction
      _trx_processor.reset();
   }

private:
   const block_interface_ptr _block_processor;
   const trx_interface_ptr _in_block_trx_processor;
   const trx_interface_ptr _speculative_block_trx_processor;
   const trx_interface_ptr _local_trx_processor;

   trx_interface_ptr _trx_processor;
};

} // eosio::chain
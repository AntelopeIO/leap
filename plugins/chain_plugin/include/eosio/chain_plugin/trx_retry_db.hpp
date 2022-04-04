#pragma once
#include <eosio/chain/types.hpp>
#include <eosio/chain/block_state.hpp>
#include <eosio/chain/trace.hpp>

namespace eosio::chain_apis {

/**
 * This class manages the ephemeral indices and data that provide the transaction retry feature.
 */
class trx_retry_db {
public:

   /**
    * @param chain - controller to read data from, caller is expected to manage lifetimes such that this controller
    *                reference does not go stale for the life of this class.
    * @param max_mem_usage_size - maximum allowed memory for this feature, see track_transaction.
    * @param retry_interval - how often to retry transaction if not see in a block.
    */
   explicit trx_retry_db( const chain::controller& controller, size_t max_mem_usage_size, fc::microseconds retry_interval );
   ~trx_retry_db();

   trx_retry_db(trx_retry_db&&) = delete;
   trx_retry_db& operator=(trx_retry_db&&) = delete;

   /**
    * @param trx_meta trx to retry if not see in a block for retry_interval
    * @param num_blocks ack seen in a block after num_blocks have been accepted
    * @param lib if true num_blocks param is ignored and ack'ed when block with trx is LIB
    * @throws throw tx_resource_exhaustion if trx would exceeds max_mem_usage_size
    */
   void track_transaction( chain::transaction_metadata_ptr trx_meta, uint16_t num_blocks, bool lib );

   /**
    * Attach to chain applied_transaction signal
    * Add a transaction trace to the DB that has been applied to the controller
    */
   void on_applied_transaction( const chain::transaction_trace_ptr& trace, const chain::packed_transaction_ptr& ptrx );

   /**
    * Attach to chain block_start signal
    */
   void on_block_start( uint32_t block_num );

   /**
    * Attach to chain accepted_block signal
    */
   void on_accepted_block(const chain::block_state_ptr& block );

   /**
    * Attach to chain irreversible_block signal
    */
   void on_irreversible_block(const chain::block_state_ptr& block );

private:
   std::unique_ptr<struct trx_retry_db_impl> _impl;
};

} // namespace eosio::chain_apis

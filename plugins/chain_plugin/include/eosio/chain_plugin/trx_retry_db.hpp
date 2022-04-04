#pragma once
#include <eosio/chain/types.hpp>
#include <eosio/chain/block_state.hpp>
#include <eosio/chain/trace.hpp>

namespace eosio::chain_apis {

/**
 * This class manages the ephemeral indices and data that provide the transaction retry feature.
 * Transactions and meta data is persisted so that transactions are not lost on restart.
 */
class trx_retry_db {
public:

   /**
    * Instantiate a new transaction DB from the given chain controller
    * The caller is expected to manage lifetimes such that this controller reference does not go stale
    * for the life of the transaction retry db
    * @param chain - controller to read data from
    */
   explicit trx_retry_db( const class eosio::chain::controller& chain );
   ~trx_retry_db();

   trx_retry_db(trx_retry_db&&) = delete;
   trx_retry_db& operator=(trx_retry_db&&) = delete;

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

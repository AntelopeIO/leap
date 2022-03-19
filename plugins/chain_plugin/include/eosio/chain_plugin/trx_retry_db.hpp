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

      trx_retry_db(trx_retry_db&&);
      trx_retry_db& operator=(trx_retry_db&&);

      /**
       * Add a transaction trace to the DB that has been applied to the controller even though it may
       * not yet be committed to by a block.
       *
       * @param trace
       */
      void cache_transaction_trace( const chain::transaction_trace_ptr& trace );

      /**
       * Add a block to the DB, committing all the cached traces it represents and dumping any uncommitted traces.
       * @param block
       */
      void commit_block(const chain::block_state_ptr& block );

   private:
      std::unique_ptr<struct trx_retry_db_impl> _impl;
   };

} // namespace eosio::chain_apis

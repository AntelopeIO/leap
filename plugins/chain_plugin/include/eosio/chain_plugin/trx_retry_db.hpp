#pragma once
#include <eosio/chain/types.hpp>
#include <eosio/chain/trace.hpp>

namespace eosio::chain_apis {

/**
 * This class manages the ephemeral indices and data that provide the transaction retry feature.
 * It is designed to be run on an API node, as it only tracks incoming API transactions.
 * It will not work correctly on a BP node because on_applied_transaction looks for
 * transactions registered in track_transaction which is registered after speculatively executed.
 * If the transaction is only executed only in a final signed block, then it will not be seen
 * as executed and will expire.
 */
class trx_retry_db {
public:

   /**
    * @param chain - controller to read data from, caller is expected to manage lifetimes such that this controller
    *                reference does not go stale for the life of this class.
    * @param max_mem_usage_size - maximum allowed memory for this feature, see track_transaction.
    * @param retry_interval - how often to retry transaction if not see in a block.
    * @param max_expiration_time - the maximum allowed expiration on a retry transaction
    * @param abi_serializer_max_time - the configurable abi-serializer-max-time-ms option used for creating trace variants
    */
   explicit trx_retry_db( const chain::controller& controller, size_t max_mem_usage_size,
                          fc::microseconds retry_interval, fc::microseconds max_expiration_time,
                          fc::microseconds abi_serializer_max_time );
   ~trx_retry_db();

   trx_retry_db(trx_retry_db&&) = delete;
   trx_retry_db& operator=(trx_retry_db&&) = delete;

   /**
    * @return current max expiration allowed on a retry transaction
    */
   fc::time_point_sec get_max_expiration_time()const;

   /**
    * @return number of trxs being tracked
    */
   size_t size()const;

   /**
    * @param ptrx trx to retry if not see in a block for retry_interval
    * @param num_blocks ack seen in a block after num_blocks have been accepted, LIB if optional !has_value()
    * @param next report result to user by calling next
    * @throws throw tx_resource_exhaustion if trx would exceeds max_mem_usage_size
    */
   void track_transaction( chain::packed_transaction_ptr ptrx, std::optional<uint16_t> num_blocks, eosio::chain::next_function<std::unique_ptr<fc::variant>> next );

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
   void on_accepted_block( uint32_t block_num );

   /**
    * Attach to chain irreversible_block signal
    */
   void on_irreversible_block( const chain::signed_block_ptr& block );

private:
   std::unique_ptr<struct trx_retry_db_impl> _impl;
};

} // namespace eosio::chain_apis

#pragma once
#include <eosio/chain/types.hpp>
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

      struct chain_state {
         chain::block_id_type          head_id;
         chain::block_timestamp_type   head_block_timestamp;
         chain::block_id_type          irr_id;
         chain::block_timestamp_type   irr_block_timestamp;
         chain::block_id_type          earliest_tracked_block_id;
      };

      struct trx_state {
         chain::block_id_type block_id;
         fc::time_point       block_timestamp;
         fc::time_point       received;
         fc::time_point       expiration;
         std::string          status;
      };

      /**
       * Instantiate a new transaction retry processor
       * @param max_storage - the maximum storage allotted to this feature
       */
      trx_finality_status_processing( uint64_t max_storage, const fc::microseconds& success_duration, const fc::microseconds& failure_duration );

      ~trx_finality_status_processing();

      void signal_applied_transaction( const chain::transaction_trace_ptr& trace, const chain::packed_transaction_ptr& ptrx );

      void signal_accepted_block( const chain::signed_block_ptr& block, const chain::block_id_type& id );

      void signal_irreversible_block( const chain::signed_block_ptr& block, const chain::block_id_type& id );

      void signal_block_start( uint32_t block_num );

      chain_state get_chain_state() const;

      std::optional<trx_state> get_trx_state( const chain::transaction_id_type& id ) const;

      size_t get_storage_memory_size() const;

   private:
      trx_finality_status_processing_impl_ptr _my;
   };

   using trx_finality_status_processing_ptr = std::unique_ptr<trx_finality_status_processing>;
} // namespace eosio::chain_apis

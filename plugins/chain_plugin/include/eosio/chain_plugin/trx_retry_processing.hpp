#pragma once
#include <eosio/chain/types.hpp>
#include <eosio/chain/block_state.hpp>
#include <eosio/chain/block_interface.hpp>
#include <eosio/chain/trx_interface.hpp>
#include <eosio/chain/trace.hpp>
#include <memory>

namespace eosio::chain_apis {
   /**
    * This class manages the processing related to the transaction retry feature.
    */
   class trx_retry_processing {
   public:

      /**
       * Instantiate a new transaction retry processor
       * @param max_storage - the maximum storage allotted to this feature
       */
      trx_retry_processing( uint64_t max_storage );

      chain::block_interface_ptr get_block_processor() const;

      chain::trx_interface_ptr get_in_block_trx_processor() const;

      chain::trx_interface_ptr get_speculative_trx_processor() const;

      chain::trx_interface_ptr get_local_trx_processor() const;
   };

   using trx_retry_processing_ptr = std::shared_ptr<trx_retry_processing>;
} // namespace eosio::chain_apis

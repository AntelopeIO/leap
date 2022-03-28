#pragma once
#include <eosio/chain/types.hpp>
#include <eosio/chain/block_state.hpp>
#include <eosio/chain/trace.hpp>
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
   };

   using trx_finality_status_processing_ptr = std::shared_ptr<trx_finality_status_processing>;
} // namespace eosio::chain_apis

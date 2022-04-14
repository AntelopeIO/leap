#pragma once
#include <eosio/chain/block_header.hpp>
#include <eosio/chain/types.hpp>
#include <eosio/chain/multi_index_includes.hpp>
#include <boost/multi_index/hashed_index.hpp>

namespace bmi = boost::multi_index;

namespace eosio {
   /**
    * @brief tracks status related to a transaction in the blockchain
    * @ingroup object
    *
    * To report the status of a transaction in the system we need to track what
    * block it is seen in as well as determine when it has failed or needs to no
    * no longer be tracked.
    */
   struct finality_status_object
   {
      chain::transaction_id_type  trx_id;
      fc::time_point              trx_expiry;      // if block time past trx_expiry && !block_num -> in failed list
      fc::time_point              received;
      chain::block_id_type        block_id;
      chain::block_timestamp_type block_timestamp;
      bool                        forked_out = false; // if !block_num && forked_out -> status == "forked out"

      size_t memory_size() const { return sizeof(*this); }
      bool is_in_block() const {
         return !forked_out && block_id != chain::block_id_type{};
      }
      uint32_t block_num() const { return chain::block_header::num_from_id(block_id); }
   };

   namespace finality_status {
      struct by_trx_id;
      struct by_status_expiry;
      struct by_block_num;

      using cbh = chain::block_header;

      constexpr uint32_t no_block_num = 0;
   }

   using finality_status_multi_index = boost::multi_index_container<
      finality_status_object,
      indexed_by<
         bmi::hashed_unique< tag<finality_status::by_trx_id>,
                             member<finality_status_object, chain::transaction_id_type, &finality_status_object::trx_id> >,
         ordered_non_unique< tag<finality_status::by_status_expiry>, 
            composite_key< finality_status_object,
               const_mem_fun<finality_status_object, bool,           &finality_status_object::is_in_block>,
               member< finality_status_object,       fc::time_point, &finality_status_object::received >
            >
         >,
         ordered_non_unique< tag<finality_status::by_block_num>,
                             const_mem_fun<finality_status_object, uint32_t, &finality_status_object::block_num> >
      >
   >;

}

FC_REFLECT( eosio::finality_status_object, (trx_id)(trx_expiry)(received)(block_id)(block_timestamp)(forked_out) )

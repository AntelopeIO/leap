#pragma once
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
      fc::time_point              status_expiry;
      std::optional<uint32_t>           block_num;
      bool                              forked_out = false; // if !block_num && forked_out -> status == "forked out"

      size_t memory_size() const { sizeof(*this); }
   };

   struct by_trx_id;
   struct by_success_status_expiry;
   struct by_fail_status_expiry;
   struct by_block_num;
   using finality_status_multi_index = boost::multi_index_container<
      finality_status_object,
      indexed_by<
         bmi::hashed_unique< tag<by_trx_id>, BOOST_MULTI_INDEX_MEMBER(finality_status_object, chain::transaction_id_type, trx_id) >,
         ordered_non_unique< tag<by_success_status_expiry>, 
            composite_key< finality_status_object,
               member<finality_status_object, std::optional<uint32_t>,      &finality_status_object::block_num>,
               member<finality_status_object, fc::time_point,         &finality_status_object::status_expiry>
            >,
            composite_key_compare<
               std::less< std::optional<uint32_t> >,
               std::less< fc::time_point >
            >
         >,
         ordered_non_unique< tag<by_fail_status_expiry>, 
            composite_key< finality_status_object,
               member<finality_status_object, std::optional<uint32_t>,      &finality_status_object::block_num>,
               member<finality_status_object, fc::time_point,         &finality_status_object::status_expiry>
            >,
            composite_key_compare<
               std::greater< std::optional<uint32_t> >,
               std::less< fc::time_point >
            >
         >,
         ordered_non_unique< tag<by_block_num>, BOOST_MULTI_INDEX_MEMBER(finality_status_object, std::optional<uint32_t>, &finality_status_object::block_num) >
      >
   >;

}

FC_REFLECT( eosio::finality_status_object, (trx_id)(trx_expiry)(status_expiry)(block_num) )

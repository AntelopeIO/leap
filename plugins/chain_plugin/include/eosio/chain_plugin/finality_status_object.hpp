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
      fc::time_point              received;
      uint32_t                    block_num;
      bool                        forked_out = false; // if !block_num && forked_out -> status == "forked out"

      size_t size() const { return sizeof(*this); }
   };

   namespace finality_status {
      struct by_trx_id;
      struct by_success_status_expiry;
      struct by_fail_status_expiry;
      struct by_block_num;

      constexpr uint32_t no_block_num = 0;

      struct real_block_comparator {
         bool operator()(uint32_t lhs, uint32_t rhs) const {
            return lhs == no_block_num && rhs != no_block_num;
         }
      };
   }

   using finality_status_multi_index = boost::multi_index_container<
      finality_status_object,
      indexed_by<
         bmi::hashed_unique< tag<finality_status::by_trx_id>, member<finality_status_object, chain::transaction_id_type, &finality_status_object::trx_id> >,
         ordered_non_unique< tag<finality_status::by_success_status_expiry>, 
            composite_key< finality_status_object,
               member< finality_status_object, uint32_t,       &finality_status_object::block_num >,
               member< finality_status_object, fc::time_point, &finality_status_object::received >
            >,
            composite_key_compare<
               finality_status::real_block_comparator,
               std::greater< >
            >
         >,
         ordered_non_unique< tag<finality_status::by_block_num>, member<finality_status_object, uint32_t, &finality_status_object::block_num> >
      >
   >;

}

FC_REFLECT( eosio::finality_status_object, (trx_id)(trx_expiry)(received)(block_num) )

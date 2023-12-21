#pragma once

#include <eosio/chain/block_header_state_legacy.hpp>
#include <eosio/chain/block.hpp>
#include <eosio/chain/transaction_metadata.hpp>
#include <eosio/chain/action_receipt.hpp>

namespace eosio { namespace chain {

   struct block_state_legacy : public block_header_state_legacy {
      block_state_legacy( const block_header_state_legacy& prev,
                          signed_block_ptr b,
                          const protocol_feature_set& pfs,
                          bool hotstuff_activated,
                          const validator_t& validator,
                          bool skip_validate_signee
                 );

      block_state_legacy( pending_block_header_state_legacy&& cur,
                          signed_block_ptr&& b, // unsigned block
                          deque<transaction_metadata_ptr>&& trx_metas,
                          const protocol_feature_set& pfs,
                          const validator_t& validator,
                          const signer_callback_type& signer
                );

      block_state_legacy() = default;


      signed_block_ptr      block;

      // internal use only, not thread safe
      const block_id_type&   id()                    const { return block_header_state_legacy::id; }
      const block_id_type&   previous()              const { return block_header_state_legacy::prev(); }
      uint32_t               irreversible_blocknum() const { return dpos_irreversible_blocknum; }
      uint32_t               block_num()             const { return block_header_state_legacy::block_num; }
      block_timestamp_type   timestamp()             const { return header.timestamp; }
      account_name           producer()              const { return header.producer; }
      const extensions_type& header_extensions()     const { return header.header_extensions; }
      bool                   is_valid()              const { return validated; }
      void                   set_valid(bool b)             { validated = b; }
      
      protocol_feature_activation_set_ptr    get_activated_protocol_features() const { return activated_protocol_features; }
      const producer_authority_schedule&     active_schedule()  const { return block_header_state_legacy_common::active_schedule; }
      const producer_authority_schedule&     pending_schedule() const { return block_header_state_legacy::pending_schedule.schedule; }
      const deque<transaction_metadata_ptr>& trxs_metas()       const { return _cached_trxs; }

      
   private: // internal use only, not thread safe
      friend struct fc::reflector<block_state_legacy>;
      friend struct controller_impl;
      friend struct completed_block;

      bool is_pub_keys_recovered()const { return _pub_keys_recovered; }
      
      deque<transaction_metadata_ptr> extract_trxs_metas() {
         _pub_keys_recovered = false;
         auto result = std::move( _cached_trxs );
         _cached_trxs.clear();
         return result;
      }
      void set_trxs_metas( deque<transaction_metadata_ptr>&& trxs_metas, bool keys_recovered ) {
         _pub_keys_recovered = keys_recovered;
         _cached_trxs = std::move( trxs_metas );
      }

      bool                                                validated = false;

      bool                                                _pub_keys_recovered = false;
      /// this data is redundant with the data stored in block, but facilitates
      /// recapturing transactions when we pop a block
      deque<transaction_metadata_ptr>                    _cached_trxs;
   };

   using block_state_legacy_ptr = std::shared_ptr<block_state_legacy>;

} } /// namespace eosio::chain

FC_REFLECT_DERIVED( eosio::chain::block_state_legacy, (eosio::chain::block_header_state_legacy), (block)(validated) )

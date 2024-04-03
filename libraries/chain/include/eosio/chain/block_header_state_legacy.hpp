#pragma once
#include <eosio/chain/block_header.hpp>
#include <eosio/chain/incremental_merkle_legacy.hpp>
#include <eosio/chain/protocol_feature_manager.hpp>
#include <eosio/chain/chain_snapshot.hpp>
#include <future>

namespace eosio::chain {

namespace snapshot_detail {
   struct snapshot_block_header_state_legacy_v2;
   struct snapshot_block_header_state_legacy_v3;
}

namespace detail {
   struct schedule_info {
      // schedule_lib_num is compared with dpos lib, but the value is actually current block at time of pending
      // After hotstuff is activated, schedule_lib_num is compared to next().next() round for determination of
      // changing from pending to active.
      uint32_t                          schedule_lib_num = 0; /// block_num of pending
      digest_type                       schedule_hash;
      producer_authority_schedule       schedule;
   };

}

using signer_callback_type = std::function<std::vector<signature_type>(const digest_type&)>;

struct block_header_state_legacy;

namespace detail {
   struct block_header_state_legacy_common {
      uint32_t                          block_num = 0;
      uint32_t                          dpos_proposed_irreversible_blocknum = 0;
      uint32_t                          dpos_irreversible_blocknum = 0;
      producer_authority_schedule       active_schedule;
      incremental_merkle_tree_legacy    blockroot_merkle;
      flat_map<account_name,uint32_t>   producer_to_last_produced;
      flat_map<account_name,uint32_t>   producer_to_last_implied_irb;
      block_signing_authority           valid_block_signing_authority;
      vector<uint8_t>                   confirm_count;
   };
}

struct pending_block_header_state_legacy : public detail::block_header_state_legacy_common {
   protocol_feature_activation_set_ptr  prev_activated_protocol_features;
   detail::schedule_info                prev_pending_schedule;
   bool                                 was_pending_promoted = false;
   block_id_type                        previous;
   account_name                         producer;
   block_timestamp_type                 timestamp;
   uint32_t                             active_schedule_version = 0;
   uint16_t                             confirmed = 1;
   std::optional<qc_claim_t>            qc_claim; // transition to savanna has begun

   bool is_if_transition_block() const { return !!qc_claim;  }

   signed_block_header make_block_header( const checksum256_type& transaction_mroot,
                                          const checksum256_type& action_mroot,
                                          const std::optional<producer_authority_schedule>& new_producers,
                                          std::optional<finalizer_policy>&& new_finalizer_policy,
                                          vector<digest_type>&& new_protocol_feature_activations,
                                          const protocol_feature_set& pfs)const;

   block_header_state_legacy  finish_next( const signed_block_header& h,
                                           vector<signature_type>&& additional_signatures,
                                           const protocol_feature_set& pfs,
                                           validator_t& validator,
                                           bool skip_validate_signee = false )&&;

   block_header_state_legacy  finish_next( signed_block_header& h,
                                           const protocol_feature_set& pfs,
                                           validator_t& validator,
                                           const signer_callback_type& signer )&&;

protected:
   block_header_state_legacy  _finish_next( const signed_block_header& h,
                                            const protocol_feature_set& pfs,
                                            validator_t& validator )&&;
};

/**
 *  @struct block_header_state
 *
 *  Algorithm for producer schedule change (pre-savanna)
 *     privileged contract -> set_proposed_producers(producers) ->
 *        global_property_object.proposed_schedule_block_num = current_block_num
 *        global_property_object.proposed_schedule           = producers
 *
 *     start_block -> (global_property_object.proposed_schedule_block_num == dpos_lib)
 *        building_block._new_pending_producer_schedule = producers
 *
 *     finish_block ->
 *        block_header.extensions.wtmsig_block_signatures = producers
 *        block_header.new_producers                      = producers
 *
 *     create_block_state ->
 *        block_state.schedule_lib_num          = current_block_num (note this should be named schedule_block_num)
 *        block_state.pending_schedule.schedule = producers
 *
 *     start_block ->
 *        block_state.prev_pending_schedule = pending_schedule (producers)
 *        if (pending_schedule.schedule_lib_num == dpos_lib)
 *           block_state.active_schedule = pending_schedule
 *           block_state.was_pending_promoted = true
 *           block_state.pending_schedule.clear() // doesn't get copied from previous
 *        else
 *           block_state.pending_schedule = prev_pending_schedule
 *
 *
 *  @struct block_header_state_legacy
 *  @brief defines the minimum state necessary to validate transaction headers
 */
struct block_header_state_legacy : public detail::block_header_state_legacy_common {
   block_id_type                        id;
   signed_block_header                  header;
   detail::schedule_info                pending_schedule;
   protocol_feature_activation_set_ptr  activated_protocol_features;
   vector<signature_type>               additional_signatures;

   /// this data is redundant with the data stored in header, but it acts as a cache that avoids
   /// duplication of work
   header_extension_multimap            header_exts;

   block_header_state_legacy() = default;

   explicit block_header_state_legacy( detail::block_header_state_legacy_common&& base )
   :detail::block_header_state_legacy_common( std::move(base) )
   {}

   explicit block_header_state_legacy( snapshot_detail::snapshot_block_header_state_legacy_v2&& snapshot );
   explicit block_header_state_legacy( snapshot_detail::snapshot_block_header_state_legacy_v3&& snapshot );

   pending_block_header_state_legacy next( block_timestamp_type when, uint16_t num_prev_blocks_to_confirm )const;

   block_header_state_legacy  next( const signed_block_header& h,
                                    vector<signature_type>&& additional_signatures,
                                    const protocol_feature_set& pfs,
                                    validator_t& validator,
                                    bool skip_validate_signee = false )const;

   uint32_t             calc_dpos_last_irreversible( account_name producer_of_next_block )const;

   const protocol_feature_activation_set_ptr& get_activated_protocol_features() const { return activated_protocol_features; }
   const producer_authority& get_scheduled_producer( block_timestamp_type t )const;
   const block_id_type&   previous()const { return header.previous; }
   digest_type            sig_digest()const;
   void                   sign( const signer_callback_type& signer );
   void                   verify_signee()const;

   const vector<digest_type>& get_new_protocol_feature_activations()const;
};

using block_header_state_legacy_ptr = std::shared_ptr<block_header_state_legacy>;

} /// namespace eosio::chain

FC_REFLECT( eosio::chain::detail::block_header_state_legacy_common,
            (block_num)
            (dpos_proposed_irreversible_blocknum)
            (dpos_irreversible_blocknum)
            (active_schedule)
            (blockroot_merkle)
            (producer_to_last_produced)
            (producer_to_last_implied_irb)
            (valid_block_signing_authority)
            (confirm_count)
)

FC_REFLECT( eosio::chain::detail::schedule_info,
            (schedule_lib_num)
            (schedule_hash)
            (schedule)
)

FC_REFLECT_DERIVED(  eosio::chain::block_header_state_legacy, (eosio::chain::detail::block_header_state_legacy_common),
                     (id)
                     (header)
                     (pending_schedule)
                     (activated_protocol_features)
                     (additional_signatures)
)



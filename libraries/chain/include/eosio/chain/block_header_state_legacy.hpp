#pragma once
#include <eosio/chain/block_header.hpp>
#include <eosio/chain/incremental_merkle.hpp>
#include <eosio/chain/protocol_feature_manager.hpp>
#include <eosio/chain/finalizer_set.hpp>
#include <eosio/chain/chain_snapshot.hpp>
#include <future>

namespace eosio { namespace chain {

namespace legacy {

   /**
    * a fc::raw::unpack compatible version of the old block_state structure stored in
    * version 2 snapshots
    */
   struct snapshot_block_header_state_v2 {
      static constexpr uint32_t minimum_version = 0;
      static constexpr uint32_t maximum_version = 2;
      static_assert(chain_snapshot_header::minimum_compatible_version <= maximum_version, "snapshot_block_header_state_v2 is no longer needed");

      struct schedule_info {
         uint32_t                          schedule_lib_num = 0; /// last irr block num
         digest_type                       schedule_hash;
         producer_schedule_type            schedule;
      };

      /// from block_header_state_common
      uint32_t                             block_num = 0;
      uint32_t                             dpos_proposed_irreversible_blocknum = 0;
      uint32_t                             dpos_irreversible_blocknum = 0;
      producer_schedule_type               active_schedule;
      incremental_canonical_merkle_tree    blockroot_merkle;
      flat_map<account_name,uint32_t>      producer_to_last_produced;
      flat_map<account_name,uint32_t>      producer_to_last_implied_irb;
      public_key_type                      block_signing_key;
      vector<uint8_t>                      confirm_count;

      // from block_header_state
      block_id_type                        id;
      signed_block_header                  header;
      schedule_info                        pending_schedule;
      protocol_feature_activation_set_ptr  activated_protocol_features;
   };
}

using signer_callback_type = std::function<std::vector<signature_type>(const digest_type&)>;

struct block_header_state;

// totem for dpos_irreversible_blocknum after hotstuff is activated
// This value implicitly means that fork_database will prefer hotstuff blocks over dpos blocks
constexpr uint32_t hs_dpos_irreversible_blocknum = std::numeric_limits<uint32_t>::max();

namespace detail {
   struct block_header_state_common {
      uint32_t                          block_num = 0;
      uint32_t                          dpos_proposed_irreversible_blocknum = 0;
      uint32_t                          dpos_irreversible_blocknum = 0;
      producer_authority_schedule       active_schedule;
      uint32_t                          last_proposed_finalizer_set_generation = 0; // TODO: Add to snapshot_block_header_state_v3
      incremental_canonical_merkle_tree blockroot_merkle;
      flat_map<account_name,uint32_t>   producer_to_last_produced;
      flat_map<account_name,uint32_t>   producer_to_last_implied_irb;
      block_signing_authority           valid_block_signing_authority;
      vector<uint8_t>                   confirm_count;
   };

   struct schedule_info {
      // schedule_lib_num is compared with dpos lib, but the value is actually current block at time of pending
      // After hotstuff is activated, schedule_lib_num is compared to next().next() round for determination of
      // changing from pending to active.
      uint32_t                          schedule_lib_num = 0; /// block_num of pending
      digest_type                       schedule_hash;
      producer_authority_schedule       schedule;
   };

   bool is_builtin_activated( const protocol_feature_activation_set_ptr& pfa,
                              const protocol_feature_set& pfs,
                              builtin_protocol_feature_t feature_codename );
}

struct pending_block_header_state : public detail::block_header_state_common {
   protocol_feature_activation_set_ptr  prev_activated_protocol_features;
   detail::schedule_info                prev_pending_schedule;
   std::optional<finalizer_set>         proposed_finalizer_set; // set by set_finalizer host function
   bool                                 was_pending_promoted = false;
   block_id_type                        previous;
   account_name                         producer;
   block_timestamp_type                 timestamp;
   uint32_t                             active_schedule_version = 0;
   uint16_t                             confirmed = 1;

   signed_block_header make_block_header( const checksum256_type& transaction_mroot,
                                          const checksum256_type& action_mroot,
                                          const std::optional<producer_authority_schedule>& new_producers,
                                          vector<digest_type>&& new_protocol_feature_activations,
                                          const protocol_feature_set& pfs)const;

   block_header_state  finish_next( const signed_block_header& h,
                                    vector<signature_type>&& additional_signatures,
                                    const protocol_feature_set& pfs,
                                    const std::function<void( block_timestamp_type,
                                                              const flat_set<digest_type>&,
                                                              const vector<digest_type>& )>& validator,
                                    bool skip_validate_signee = false )&&;

   block_header_state  finish_next( signed_block_header& h,
                                    const protocol_feature_set& pfs,
                                    const std::function<void( block_timestamp_type,
                                                              const flat_set<digest_type>&,
                                                              const vector<digest_type>& )>& validator,
                                    const signer_callback_type& signer )&&;

protected:
   block_header_state  _finish_next( const signed_block_header& h,
                                     const protocol_feature_set& pfs,
                                     const std::function<void( block_timestamp_type,
                                                               const flat_set<digest_type>&,
                                                               const vector<digest_type>& )>& validator )&&;
};

/**
 *  @struct block_header_state_core
 *
 *  A data structure holding hotstuff core information
 */
struct block_header_state_core {
   // the block height of the last irreversible (final) block.
   uint32_t last_final_block_height = 0;

   // the block height of the block that would become irreversible (final) if the
   // associated block header was to achieve a strong QC.
   std::optional<uint32_t> final_on_strong_qc_block_height;

   // the block height of the block that is referenced as the last QC block
   std::optional<uint32_t> last_qc_block_height;

   block_header_state_core() = default;

   explicit block_header_state_core( uint32_t last_final_block_height,
                                     std::optional<uint32_t> final_on_strong_qc_block_height,
                                     std::optional<uint32_t> last_qc_block_height );

   block_header_state_core next( uint32_t last_qc_block_height,
                                 bool is_last_qc_strong);
};
/**
 *  @struct block_header_state
 *
 *  Algorithm for producer schedule change (pre-hostuff)
 *     privileged contract -> set_proposed_producers(producers) ->
 *        global_property_object.proposed_schedule_block_num = current_block_num
 *        global_property_object.proposed_schedule           = producers
 *
 *     start_block -> (global_property_object.proposed_schedule_block_num == dpos_lib)
 *        building_block._new_pending_producer_schedule = producers
 *
 *     finalize_block ->
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
 *  @brief defines the minimum state necessary to validate transaction headers
 */
struct block_header_state : public detail::block_header_state_common {
   block_id_type                        id;
   signed_block_header                  header;
   detail::schedule_info                pending_schedule;
   protocol_feature_activation_set_ptr  activated_protocol_features;
   vector<signature_type>               additional_signatures;

   /// this data is redundant with the data stored in header, but it acts as a cache that avoids
   /// duplication of work
   flat_multimap<uint16_t, block_header_extension> header_exts;

   block_header_state() = default;

   explicit block_header_state( detail::block_header_state_common&& base )
   :detail::block_header_state_common( std::move(base) )
   {}

   explicit block_header_state( legacy::snapshot_block_header_state_v2&& snapshot );

   pending_block_header_state  next( block_timestamp_type when, bool hotstuff_activated, uint16_t num_prev_blocks_to_confirm )const;

   block_header_state   next( const signed_block_header& h,
                              vector<signature_type>&& additional_signatures,
                              const protocol_feature_set& pfs,
                              bool hotstuff_activated,
                              const std::function<void( block_timestamp_type,
                                                        const flat_set<digest_type>&,
                                                        const vector<digest_type>& )>& validator,
                              bool skip_validate_signee = false )const;

   uint32_t             calc_dpos_last_irreversible( account_name producer_of_next_block )const;

   producer_authority     get_scheduled_producer( block_timestamp_type t )const;
   const block_id_type&   prev()const { return header.previous; }
   digest_type            sig_digest()const;
   void                   sign( const signer_callback_type& signer );
   void                   verify_signee()const;

   const vector<digest_type>& get_new_protocol_feature_activations()const;
};

using block_header_state_ptr = std::shared_ptr<block_header_state>;

} } /// namespace eosio::chain

FC_REFLECT( eosio::chain::detail::block_header_state_common,
            (block_num)
            (dpos_proposed_irreversible_blocknum)
            (dpos_irreversible_blocknum)
            (active_schedule)
            (last_proposed_finalizer_set_generation)
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

FC_REFLECT_DERIVED(  eosio::chain::block_header_state, (eosio::chain::detail::block_header_state_common),
                     (id)
                     (header)
                     (pending_schedule)
                     (activated_protocol_features)
                     (additional_signatures)
)


FC_REFLECT( eosio::chain::legacy::snapshot_block_header_state_v2::schedule_info,
          ( schedule_lib_num )
          ( schedule_hash )
          ( schedule )
)


FC_REFLECT( eosio::chain::legacy::snapshot_block_header_state_v2,
          ( block_num )
          ( dpos_proposed_irreversible_blocknum )
          ( dpos_irreversible_blocknum )
          ( active_schedule )
          ( blockroot_merkle )
          ( producer_to_last_produced )
          ( producer_to_last_implied_irb )
          ( block_signing_key )
          ( confirm_count )
          ( id )
          ( header )
          ( pending_schedule )
          ( activated_protocol_features )
)

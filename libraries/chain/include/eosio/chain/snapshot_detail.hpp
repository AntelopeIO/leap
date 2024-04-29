#pragma once
#include <eosio/chain/block_header.hpp>
#include <eosio/chain/incremental_merkle_legacy.hpp>
#include <eosio/chain/protocol_feature_manager.hpp>
#include <eosio/chain/chain_snapshot.hpp>
#include <eosio/chain/block_state_legacy.hpp>
#include <eosio/chain/block_state.hpp>

namespace eosio::chain::snapshot_detail {

   /**
    * a fc::raw::unpack compatible version of the old block_state structure stored in
    * version 2 snapshots
    */
   struct snapshot_block_header_state_legacy_v2 {
      static constexpr uint32_t minimum_version = 0;
      static constexpr uint32_t maximum_version = 2;
      static_assert(chain_snapshot_header::minimum_compatible_version <= maximum_version,
                    "snapshot_block_header_state_v2 is no longer needed");

      struct schedule_info {
         uint32_t                          schedule_lib_num = 0; /// last irr block num
         digest_type                       schedule_hash;
         legacy::producer_schedule_type    schedule;
      };

      /// from block_header_state_legacy_common
      uint32_t                             block_num = 0;
      uint32_t                             dpos_proposed_irreversible_blocknum = 0;
      uint32_t                             dpos_irreversible_blocknum = 0;
      legacy::producer_schedule_type       active_schedule;
      incremental_merkle_tree_legacy       blockroot_merkle;
      flat_map<account_name,uint32_t>      producer_to_last_produced;
      flat_map<account_name,uint32_t>      producer_to_last_implied_irb;
      public_key_type                      block_signing_key;
      vector<uint8_t>                      confirm_count;

      // from block_header_state_legacy
      block_id_type                        id;
      signed_block_header                  header;
      schedule_info                        pending_schedule;
      protocol_feature_activation_set_ptr  activated_protocol_features;
   };

   /**
    * a fc::raw::unpack compatible version of the old block_state_legacy structure stored in
    * version 3 to 6 snapshots
    */
   struct snapshot_block_header_state_legacy_v3 {
      static constexpr uint32_t minimum_version = 3;
      static constexpr uint32_t maximum_version = 6;
      static_assert(chain_snapshot_header::minimum_compatible_version <= maximum_version,
                    "snapshot_block_header_state_v3 is no longer needed");

      /// from block_header_state_legacy_common
      uint32_t                             block_num = 0;
      uint32_t                             dpos_proposed_irreversible_blocknum = 0;
      uint32_t                             dpos_irreversible_blocknum = 0;
      producer_authority_schedule          active_schedule;
      incremental_merkle_tree_legacy       blockroot_merkle;
      flat_map<account_name,uint32_t>      producer_to_last_produced;
      flat_map<account_name,uint32_t>      producer_to_last_implied_irb;
      block_signing_authority              valid_block_signing_authority;
      vector<uint8_t>                      confirm_count;

      // from block_header_state_legacy
      block_id_type                        id;
      signed_block_header                  header;
      detail::schedule_info                pending_schedule;
      protocol_feature_activation_set_ptr  activated_protocol_features;
      vector<signature_type>               additional_signatures;

      snapshot_block_header_state_legacy_v3() = default;

      explicit snapshot_block_header_state_legacy_v3(const block_state_legacy& bs)
         : block_num(bs.block_num())
         , dpos_proposed_irreversible_blocknum(bs.dpos_proposed_irreversible_blocknum)
         , dpos_irreversible_blocknum(bs.dpos_irreversible_blocknum)
         , active_schedule(bs.active_schedule)
         , blockroot_merkle(bs.blockroot_merkle)
         , producer_to_last_produced(bs.producer_to_last_produced)
         , producer_to_last_implied_irb(bs.producer_to_last_implied_irb)
         , valid_block_signing_authority(bs.valid_block_signing_authority)
         , confirm_count(bs.confirm_count)
         , id(bs.id())
         , header(bs.header)
         , pending_schedule(bs.pending_schedule)
         , activated_protocol_features(bs.activated_protocol_features)
         , additional_signatures(bs.additional_signatures)
      {}
   };

   /**
    *      Snapshot V7 Data structures
    *      ---------------------------
    */
   struct snapshot_block_state_legacy_v7 : public snapshot_block_header_state_legacy_v3 {
      // additional member that can be present in `Transition Legacy Block` and
      // is needed to convert to `Transition IF Block` (see https://github.com/AntelopeIO/leap/issues/2080)
      using valid_t = uint32_t; // snapshot todo
      std::optional<valid_t> valid;

      snapshot_block_state_legacy_v7() = default;

      explicit snapshot_block_state_legacy_v7(const block_state_legacy& bs)
         : snapshot_block_header_state_legacy_v3(bs)
         , valid(0)  // snapshot todo
      {}
   };

   struct snapshot_block_state_v7 {
      // from block_header_state
      block_id_type                                       block_id;
      block_header                                        header;
      protocol_feature_activation_set_ptr                 activated_protocol_features;
      finality_core                                       core;
      finalizer_policy_ptr                                active_finalizer_policy;
      proposer_policy_ptr                                 active_proposer_policy;
      flat_map<block_timestamp_type, proposer_policy_ptr> proposer_policies;
      flat_map<uint32_t, finalizer_policy_ptr>            finalizer_policies;

      // from block_state
      std::optional<valid_t>                              valid;

      snapshot_block_state_v7() = default;

      explicit snapshot_block_state_v7(const block_state& bs)
         : block_id(bs.block_id)
         , header(bs.header)
         , activated_protocol_features(bs.activated_protocol_features)
         , core(bs.core)
         , active_finalizer_policy(bs.active_finalizer_policy)
         , active_proposer_policy(bs.active_proposer_policy)
         , proposer_policies(bs.proposer_policies)
         , finalizer_policies(bs.finalizer_policies)
         , valid(bs.valid)
      {}
   };

   struct snapshot_block_state_data_v7 {
      static constexpr uint32_t minimum_version = 7;
      static constexpr uint32_t maximum_version = 7;

      std::optional<snapshot_block_state_legacy_v7> bs_l;
      std::optional<snapshot_block_state_v7>        bs;

      snapshot_block_state_data_v7() = default;

      explicit snapshot_block_state_data_v7(const block_state_pair& p)
      {
         if (p.first)
            bs_l = snapshot_block_state_legacy_v7(*p.first);
         if (p.second)
            bs = snapshot_block_state_v7(*p.second);
      }
   };

}


FC_REFLECT( eosio::chain::snapshot_detail::snapshot_block_header_state_legacy_v2::schedule_info,
          ( schedule_lib_num )
          ( schedule_hash )
          ( schedule )
)


FC_REFLECT( eosio::chain::snapshot_detail::snapshot_block_header_state_legacy_v2,
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

FC_REFLECT( eosio::chain::snapshot_detail::snapshot_block_header_state_legacy_v3,
          ( block_num )
          ( dpos_proposed_irreversible_blocknum )
          ( dpos_irreversible_blocknum )
          ( active_schedule )
          ( blockroot_merkle )
          ( producer_to_last_produced )
          ( producer_to_last_implied_irb )
          ( valid_block_signing_authority )
          ( confirm_count )
          ( id )
          ( header )
          ( pending_schedule )
          ( activated_protocol_features )
          ( additional_signatures )
)

FC_REFLECT_DERIVED( eosio::chain::snapshot_detail::snapshot_block_state_legacy_v7,
                    (eosio::chain::snapshot_detail::snapshot_block_header_state_legacy_v3),
                    (valid)
   )

FC_REFLECT( eosio::chain::snapshot_detail::snapshot_block_state_v7,
            (block_id)
            (header)
            (activated_protocol_features)
            (core)
            (active_finalizer_policy)
            (active_proposer_policy)
            (proposer_policies)
            (finalizer_policies)
            (valid)
   )

FC_REFLECT( eosio::chain::snapshot_detail::snapshot_block_state_data_v7,
            (bs_l)
            (bs)
   )

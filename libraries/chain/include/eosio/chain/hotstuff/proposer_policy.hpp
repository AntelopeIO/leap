#pragma once

#include <eosio/chain/block_timestamp.hpp>
#include <eosio/chain/producer_schedule.hpp>

namespace eosio::chain {

struct proposer_policy {
   constexpr static uint8_t    current_schema_version = 1;
   uint8_t                     schema_version {current_schema_version};
   // Useful for light clients, not necessary for nodeos
   block_timestamp_type        active_time; // block when schedule will become active
   producer_authority_schedule proposer_schedule;
};

} /// eosio::chain

FC_REFLECT( eosio::chain::proposer_policy, (schema_version)(active_time)(proposer_schedule) )

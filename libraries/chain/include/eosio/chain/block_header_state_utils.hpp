#pragma once
#include "eosio/chain/protocol_feature_manager.hpp"
#include <eosio/chain/block_header.hpp>

namespace eosio::chain::detail {

   inline bool is_builtin_activated(const protocol_feature_activation_set_ptr& pfa,
                                    const protocol_feature_set& pfs,
                                    builtin_protocol_feature_t feature_codename) {
      auto digest = pfs.get_builtin_digest(feature_codename);
      const auto& protocol_features = pfa->protocol_features;
      return digest && protocol_features.find(*digest) != protocol_features.end();
   }

   inline block_timestamp_type get_next_next_round_block_time( block_timestamp_type t) {
      auto index = t.slot % config::producer_repetitions; // current index in current round
      //                                   (increment to the end of this round  ) + next round
      return block_timestamp_type{t.slot + (config::producer_repetitions - index) + config::producer_repetitions};
   }

   inline producer_authority get_scheduled_producer(const vector<producer_authority>& producers, block_timestamp_type t) {
      auto index = t.slot % (producers.size() * config::producer_repetitions);
      index /= config::producer_repetitions;
      return producers[index];
   }

} /// namespace eosio::chain

#pragma once
#include "eosio/chain/protocol_feature_manager.hpp"
#include <eosio/chain/block.hpp>
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

   inline const producer_authority& get_scheduled_producer(const vector<producer_authority>& producers, block_timestamp_type t) {
      auto index = t.slot % (producers.size() * config::producer_repetitions);
      index /= config::producer_repetitions;
      return producers[index];
   }

   constexpr auto additional_sigs_eid = additional_block_signatures_extension::extension_id();

   /**
    * Given a complete signed block, extract the validated additional signatures if present;
    *
    * @param b complete signed block
    * @return the list of additional signatures
    */
   inline vector<signature_type> extract_additional_signatures(const signed_block_ptr& b) {
      auto exts = b->validate_and_extract_extensions();

      if (exts.count(additional_sigs_eid) > 0) {
         auto& additional_sigs = std::get<additional_block_signatures_extension>(exts.lower_bound(additional_sigs_eid)->second);
         return std::move(additional_sigs.signatures);
      }

      return {};
   }

   /**
    *  Reference cannot outlive header_exts. Assumes header_exts is not mutated after instantiation.
    */
   inline const vector<digest_type>& get_new_protocol_feature_activations(const header_extension_multimap& header_exts) {
      static const vector<digest_type> no_activations{};

      if( header_exts.count(protocol_feature_activation::extension_id()) == 0 )
         return no_activations;

      return std::get<protocol_feature_activation>(header_exts.lower_bound(protocol_feature_activation::extension_id())->second).protocol_features;
   }

} /// namespace eosio::chain

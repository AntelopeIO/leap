#pragma once

#include <eosio/chain/hotstuff/hotstuff.hpp>
#include <eosio/chain/types.hpp>
#include <eosio/chain/name.hpp>
#include <eosio/chain/hotstuff/finalizer_policy.hpp>

namespace eosio::chain {

   // Abstract pacemaker; a reference of this type will only be used by qc_chain, as qc_chain
   //   cannot know which environment it is in.
   // All other pacemaker clients will be interacting with a reference to the concrete class:
   // - Testers will access a test_pacemaker reference;
   // - Real-world code will access a chain_pacemaker reference.
   class base_pacemaker {
   public:

      virtual ~base_pacemaker() = default;

      //TODO: discuss
#warning discuss
      virtual uint32_t get_quorum_threshold() = 0;

      virtual block_id_type get_current_block_id() = 0;

      virtual name get_proposer() = 0;
      virtual name get_leader() = 0;
      virtual name get_next_leader() = 0;
      virtual const finalizer_policy& get_finalizer_policy() = 0;

      //outbound communications; 'id' is the producer name (can be ignored if/when irrelevant to the implementer)
      virtual void send_hs_proposal_msg(const hs_proposal_message& msg, const std::string& id, const std::optional<uint32_t>& exclude_peer = std::nullopt) = 0;
      virtual void send_hs_vote_msg(const vote_message& msg, const std::string& id, const std::optional<uint32_t>& exclude_peer = std::nullopt) = 0;
      virtual void send_hs_new_view_msg(const hs_new_view_message& msg, const std::string& id, const std::optional<uint32_t>& exclude_peer = std::nullopt) = 0;

      virtual void send_hs_message_warning(uint32_t sender_peer, hs_message_warning code) = 0;

   };

} // namespace eosio::hotstuff

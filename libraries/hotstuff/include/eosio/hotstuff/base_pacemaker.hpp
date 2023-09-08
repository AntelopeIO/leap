#pragma once

#include <eosio/chain/types.hpp>
#include <eosio/chain/name.hpp>
#include <eosio/chain/hotstuff.hpp>

#include <vector>

namespace eosio::chain {
   struct hs_proposal_message;
   struct hs_vote_message;
   struct hs_new_view_message;
   struct hs_new_block_message;
}

namespace eosio::hotstuff {

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

      virtual chain::block_id_type get_current_block_id() = 0;

      virtual chain::name get_proposer() = 0;
      virtual chain::name get_leader() = 0;
      virtual chain::name get_next_leader() = 0;
      virtual std::vector<chain::name> get_finalizers() = 0;

      //outbound communications; 'id' is the producer name (can be ignored if/when irrelevant to the implementer)
      virtual void send_hs_msg(const eosio::chain::hs_message& msg, chain::name id) = 0;
   };

} // namespace eosio::hotstuff

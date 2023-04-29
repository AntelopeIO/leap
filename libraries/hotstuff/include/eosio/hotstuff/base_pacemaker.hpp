#pragma once

#include <eosio/chain/hotstuff.hpp>
//#include <eosio/hotstuff/qc_chain.hpp>
#include <eosio/chain/name.hpp>
#include <eosio/chain/producer_schedule.hpp>
#include <eosio/chain/block_state.hpp>

using namespace eosio::chain;

namespace eosio { namespace hotstuff {

   class qc_chain;

   // Abstract pacemaker; a reference of this type will only be used by qc_chain, as qc_chain
   //   cannot know which environment it is in.
   // All other pacemaker clients will be interacting with a reference to the concrete class:
   // - Testers will access a test_pacemaker reference;
   // - Real-world code will access a chain_pacemaker reference.
   class base_pacemaker {
   public:

      //TODO: discuss
      virtual uint32_t get_quorum_threshold() = 0;

      virtual block_id_type get_current_block_id() = 0;

      //hotstuff getters. todo : implement relevant setters as host functions
      virtual name get_proposer() = 0;
      virtual name get_leader() = 0;
      virtual name get_next_leader() = 0;
      virtual std::vector<name> get_finalizers() = 0;

      //outbound communications; 'id' is the producer name (can be ignored if/when irrelevant to the implementer)
      virtual void send_hs_proposal_msg(const hs_proposal_message & msg, name id) = 0;
      virtual void send_hs_vote_msg(const hs_vote_message & msg, name id) = 0;
      virtual void send_hs_new_view_msg(const hs_new_view_message & msg, name id) = 0;
      virtual void send_hs_new_block_msg(const hs_new_block_message & msg, name id) = 0;
   };

}}

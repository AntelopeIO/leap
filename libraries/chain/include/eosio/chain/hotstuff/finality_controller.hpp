#pragma once

#include <eosio/chain/hotstuff/hotstuff.hpp>
#include <eosio/chain/block_state.hpp>

namespace eosio::chain {
   struct finality_controller_impl;

   class finality_controller {
      public:
         finality_controller();
         ~finality_controller();

         bool aggregate_vote(const block_state_ptr& bsp, const hs_vote_message& vote);

      private:
         std::unique_ptr<finality_controller_impl> my;
   };

} // eosio::chain

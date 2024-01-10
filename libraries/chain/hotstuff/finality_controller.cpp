#include <eosio/chain/hotstuff/finality_controller.hpp>

namespace eosio::chain {

struct finality_controller_impl {
   bool aggregate_vote(const block_state_ptr& bsp, const hs_vote_message& vote) {
      const auto& finalizers = bsp->finalizer_policy->finalizers;
      auto it = std::find_if(finalizers.begin(),
                             finalizers.end(),
                             [&](const auto& finalizer) { return finalizer.public_key == vote.finalizer_key; });

      if (it != finalizers.end()) {
         auto index = std::distance(finalizers.begin(), it);
         return bsp->pending_qc.add_vote( vote.strong,
                                          index,
                                          vote.finalizer_key,
                                          vote.sig );
      } else {
         wlog( "finalizer_key (${k}) in vote is not in finalizer policy", ("k", vote.finalizer_key) );
         return false;
      }
   }
}; // finality_controller_impl

finality_controller::finality_controller()
:my( new finality_controller_impl() )
{
}

finality_controller::~finality_controller()
{
}

bool finality_controller::aggregate_vote(const block_state_ptr& bsp, const hs_vote_message& vote) {
   return my->aggregate_vote(bsp, vote);
}

} // namespace eosio::chain

#include <eosio/chain/hotstuff/finalizer.hpp>

namespace eosio::chain {

block_state_ptr get_block_by_height(const fork_database_if_t::branch_type& branch, uint32_t block_num) {
   auto it = std::find_if(branch.begin(), branch.end(),
                          [&](const block_state_ptr& bsp) { return bsp->block_num() == block_num; });
   return it == branch.end() ? block_state_ptr{} : *it;
}

qc_chain_t finalizer::get_qc_chain(const block_state_ptr& proposal, const fork_database_if_t::branch_type& branch) const {
   qc_chain_t res;

   // get b2
   // ------
   auto it2 = std::find_if(branch.begin(), branch.end(),
                           [t=proposal->core.last_qc_block_num](const block_state_ptr& bsp) { return bsp->block_num() == t; });
   if (it2 == branch.end())
      return res;
   res.b2 = *it2;

   // get b1
   // ------
   auto it1 = std::find_if(++it2, branch.end(),
                           [t=res.b2->core.last_qc_block_num](const block_state_ptr& bsp) { return bsp->block_num() == t; });
   if (it1 == branch.end())
      return res;
   res.b1 = *it1;

   // get b
   // ------
   auto it = std::find_if(++it1, branch.end(),
                          [t=res.b1->core.last_qc_block_num](const block_state_ptr& bsp) { return bsp->block_num() == t; });
   if (it == branch.end())
      return res;
   res.b = *it;

   return res;
}

bool extends(const fork_database_if_t& fork_db, const block_state_ptr& descendant, const block_id_type& ancestor) {
   if (ancestor.empty())
      return false;
   auto cur = fork_db.get_block(descendant->previous());
   while (cur) {
      if (cur->id() == ancestor)
         return true;
      cur = fork_db.get_block(cur->previous());
   }
   return false;
}

finalizer::VoteDecision finalizer::decide_vote(const block_state_ptr& p, const fork_database_if_t& fork_db) {
   bool safety_check   = false;
   bool liveness_check = false;

   auto p_branch = fork_db.fetch_branch(p->id());

   qc_chain_t chain = get_qc_chain(p, p_branch);

   // we expect last_qc_block_num() to always be found except at bootstrap
   // in `assemble_block()`, if we don't find a qc in the ancestors of the proposed block, we use block_num from fork_db.root()
   // and it was weak.
   auto bsp_last_qc   = p->last_qc_block_num() ? get_block_by_height(p_branch, *p->last_qc_block_num()) : block_state_ptr{};

   bool monotony_check = !fsi.last_vote || p->timestamp() > fsi.last_vote.timestamp;
   // !fsi.last_vote means we have never voted on a proposal, so the protocol feature just activated and we can proceed

   if (!fsi.lock.empty()) {
      // Safety check : check if this proposal extends the proposal we're locked on
      // --------------------------------------------------------------------------
      if (extends(fork_db, p, fsi.lock.id))
         safety_check = true;

      // Liveness check : check if the height of this proposal's justification is higher
      // than the height of the proposal I'm locked on.
      // This allows restoration of liveness if a replica is locked on a stale proposal
      // -------------------------------------------------------------------------------
      if (bsp_last_qc && bsp_last_qc->timestamp() > fsi.lock.timestamp)
         liveness_check = true;
   } else {
      // Safety and Liveness both fail if `fsi.lock` is empty. It should not happen.
      // `fsi.lock` is initially set to `lib` when switching to IF or starting from a snapshot.
      // -------------------------------------------------------------------------------------
      liveness_check = false;
      safety_check   = false;
   }


   // Figure out if we can vote and wether our vote will be strong or weak
   // --------------------------------------------------------------------
   VoteDecision my_vote = VoteDecision::NoVote;

   if (bsp_last_qc && monotony_check && (liveness_check || safety_check)) {
      auto requested_vote_range = time_range_t { bsp_last_qc->timestamp(), p->timestamp() };

      bool time_range_disjoint =
         fsi.last_vote_range.start > requested_vote_range.end || fsi.last_vote_range.end < requested_vote_range.start;

      // my last vote was on (t9, t10_1], I'm asked to vote on t10 :
      //                 t9 < t10 && t9 < t10_1;  // time_range_interference == true, correct
      //
      // my last vote was on (t9, t10_1], I'm asked to vote on t11 :
      //                 t9 < t11 && t10 < t10_1; // time_range_interference == false, correct
      //
      // my last vote was on (t7, t9], I'm asked to vote on t10 :
      //                 t7 < t10 && t9 < t9;     // time_range_interference == false, correct

      bool enough_for_strong_vote = time_range_disjoint || extends(fork_db, p, fsi.last_vote.id);

      fsi.last_vote = proposal_ref(p);                     // v_height

      if (chain.b1 && chain.b1->timestamp() > fsi.lock.timestamp)
         fsi.lock = proposal_ref(chain.b1);                // commit phase on b1

      fsi.last_vote_range = requested_vote_range;

      my_vote = enough_for_strong_vote ? VoteDecision::StrongVote : VoteDecision::WeakVote;
   }

   return my_vote;
}

} // namespace eosio::chain
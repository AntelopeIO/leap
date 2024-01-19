#include <eosio/chain/hotstuff/finalizer.hpp>

namespace eosio::chain {

block_state_ptr get_block_by_height(const fork_db_t::branch_type& branch, uint32_t block_num) {
   auto it = std::find_if(branch.begin(), branch.end(),
                          [&](const block_state_ptr& bsp) { return bsp->block_num() == block_num; });
   return it == branch.end() ? block_state_ptr{} : *it;
}

qc_chain finalizer::get_qc_chain(const block_state_ptr& proposal, const fork_db_t::branch_type& branch) const {
   qc_chain res;

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

bool extends(const fork_db_t& fork_db, const block_state_ptr& descendant, const block_state_ptr& ancestor) {
   if (!ancestor)
      return true;
   auto cur = fork_db.get_block(descendant->previous());
   while (cur) {
      if (cur == ancestor)
         return true;
      cur = fork_db.get_block(cur->previous());
   }
   return false;
}

finalizer::VoteDecision finalizer::decide_vote(const block_state_ptr& p, const fork_db_t& fork_db) {
   bool safety_check   = false;
   bool liveness_check = false;

   auto p_branch = fork_db.fetch_branch(p->id());

   qc_chain chain = get_qc_chain(p, p_branch);
   auto bsp_last_vote = fsi.last_vote.empty() ? block_state_ptr{} : fork_db.get_block(fsi.last_vote);

   bool monotony_check = !bsp_last_vote || p->timestamp() > bsp_last_vote->timestamp();
   // !bsp_last_vote means we have never voted on a proposal, so the protocol feature just activated and we can proceed

   auto bsp_last_qc = p->core.last_qc_block_num ? get_block_by_height(p_branch, *p->core.last_qc_block_num) : block_state_ptr{};
   auto bsp_lock    = fsi.lock_id.empty() ? block_state_ptr{} : fork_db.get_block(fsi.lock_id);
   if (bsp_lock) {
      assert(bsp_lock); // [if todo] can we assert that the lock_id block is always found in fork_db?

      // Safety check : check if this proposal extends the proposal we're locked on
      if (extends(fork_db, p, bsp_lock))
         safety_check = true;

      // Liveness check : check if the height of this proposal's justification is higher than the height
      // of the proposal I'm locked on. This allows restoration of liveness if a replica is locked on a stale proposal
      if (!bsp_last_qc || bsp_last_qc->timestamp() > bsp_lock->timestamp())
         liveness_check = true;
   } else {
      // if we're not locked on anything, means the protocol feature just activated and we can proceed
      liveness_check = true;
      safety_check   = true;
   }

   if (!bsp_last_qc)
      return VoteDecision::StrongVote; // [if todo] is this OK?

   else if (monotony_check && (liveness_check || safety_check)) {
      auto requested_vote_range = time_range_t { bsp_last_qc->timestamp(), p->timestamp() };

      bool time_range_interference =
         fsi.last_vote_range.start < requested_vote_range.end && requested_vote_range.start < fsi.last_vote_range.end;

      // my last vote was on (t9, t10_1], I'm asked to vote on t10 :
      //                 t9 < t10 && t9 < t10_1;  // time_range_interference == true, correct
      //
      // my last vote was on (t9, t10_1], I'm asked to vote on t11 :
      //                 t9 < t11 && t10 < t10_1; // time_range_interference == false, correct
      //
      // my last vote was on (t7, t9], I'm asked to vote on t10 :
      //                 t7 < t10 && t9 < t9;     // time_range_interference == false, correct

      bool enough_for_strong_vote = false;

      if (!time_range_interference || extends(fork_db, p, bsp_last_vote))
         enough_for_strong_vote = true;

      // fsi.is_last_vote_strong = enough_for_strong_vote;
      fsi.last_vote = p->id();         // v_height

      if (chain.b1 && (!bsp_lock || chain.b1->timestamp() > bsp_lock->timestamp()))
         fsi.lock_id = chain.b1->id(); // commit phase on b1

      fsi.last_vote_range = requested_vote_range;

      return enough_for_strong_vote ? VoteDecision::StrongVote : VoteDecision::WeakVote;
   }
   else
      return VoteDecision::NoVote;
}
}
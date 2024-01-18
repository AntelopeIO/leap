#include <eosio/chain/hotstuff/finalizer.hpp>

namespace eosio::chain {

block_state_ptr get_block_by_height(const fork_db_t::branch_type& branch, block_id_type block_id, uint32_t height) {

}

qc_chain finalizer::get_qc_chain(const block_state_ptr& proposal, const fork_db_t& fork_db) const {
   qc_chain res;

   auto branch = fork_db.fetch_branch(proposal->id());

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

finalizer::VoteDecision finalizer::decide_vote(const block_state_ptr& p, const fork_db_t& fork_db) const {
   bool monotony_check = false;
   bool safety_check   = false;
   bool liveness_check = false;

   qc_chain chain = get_qc_chain(p, fork_db);

    if (!fsi.last_vote.empty()) {
       if (p->timestamp() > fork_db.get_block(fsi.last_vote)->timestamp()) {
            monotony_check = true;
        }
    }
    else monotony_check = true; // if I have never voted on a proposal, means the protocol feature just activated and we can proceed
#if 0
    if (!fsi.lock_id.empty()) {
        //Safety check : check if this proposal extends the proposal we're locked on
       if (extends(p, fork_db.get_block(fsi.lock_id)))
            safety_check = true;

       //Liveness check : check if the height of this proposal's justification is higher than the height
       // of the proposal I'm locked on. This allows restoration of liveness if a replica is locked on a stale proposal
        if (fork_db.get_block_by_height(p.id(), p->core.last_qc_block_num)->timestamp() > fork_db.get_block(fsi.lock_id)->timestamp())) liveness_check = true;
    }
    else {
        //if we're not locked on anything, means the protocol feature just activated and we can proceed
        liveness_check = true;
        safety_check = true;
    }

    if (monotony_check && (liveness_check || safety_check)){

        uint32_t requested_vote_range_lower_bound = fork_db.get_block_by_height(p.block_id, p.last_qc_block_height)->timestamp();
        uint32_t requested_vote_range_upper_bound = p->timestamp();

        bool time_range_interference = fsi.last_vote_range_lower_bound < requested_vote_range_upper_bound && requested_vote_range_lower_bound < fsi.last_vote_range_upper_bound;

        //my last vote was on (t9, t10_1], I'm asked to vote on t10 : t9 < t10 && t9 < t10_1; //time_range_interference == true, correct
        //my last vote was on (t9, t10_1], I'm asked to vote on t11 : t9 < t11 && t10 < t10_1; //time_range_interference == false, correct
        //my last vote was on (t7, t9], I'm asked to vote on t10 : t7 < t10 && t9 < t9; //time_range_interference == false, correct

        bool enough_for_strong_vote = false;

        if (!time_range_interference || extends(p, fork_db.get_block(fsi.last_vote_block_ref)) enough_for_strong_vote = true;

        //fsi.is_last_vote_strong = enough_for_strong_vote;
        fsi.last_vote_block_ref = p.block_id; //v_height

        if (b1->timestamp() > fork_db.get_block(fsi.lock_id)->timestamp()) fsi.lock_id = b1.block_id; //commit phase on b1

        fsi.last_vote_range_lower_bound = requested_vote_range_lower_bound;
        fsi.last_vote_range_upper_bound = requested_vote_range_upper_bound;

        if (enough_for_strong_vote) return VoteDecision::StrongVote;
        else return VoteDecision::WeakVote;

    }
        else
#endif
       return VoteDecision::NoVote;

}


}
#pragma once

namespace eosio::chain {
// ----------------------------------------------------------------------------------------
template<class FORK_DB>
typename FORK_DB::bhsp
get_block_by_num(const typename FORK_DB::full_branch_type& branch, std::optional<uint32_t> block_num) {
   if (!block_num || branch.empty())
      return {};

   // a branch always contains consecutive block numbers, starting with the highest
   uint32_t first = branch[0]->block_num();
   uint32_t dist  = first - *block_num;
   return dist < branch.size() ? branch[dist] : typename FORK_DB::bhsp{};
}

// ----------------------------------------------------------------------------------------
template<class FORK_DB>
bool extends(const typename FORK_DB::full_branch_type& branch, const block_id_type& id)  {
   return !branch.empty() &&
      std::any_of(++branch.cbegin(), branch.cend(), [&](const auto& h) { return h->id() == id; });
}

// ----------------------------------------------------------------------------------------
template<class FORK_DB>
finalizer_tpl<FORK_DB>::vote_decision finalizer_tpl<FORK_DB>::decide_vote(const FORK_DB::bsp& proposal, const FORK_DB& fork_db) {
   bool safety_check   = false;
   bool liveness_check = false;

   bool monotony_check = !fsi.last_vote || proposal->timestamp() > fsi.last_vote.timestamp;
   // !fsi.last_vote means we have never voted on a proposal, so the protocol feature just activated and we can proceed

   if (!monotony_check) {
      dlog("monotony check failed for proposal ${p}, cannot vote", ("p", proposal->id()));
      return vote_decision::no_vote;
   }

   std::optional<full_branch_type> p_branch; // a branch that includes the root.

   if (!fsi.lock.empty()) {
      // Liveness check : check if the height of this proposal's justification is higher
      // than the height of the proposal I'm locked on.
      // This allows restoration of liveness if a replica is locked on a stale proposal
      // -------------------------------------------------------------------------------
      liveness_check = proposal->last_qc_block_timestamp() > fsi.lock.timestamp;

      if (!liveness_check) {
         // Safety check : check if this proposal extends the proposal we're locked on
         p_branch = fork_db.fetch_full_branch(proposal->id());
         safety_check = extends<FORK_DB>(*p_branch, fsi.lock.id);
      }
   } else {
      // Safety and Liveness both fail if `fsi.lock` is empty. It should not happen.
      // `fsi.lock` is initially set to `lib` when switching to IF or starting from a snapshot.
      // -------------------------------------------------------------------------------------
      liveness_check = false;
      safety_check   = false;
   }

   dlog("liveness_check=${l}, safety_check=${s}, monotony_check=${m}, can vote = {can_vote}",
        ("l",liveness_check)("s",safety_check)("m",monotony_check)("can_vote",(liveness_check || safety_check)));

   // Figure out if we can vote and wether our vote will be strong or weak
   // If we vote, update `fsi.last_vote` and also `fsi.lock` if we have a newer commit qc
   // -----------------------------------------------------------------------------------
   vote_decision decision = vote_decision::no_vote;

   if (liveness_check || safety_check) {
      auto [p_start, p_end] = std::make_pair(proposal->last_qc_block_timestamp(), proposal->timestamp());

      bool time_range_disjoint  = fsi.last_vote_range_start >= p_end || fsi.last_vote.timestamp <= p_start;
      bool voting_strong        = time_range_disjoint;
      if (!voting_strong) {
         if (!p_branch)
            p_branch = fork_db.fetch_full_branch(proposal->id());
         voting_strong = extends<FORK_DB>(*p_branch, fsi.last_vote.id);
      }

      fsi.last_vote             = proposal_ref(proposal);
      fsi.last_vote_range_start = p_start;

      if (!p_branch)
         p_branch = fork_db.fetch_full_branch(proposal->id());
      auto bsp_final_on_strong_qc =  get_block_by_num<FORK_DB>(*p_branch, proposal->final_on_strong_qc_block_num());
      if (voting_strong && bsp_final_on_strong_qc && bsp_final_on_strong_qc->timestamp() > fsi.lock.timestamp)
         fsi.lock = proposal_ref(bsp_final_on_strong_qc);

      decision = voting_strong ? vote_decision::strong_vote : vote_decision::weak_vote;
   } else {
      dlog("last_qc_block_num=${lqc}, fork_db root block_num=${f}",
           ("lqc",!!proposal->last_qc_block_num())("f",fork_db.root()->block_num()));
      if (proposal->last_qc_block_num())
         dlog("last_qc_block_num=${lqc}", ("lqc", proposal->last_qc_block_num()));
   }
   if (decision != vote_decision::no_vote)
      dlog("Voting ${s}", ("s", decision == vote_decision::strong_vote ? "strong" : "weak"));
   return decision;
}

// ----------------------------------------------------------------------------------------
template<class FORK_DB>
std::optional<vote_message> finalizer_tpl<FORK_DB>::maybe_vote(const bls_public_key& pub_key, const FORK_DB::bsp& p,
                                                               const digest_type& digest, const FORK_DB& fork_db) {
   finalizer::vote_decision decision = decide_vote(p, fork_db);
   if (decision == vote_decision::strong_vote || decision == vote_decision::weak_vote) {
      bls_signature sig;
      if (decision == vote_decision::weak_vote) {
         // if voting weak, the digest to sign should be a hash of the concatenation of the finalizer_digest
         // and the string "WEAK"
         sig =  priv_key.sign(create_weak_digest(digest));
      } else {
         sig =  priv_key.sign({(uint8_t*)digest.data(), (uint8_t*)digest.data() + digest.data_size()});
      }
      return vote_message{ p->id(), decision == vote_decision::strong_vote, pub_key, sig };
   }
   return {};
}
          
}
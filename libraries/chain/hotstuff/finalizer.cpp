#include <eosio/chain/hotstuff/finalizer.hpp>
#include <eosio/chain/exceptions.hpp>
#include <fc/log/logger_config.hpp>

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
   // and specify weak.
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

   dlog("liveness_check=${l}, safety_check=${s}, monotony_check=${m}", ("l",liveness_check)("s",safety_check)("m",monotony_check));

   return VoteDecision::StrongVote; // temporary

   // Figure out if we can vote and wether our vote will be strong or weak
   // If we vote, update `fsi.last_vote` and also `fsi.lock` if we have a newer commit qc
   // -----------------------------------------------------------------------------------
   VoteDecision my_vote = VoteDecision::NoVote;

   if (bsp_last_qc && monotony_check && (liveness_check || safety_check)) {
      auto [p_start, p_end] = std::make_pair(bsp_last_qc->timestamp(), p->timestamp());

      bool time_range_disjoint    = fsi.last_vote_range_start >= p_end || fsi.last_vote.timestamp <= p_start;

      bool enough_for_strong_vote = time_range_disjoint || extends(fork_db, p, fsi.last_vote.id);

      fsi.last_vote             = proposal_ref(p);          // v_height
      fsi.last_vote_range_start = p_start;

      if (chain.b1 && chain.b1->timestamp() > fsi.lock.timestamp)
         fsi.lock = proposal_ref(chain.b1);                // commit phase on b1

      my_vote = enough_for_strong_vote ? VoteDecision::StrongVote : VoteDecision::WeakVote;
   } else if (!bsp_last_qc &&  p->last_qc_block_num() && fork_db.root()->block_num() == *p->last_qc_block_num()) {
      // recovery mode (for example when we just switched to IF). Vote weak.
      my_vote = VoteDecision::WeakVote;
   }

   return my_vote;
}

std::optional<vote_message> finalizer::maybe_vote(const block_state_ptr& p, const digest_type& digest, const fork_database_if_t& fork_db) {
   finalizer::VoteDecision decision = decide_vote(p, fork_db);
   if (decision == VoteDecision::StrongVote || decision == VoteDecision::WeakVote) {
      //save_finalizer_safety_info();
      auto sig =  priv_key.sign(std::vector<uint8_t>(digest.data(), digest.data() + digest.data_size()));
      return vote_message{ p->id(), decision == VoteDecision::StrongVote, pub_key, sig };
   }
   return {};
}

void finalizer::save_finalizer_safety_info() {
   if (!safety_file.is_open()) {
      EOS_ASSERT(!safety_file_path.empty(), finalizer_safety_exception,
                 "path for storing finalizer safety persistence file not specified");
      safety_file.set_file_path(safety_file_path);
      safety_file.open(fc::cfile::create_or_update_rw_mode);
      EOS_ASSERT(safety_file.is_open(), finalizer_safety_exception,
                 "unable to open finalizer safety persistence file: ${p}", ("p", safety_file_path));
   }
   safety_file.seek(0);
   fc::raw::pack(safety_file, finalizer::safety_information::magic);
   fc::raw::pack(safety_file, fsi);
   safety_file.flush();
}


void finalizer::load_finalizer_safety_info() {
   EOS_ASSERT(!safety_file_path.empty(), finalizer_safety_exception,
              "path for storing finalizer safety persistence file not specified");

   EOS_ASSERT(!safety_file.is_open(), finalizer_safety_exception,
              "Trying to read an already open finalizer safety persistence file: ${p}", ("p", safety_file_path));
   safety_file.set_file_path(safety_file_path);
   safety_file.open(fc::cfile::update_rw_mode);
   EOS_ASSERT(safety_file.is_open(), finalizer_safety_exception,
              "unable to open finalizer safety persistence file: ${p}", ("p", safety_file_path));
   try {
      safety_file.seek(0);
      uint64_t magic = 0;
      fc::raw::unpack(safety_file, magic);
      EOS_ASSERT(magic == finalizer::safety_information::magic, finalizer_safety_exception,
                 "bad magic number in finalizer safety persistence file: ${p}", ("p", safety_file_path));
      fc::raw::unpack(safety_file, fsi);
   } catch (const fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   } catch (const std::exception& e) {
      edump((e.what()));
      throw;
   }
}

} // namespace eosio::chain
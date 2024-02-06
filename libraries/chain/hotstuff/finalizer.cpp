#include <eosio/chain/hotstuff/finalizer.hpp>
#include <eosio/chain/exceptions.hpp>
#include <fc/log/logger_config.hpp>

namespace eosio::chain {

// ----------------------------------------------------------------------------------------
block_state_ptr get_block_by_height(const fork_database_if_t::branch_type& branch, uint32_t block_num) {
   auto it = std::find_if(branch.begin(), branch.end(),
                          [&](const block_state_ptr& bsp) { return bsp->block_num() == block_num; });
   return it == branch.end() ? block_state_ptr{} : *it;
}

// ----------------------------------------------------------------------------------------
qc_chain_t finalizer::get_qc_chain(const block_state_ptr& proposal, const branch_type& branch) const {
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

// ----------------------------------------------------------------------------------------
bool extends(const fork_database_if_t& fork_db, const block_state_ptr& descendant, const block_id_type& ancestor) {
   if (ancestor.empty())
      return false;
   auto cur = fork_db.get_block_header(descendant->previous()); // use `get_block_header` so we can get the root
   while (cur) {
      if (cur->id == ancestor)
         return true;
      cur = fork_db.get_block_header(cur->previous());
   }
   return false;
}

// ----------------------------------------------------------------------------------------
finalizer::VoteDecision finalizer::decide_vote(const block_state_ptr& p, const fork_database_if_t& fork_db) {
   bool safety_check   = false;
   bool liveness_check = false;

   auto p_branch = fork_db.fetch_branch(p->id());

   qc_chain_t chain = get_qc_chain(p, p_branch);

   // we expect last_qc_block_num() to always be found except at bootstrap
   // in `assemble_block()`, if we don't find a qc in the ancestors of the proposed block, we use block_num
   // from fork_db.root(), and specify weak.
   auto bsp_last_qc = p->last_qc_block_num() ? get_block_by_height(p_branch, *p->last_qc_block_num()) : block_state_ptr{};

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

   dlog("liveness_check=${l}, safety_check=${s}, monotony_check=${m}",
        ("l",liveness_check)("s",safety_check)("m",monotony_check));

   // Figure out if we can vote and wether our vote will be strong or weak
   // If we vote, update `fsi.last_vote` and also `fsi.lock` if we have a newer commit qc
   // -----------------------------------------------------------------------------------
   VoteDecision decision = VoteDecision::NoVote;

   if (monotony_check && (liveness_check || safety_check)) {
      auto [p_start, p_end] = std::make_pair(bsp_last_qc ? bsp_last_qc->timestamp() : p->timestamp(),
                                             p->timestamp());

      bool time_range_disjoint    = fsi.last_vote_range_start >= p_end || fsi.last_vote.timestamp <= p_start;

      bool enough_for_strong_vote = time_range_disjoint || extends(fork_db, p, fsi.last_vote.id);

      fsi.last_vote             = proposal_ref(p);          // v_height
      fsi.last_vote_range_start = p_start;

      if (chain.b1 && chain.b1->timestamp() > fsi.lock.timestamp)
         fsi.lock = proposal_ref(chain.b1);                // commit phase on b1

      decision = enough_for_strong_vote ? VoteDecision::StrongVote : VoteDecision::WeakVote;
   } else {
      dlog("bsp_last_qc=${bsp}, last_qc_block_num=${lqc}, fork_db root block_num=${f}",
           ("bsp", !!bsp_last_qc)("lqc",!!p->last_qc_block_num())("f",fork_db.root()->block_num()));
      if (p->last_qc_block_num())
         dlog("last_qc_block_num=${lqc}", ("lqc", *p->last_qc_block_num()));
   }
   if (decision != VoteDecision::NoVote)
      dlog("Voting ${s}", ("s", decision == VoteDecision::StrongVote ? "strong" : "weak"));
   return decision;
}

// ----------------------------------------------------------------------------------------
std::optional<vote_message> finalizer::maybe_vote(const bls_public_key& pub_key, const block_state_ptr& p,
                                                  const digest_type& digest, const fork_database_if_t& fork_db) {
   finalizer::VoteDecision decision = decide_vote(p, fork_db);
   if (decision == VoteDecision::StrongVote || decision == VoteDecision::WeakVote) {
      bls_signature sig;
      if (decision == VoteDecision::WeakVote) {
         // if voting weak, the digest to sign should be a hash of the concatenation of the finalizer_digest
         // and the string "WEAK"
         sig =  priv_key.sign(create_weak_digest(digest));
      } else {
         sig =  priv_key.sign({(uint8_t*)digest.data(), (uint8_t*)digest.data() + digest.data_size()});
      }
      return vote_message{ p->id(), decision == VoteDecision::StrongVote, pub_key, sig };
   }
   return {};
}

// ----------------------------------------------------------------------------------------
void finalizer_set::save_finalizer_safety_info() const {

   if (!persist_file.is_open()) {
      EOS_ASSERT(!persist_file_path.empty(), finalizer_safety_exception,
                 "path for storing finalizer safety information file not specified");
      if (!std::filesystem::exists(persist_file_path.parent_path()))
          std::filesystem::create_directories(persist_file_path.parent_path());
      persist_file.set_file_path(persist_file_path);
      persist_file.open(fc::cfile::truncate_rw_mode);
      EOS_ASSERT(persist_file.is_open(), finalizer_safety_exception,
                 "unable to open finalizer safety persistence file: ${p}", ("p", persist_file_path));
   }
   try {
      static bool first_vote = true;
      persist_file.seek(0);
      fc::raw::pack(persist_file, finalizer::safety_information::magic);
      fc::raw::pack(persist_file, (uint64_t)finalizers.size());
      for (const auto& [pub_key, f] : finalizers) {
         fc::raw::pack(persist_file, pub_key);
         fc::raw::pack(persist_file, f.fsi);
      }
      if (first_vote) {
         // save also the fsi that was originally present in the file, but which applied to
         // finalizers not configured anymore.
         for (const auto& [pub_key, fsi] : inactive_safety_info) {
            fc::raw::pack(persist_file, pub_key);
            fc::raw::pack(persist_file, fsi);
         }
         first_vote = false;
      }
      persist_file.flush();
   } catch (const fc::exception& e) {
      edump((e.to_detail_string()));
      throw;
   } catch (const std::exception& e) {
      edump((e.what()));
      throw;
   }
}

// ----------------------------------------------------------------------------------------
finalizer_set::fsi_map finalizer_set::load_finalizer_safety_info() {
   fsi_map res;

   EOS_ASSERT(!persist_file_path.empty(), finalizer_safety_exception,
              "path for storing finalizer safety persistence file not specified");
   EOS_ASSERT(!persist_file.is_open(), finalizer_safety_exception,
              "Trying to read an already open finalizer safety persistence file: ${p}",
              ("p", persist_file_path));
   persist_file.set_file_path(persist_file_path);
   try {
      // if we can't open the finalizer safety file, we return an empty map.
      persist_file.open(fc::cfile::update_rw_mode);
   } catch(std::exception& e) {
      elog( "unable to open finalizer safety persistence file ${p}, using defaults. Exception: ${e}",
            ("p", persist_file_path)("e", e.what()));
      return res;
   } catch(...) {
      elog( "unable to open finalizer safety persistence file ${p}, using defaults", ("p", persist_file_path));
      return res;
   }
   EOS_ASSERT(persist_file.is_open(), finalizer_safety_exception,
              "unable to open finalizer safety persistence file: ${p}", ("p", persist_file_path));
   try {
      persist_file.seek(0);
      uint64_t magic = 0;
      fc::raw::unpack(persist_file, magic);
      EOS_ASSERT(magic == finalizer::safety_information::magic, finalizer_safety_exception,
                 "bad magic number in finalizer safety persistence file: ${p}", ("p", persist_file_path));
      uint64_t num_finalizers {0};
      fc::raw::unpack(persist_file, num_finalizers);
      for (size_t i=0; i<num_finalizers; ++i) {
         fsi_map::value_type entry;
         fc::raw::unpack(persist_file, entry.first);
         fc::raw::unpack(persist_file, entry.second);
         res.insert(entry);
      }
      persist_file.close();
   } catch (const fc::exception& e) {
      edump((e.to_detail_string()));
      // std::filesystem::remove(persist_file_path); // don't remove file we can't load
      throw;
   } catch (const std::exception& e) {
      edump((e.what()));
      // std::filesystem::remove(persist_file_path); // don't rremove file we can't load
      throw;
   }
   return res;
}

// ----------------------------------------------------------------------------------------
void finalizer_set::set_keys(const std::map<std::string, std::string>& finalizer_keys) {
   assert(finalizers.empty()); // set_keys should be called only once at startup
   if (finalizer_keys.empty())
      return;

   fsi_map safety_info = load_finalizer_safety_info();
   for (const auto& [pub_key_str, priv_key_str] : finalizer_keys) {
      auto public_key {bls_public_key{pub_key_str}};
      auto it  = safety_info.find(public_key);
      auto fsi = it != safety_info.end() ? it->second : default_safety_information();
      finalizers[public_key] = finalizer{bls_private_key{priv_key_str}, fsi};
   }

   // Now that we have updated the  finalizer_safety_info of our local finalizers,
   // remove these from the in-memory map. Whenever we save the finalizer_safety_info, we will
   // write the info for the local finalizers, and the first time we'll write the information for
   // currently inactive finalizers (which might be configured again in the future).
   //
   // So for every vote but the first, we'll only have to write the safety_info for the configured
   // finalizers.
   // --------------------------------------------------------------------------------------------
   for (const auto& [pub_key_str, priv_key_str] : finalizer_keys)
      safety_info.erase(bls_public_key{pub_key_str});

   // now only inactive finalizers remain in safety_info => move it to inactive_safety_info
   inactive_safety_info = std::move(safety_info);
}


// ----------------------------------------------------------------------------------------
finalizer::safety_information finalizer_set::default_safety_information() const {
   finalizer::safety_information res;
   return res;
}

// ----------------------------------------------------------------------------------------
void finalizer_set::finality_transition_notification(block_timestamp_type b1_time, block_id_type b1_id,
                                                     block_timestamp_type b2_time, block_id_type b2_id) {
   assert(t_startup < b1_time);
   for (auto& [pub_key, f] : finalizers) {
      // update only finalizers which are uninitialized
      if (!f.fsi.last_vote.empty())
         continue;

      f.fsi.last_vote = finalizer::proposal_ref(b1_id, b1_time);
      f.fsi.lock      = finalizer::proposal_ref(b2_id, b2_time);
   }
}

} // namespace eosio::chain
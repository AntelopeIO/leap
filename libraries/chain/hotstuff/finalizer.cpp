#include <eosio/chain/hotstuff/finalizer.hpp>
#include <eosio/chain/exceptions.hpp>
#include <fc/log/logger_config.hpp>

namespace eosio::chain {

// ----------------------------------------------------------------------------------------
finalizer::vote_result finalizer::decide_vote(const block_state_ptr& bsp) {
   vote_result res;

   res.monotony_check = fsi.last_vote.empty() || bsp->timestamp() > fsi.last_vote.timestamp;
   // fsi.last_vote.empty() means we have never voted on a proposal, so the protocol feature
   // just activated and we can proceed

   if (!res.monotony_check) {
      if (fsi.last_vote.empty()) {
         dlog("monotony check failed, block ${bn} ${p}, cannot vote, fsi.last_vote empty", ("bn", bsp->block_num())("p", bsp->id()));
      } else {
         if (fc::logger::get(DEFAULT_LOGGER).is_enabled(fc::log_level::debug)) {
            if (bsp->id() != fsi.last_vote.block_id) { // we may have already voted when we received the block
               dlog("monotony check failed, block ${bn} ${p}, cannot vote, ${t} <= ${lt}, fsi.last_vote ${lbn} ${lid}",
                    ("bn", bsp->block_num())("p", bsp->id())("t", bsp->timestamp())("lt", fsi.last_vote.timestamp)("lbn", fsi.last_vote.block_num())("lid", fsi.last_vote.block_id));
            }
         }
      }
      return res;
   }

   if (!fsi.lock.empty()) {
      // Liveness check : check if the height of this proposal's justification is higher
      // than the height of the proposal I'm locked on.
      // This allows restoration of liveness if a replica is locked on a stale proposal
      // -------------------------------------------------------------------------------
      res.liveness_check = bsp->core.latest_qc_block_timestamp() > fsi.lock.timestamp;

      if (!res.liveness_check) {
         dlog("liveness check failed, block ${bn} ${id}: ${c} <= ${l}, fsi.lock ${lbn} ${lid}, latest_qc_claim: ${qc}",
              ("bn", bsp->block_num())("id", bsp->id())("c", bsp->core.latest_qc_block_timestamp())("l", fsi.lock.timestamp)
              ("lbn", fsi.lock.block_num())("lid", fsi.lock.block_id)("qc", bsp->core.latest_qc_claim()));
         // Safety check : check if this proposal extends the proposal we're locked on
         res.safety_check = bsp->core.extends(fsi.lock.block_id);
         if (!res.safety_check) {
            dlog("safety  check  failed, block ${bn} ${id} did not extend fsi.lock ${lbn} ${lid}",
                 ("bn", bsp->block_num())("id", bsp->id())("lbn", fsi.lock.block_num())("lid", fsi.lock.block_id));
         }
      }
   } else {
      // Safety and Liveness both fail if `fsi.lock` is empty. It should not happen.
      // `fsi.lock` is initially set to `lib` when switching to IF or starting from a snapshot.
      // -------------------------------------------------------------------------------------
      wlog("liveness check & safety check failed, block ${bn} ${id}, fsi.lock is empty", ("bn", bsp->block_num())("id", bsp->id()));
      res.liveness_check = false;
      res.safety_check   = false;
   }

   bool can_vote = res.liveness_check || res.safety_check;

   // Figure out if we can vote and wether our vote will be strong or weak
   // If we vote, update `fsi.last_vote` and also `fsi.lock` if we have a newer commit qc
   // -----------------------------------------------------------------------------------
   if (can_vote) {
      auto [p_start, p_end] = std::make_pair(bsp->core.latest_qc_block_timestamp(), bsp->timestamp());

      bool time_range_disjoint  = fsi.last_vote_range_start >= p_end || fsi.last_vote.timestamp <= p_start;
      bool voting_strong        = time_range_disjoint;
      if (!voting_strong && !fsi.last_vote.empty()) {
         // we can vote strong if the proposal is a descendant of (i.e. extends) our last vote id
         voting_strong = bsp->core.extends(fsi.last_vote.block_id);
      }

      fsi.last_vote             = { bsp->id(), bsp->timestamp() };
      fsi.last_vote_range_start = p_start;

      auto& final_on_strong_qc_block_ref = bsp->core.get_block_reference(bsp->core.final_on_strong_qc_block_num);
      if (voting_strong && final_on_strong_qc_block_ref.timestamp > fsi.lock.timestamp) {
         fsi.lock = { final_on_strong_qc_block_ref.block_id, final_on_strong_qc_block_ref.timestamp };
      }

      res.decision = voting_strong ? vote_decision::strong_vote : vote_decision::weak_vote;
   }

   dlog("block=${bn} ${id}, liveness_check=${l}, safety_check=${s}, monotony_check=${m}, can vote=${can_vote}, voting=${v}, locked=${lbn} ${lid}",
        ("bn", bsp->block_num())("id", bsp->id())("l",res.liveness_check)("s",res.safety_check)("m",res.monotony_check)
        ("can_vote",can_vote)("v", res.decision)("lbn", fsi.lock.block_num())("lid", fsi.lock.block_id));
   return res;
}

// ----------------------------------------------------------------------------------------
std::optional<vote_message> finalizer::maybe_vote(const bls_public_key& pub_key,
                                                  const block_state_ptr& bsp,
                                                  const digest_type& digest) {
   finalizer::vote_decision decision = decide_vote(bsp).decision;
   if (decision == vote_decision::strong_vote || decision == vote_decision::weak_vote) {
      bls_signature sig;
      if (decision == vote_decision::weak_vote) {
         // if voting weak, the digest to sign should be a hash of the concatenation of the finalizer_digest
         // and the string "WEAK"
         sig =  priv_key.sign(create_weak_digest(digest));
      } else {
         sig =  priv_key.sign({(uint8_t*)digest.data(), (uint8_t*)digest.data() + digest.data_size()});
      }
      return std::optional{vote_message{ bsp->id(), decision == vote_decision::strong_vote, pub_key, sig }};
   }
   return {};
}

// ----------------------------------------------------------------------------------------
void my_finalizers_t::save_finalizer_safety_info() const {

   if (!persist_file.is_open()) {
      EOS_ASSERT(!persist_file_path.empty(), finalizer_safety_exception,
                 "path for storing finalizer safety information file not specified");
      if (!std::filesystem::exists(persist_file_path.parent_path()))
          std::filesystem::create_directories(persist_file_path.parent_path());
      persist_file.set_file_path(persist_file_path);
      persist_file.open(fc::cfile::truncate_rw_mode);
   }
   try {
      persist_file.seek(0);
      fc::raw::pack(persist_file, fsi_t::magic);
      fc::raw::pack(persist_file, (uint64_t)(finalizers.size() + inactive_safety_info.size()));
      for (const auto& [pub_key, f] : finalizers) {
         fc::raw::pack(persist_file, pub_key);
         fc::raw::pack(persist_file, f.fsi);
      }
      if (!inactive_safety_info_written) {
         // save also the fsi that was originally present in the file, but which applied to
         // finalizers not configured anymore.
         for (const auto& [pub_key, fsi] : inactive_safety_info) {
            fc::raw::pack(persist_file, pub_key);
            fc::raw::pack(persist_file, fsi);
         }
         inactive_safety_info_written = true;
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
my_finalizers_t::fsi_map my_finalizers_t::load_finalizer_safety_info() {
   fsi_map res;

   EOS_ASSERT(!persist_file_path.empty(), finalizer_safety_exception,
              "path for storing finalizer safety persistence file not specified");
   EOS_ASSERT(!persist_file.is_open(), finalizer_safety_exception,
              "Trying to read an already open finalizer safety persistence file: ${p}",
              ("p", persist_file_path));

   if (!std::filesystem::exists(persist_file_path)) {
      elog( "unable to open finalizer safety persistence file ${p}, file doesn't exist",
            ("p", persist_file_path));
      return res;
   }

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
   try {
      persist_file.seek(0);
      uint64_t magic = 0;
      fc::raw::unpack(persist_file, magic);
      EOS_ASSERT(magic == fsi_t::magic, finalizer_safety_exception,
                 "bad magic number in finalizer safety persistence file: ${p}", ("p", persist_file_path));
      uint64_t num_finalizers {0};
      fc::raw::unpack(persist_file, num_finalizers);
      for (size_t i=0; i<num_finalizers; ++i) {
         bls_public_key pubkey;
         fsi_t fsi;
         fc::raw::unpack(persist_file, pubkey);
         fc::raw::unpack(persist_file, fsi);
         res.emplace(pubkey, fsi);
      }
      persist_file.close();
   } catch (const fc::exception& e) {
      edump((e.to_detail_string()));
      // don't remove file we can't load
      throw;
   } catch (const std::exception& e) {
      edump((e.what()));
      // don't rremove file we can't load
      throw;
   }
   return res;
}

// ----------------------------------------------------------------------------------------
void my_finalizers_t::set_keys(const std::map<std::string, std::string>& finalizer_keys) {
   assert(finalizers.empty()); // set_keys should be called only once at startup
   if (finalizer_keys.empty())
      return;

   fsi_map safety_info = load_finalizer_safety_info();
   for (const auto& [pub_key_str, priv_key_str] : finalizer_keys) {
      auto public_key {bls_public_key{pub_key_str}};
      auto it  = safety_info.find(public_key);
      const auto& fsi = it != safety_info.end() ? it->second : default_fsi;
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


// --------------------------------------------------------------------------------------------
// Can be called either:
//   - when transitioning to IF (before any votes are to be sent)
//   - at leap startup, if we start at a block which is either within or past the IF transition.
// In either case, we are never updating existing finalizer safety information. This is only
// to ensure that the safety information will have defaults that ensure safety as much as
// possible, and allow for liveness which will allow the finalizers to eventually vote.
// --------------------------------------------------------------------------------------------
void my_finalizers_t::set_default_safety_information(const fsi_t& fsi) {
   for (auto& [pub_key, f] : finalizers) {
      // update only finalizers which are uninitialized
      if (!f.fsi.last_vote.empty() || !f.fsi.lock.empty())
         continue;

      f.fsi = fsi;
   }

   // save it in case set_keys called afterwards.
   default_fsi = fsi;
}

} // namespace eosio::chain

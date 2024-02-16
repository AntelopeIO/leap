#include <eosio/chain/hotstuff/finalizer.hpp>
#include <eosio/chain/exceptions.hpp>
#include <fc/log/logger_config.hpp>

#include <eosio/chain/hotstuff/finalizer.ipp> // implementation of finalizer methods

namespace eosio::chain {

// ----------------------------------------------------------------------------------------
// Explicit template instantiation
template struct finalizer_tpl<fork_database_if_t>;

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
#pragma once
#include <eosio/chain/hotstuff/hotstuff.hpp>
#include <eosio/chain/hotstuff/finalizer_policy.hpp>
#include <eosio/chain/fork_database.hpp>
#include <fc/crypto/bls_utils.hpp>
#include <fc/io/cfile.hpp>
#include <fc/reflect/reflect.hpp>
#include <fc/crypto/sha256.hpp>
#include <compare>
#include <utility>

// -------------------------------------------------------------------------------------------
// this file defines the classes:
//
// finalizer:
// ---------
//     - holds the bls12 private key which allows the finalizer to sign proposals (the
//       proposal is assumed to have been previously validated for correctness). These
//       signatures will be aggregated by block proposers into quorum certificates, which
//       are an essential part of the Savanna consensus algorithm.
//     - every time a finalizer votes, it may update its own safety info in memory
//     - finalizer safety info is appropriately initialized (iff not already present
//       in the persistent file) at Leap startup.
//
//  my_finalizers_t:
//  ---------------
//     - stores the set of finalizers currently active on this node.
//     - manages a `finalizer safety` file (`safety.dat`) which tracks the active finalizers
//       safety info (file is updated after each vote), and also the safety information for
//       every finalizer which has been active on this node (using the same `finalizer-dir`)
// -------------------------------------------------------------------------------------------

namespace eosio::chain {
   struct proposal_ref {
      block_id_type         id;
      block_timestamp_type  timestamp;

      proposal_ref() = default;

      proposal_ref(const block_id_type& id, block_timestamp_type t) :
         id(id), timestamp(t)
      {}

      template<class BSP>
      explicit proposal_ref(const BSP& p) :
         id(p->id()),
         timestamp(p->timestamp())
      {}

      void reset() {
         id = block_id_type();
         timestamp = block_timestamp_type();
      }

      bool empty() const { return id.empty(); }

      explicit operator bool() const { return !id.empty(); }

      bool operator==(const proposal_ref& o) const {
         return id == o.id && timestamp == o.timestamp;
      }
   };

   struct finalizer_safety_information {
      block_timestamp_type last_vote_range_start;
      proposal_ref         last_vote;
      proposal_ref         lock;

      static constexpr uint64_t magic = 0x5AFE11115AFE1111ull;

      static finalizer_safety_information unset_fsi() { return {block_timestamp_type(), {}, {}}; }

      bool operator==(const finalizer_safety_information& o) const {
         return last_vote_range_start == o.last_vote_range_start &&
            last_vote == o.last_vote &&
            lock == o.lock;
      }
   };

   // ----------------------------------------------------------------------------------------
   template<class FORK_DB>
   struct finalizer_tpl {
      enum class vote_decision { strong_vote, weak_vote, no_vote };

      bls_private_key               priv_key;
      finalizer_safety_information  fsi;

   private:
      using full_branch_type = FORK_DB::full_branch_type;

   public:
      vote_decision  decide_vote(const FORK_DB::bsp& proposal, const FORK_DB& fork_db);

      std::optional<vote_message> maybe_vote(const bls_public_key& pub_key, const FORK_DB::bsp& bsp,
                                             const digest_type& digest, const FORK_DB& fork_db);
   };

   using finalizer = finalizer_tpl<fork_database_if_t>;

   // ----------------------------------------------------------------------------------------
   struct my_finalizers_t {
      using fsi_t   = finalizer_safety_information;
      using fsi_map = std::map<bls_public_key, fsi_t>;

      const block_timestamp_type        t_startup;             // nodeos startup time, used for default safety_information
      const std::filesystem::path       persist_file_path;     // where we save the safety data
      mutable fc::datastream<fc::cfile> persist_file;          // we want to keep the file open for speed
      std::map<bls_public_key, finalizer>  finalizers;         // the active finalizers for this node
      fsi_map                           inactive_safety_info;  // loaded at startup, not mutated afterwards
      fsi_t                             default_fsi = fsi_t::unset_fsi(); // default provided at leap startup
      mutable bool                      inactive_safety_info_written{false};

      template<class F>
      void maybe_vote(const finalizer_policy& fin_pol,
                      const block_state_ptr& bsp,
                      const fork_database_if_t& fork_db,
                      const digest_type& digest,
                      F&& process_vote) {
         std::vector<vote_message> votes;
         votes.reserve(finalizers.size());

         // first accumulate all the votes
         for (const auto& f : fin_pol.finalizers) {
            if (auto it = finalizers.find(f.public_key); it != finalizers.end()) {
               std::optional<vote_message> vote_msg = it->second.maybe_vote(it->first, bsp, digest, fork_db);
               if (vote_msg)
                  votes.push_back(std::move(*vote_msg));
            }
         }
         // then save the safety info and, if successful, gossip the votes
         if (!votes.empty()) {
            save_finalizer_safety_info();
            for (const auto& vote : votes)
               std::forward<F>(process_vote)(vote);
         }
      }

      size_t  size() const { return finalizers.size(); }
      void    set_keys(const std::map<std::string, std::string>& finalizer_keys);
      void    set_default_safety_information(const fsi_t& fsi);

      // following two member functions could be private, but are used in testing
      void    save_finalizer_safety_info() const;
      fsi_map load_finalizer_safety_info();

      // for testing purposes only
      const fsi_t& get_fsi(const bls_public_key& k) { return finalizers[k].fsi; }
      void         set_fsi(const bls_public_key& k, const fsi_t& fsi) { finalizers[k].fsi = fsi; }
   };

}

namespace std {
   inline std::ostream& operator<<(std::ostream& os, const eosio::chain::proposal_ref& r) {
      os << "proposal_ref(id(" << r.id.str() << "), tstamp(" << r.timestamp.slot << "))";
      return os;
   }

   inline std::ostream& operator<<(std::ostream& os, const eosio::chain::finalizer_safety_information& fsi) {
      os << "fsi(" << fsi.last_vote_range_start.slot << ", " << fsi.last_vote << ", " << fsi.lock << ")";
      return os;
   }
}

FC_REFLECT(eosio::chain::proposal_ref, (id)(timestamp))
FC_REFLECT(eosio::chain::finalizer_safety_information, (last_vote_range_start)(last_vote)(lock))
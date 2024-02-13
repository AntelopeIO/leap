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

namespace eosio::chain {
   // ----------------------------------------------------------------------------------------
   struct qc_chain_t {
      block_state_ptr b2; // first phase,  prepare
      block_state_ptr b1; // second phase, precommit
      block_state_ptr b;  // third phase,  commit
   };

   // ----------------------------------------------------------------------------------------
   struct finalizer {
      enum class vote_decision { strong_vote, weak_vote, no_vote };

      struct proposal_ref {
         block_id_type         id;
         block_timestamp_type  timestamp;

         proposal_ref() = default;

         template<class Bsp>
         proposal_ref(const Bsp& p) :
            id(p->id()),
            timestamp(p->timestamp())
         {}

         proposal_ref(const block_id_type& id, block_timestamp_type t) :
            id(id), timestamp(t)
         {}

         void reset() {
            id = block_id_type();
            timestamp = block_timestamp_type();
         }

         bool empty() const { return id.empty(); }

         operator bool() const { return !id.empty(); }

         auto operator==(const proposal_ref& o) const {
            return id == o.id && timestamp == o.timestamp;
         }
      };

      struct safety_information {
         block_timestamp_type last_vote_range_start;
         proposal_ref         last_vote;          // v_height under hotstuff
         proposal_ref         lock;               // b_lock under hotstuff

         static constexpr uint64_t magic = 0x5AFE11115AFE1111ull;

         static safety_information unset_fsi() { return {block_timestamp_type(), {}, {}}; }

         auto operator==(const safety_information& o) const {
            return last_vote_range_start == o.last_vote_range_start &&
               last_vote == o.last_vote &&
               lock == o.lock;
         }
      };

      bls_private_key           priv_key;
      safety_information        fsi;

   private:
      using branch_type = fork_database_if_t::branch_type;
      qc_chain_t     get_qc_chain(const block_state_ptr& proposal, const branch_type& branch) const;
      vote_decision  decide_vote(const block_state_ptr& proposal, const fork_database_if_t& fork_db);

   public:
      std::optional<vote_message> maybe_vote(const bls_public_key& pub_key, const block_state_ptr& bsp,
                                             const digest_type& digest, const fork_database_if_t& fork_db);
   };

   // ----------------------------------------------------------------------------------------
   struct finalizer_set {
      using fsi_t   = finalizer::safety_information;
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
   inline std::ostream& operator<<(std::ostream& os, const eosio::chain::finalizer::proposal_ref& r) {
      os << "proposal_ref(id(" << r.id.str() << "), tstamp(" << r.timestamp.slot << "))";
      return os;
   }

   inline std::ostream& operator<<(std::ostream& os, const eosio::chain::finalizer::safety_information& fsi) {
      os << "fsi(" << fsi.last_vote_range_start.slot << ", " << fsi.last_vote << ", " << fsi.lock << ")";
      return os;
   }
}

FC_REFLECT(eosio::chain::finalizer::proposal_ref, (id)(timestamp))
FC_REFLECT(eosio::chain::finalizer::safety_information, (last_vote_range_start)(last_vote)(lock))
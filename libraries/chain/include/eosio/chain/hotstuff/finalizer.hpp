#pragma once
#include <eosio/chain/hotstuff/hotstuff.hpp>
#include <eosio/chain/hotstuff/finalizer_policy.hpp>
#include <eosio/chain/fork_database.hpp>
#include <fc/io/cfile.hpp>
#include <fc/reflect/reflect.hpp>
#include <fc/crypto/sha256.hpp>
#include <compare>
#include <utility>

namespace eosio::chain {
   struct qc_chain_t {
      block_state_ptr b2; // first phase,  prepare
      block_state_ptr b1; // second phase, precommit
      block_state_ptr b;  // third phase,  commit
   };

   struct finalizer {
      enum class VoteDecision { StrongVote, WeakVote, NoVote };

      struct proposal_ref {
         block_id_type         id;
         block_timestamp_type  timestamp;

         proposal_ref() = default;

         proposal_ref(const block_state_ptr& p) :
            id(p->id()),
            timestamp(p->timestamp())
         {}

         void reset() {
            id = block_id_type();
            timestamp = block_timestamp_type();
         }

         bool empty() const { return id.empty(); }

         operator bool() const { return id.empty(); }
      };

      struct safety_information {
         block_timestamp_type last_vote_range_start;
         proposal_ref         last_vote;          // v_height under hotstuff
         proposal_ref         lock;               // b_lock under hotstuff
         bool                 recovery_mode;

         static constexpr uint64_t magic = 0x5AFE11115AFE1111ull;

         safety_information() = default;
      };

      bls_public_key            pub_key;
      bls_private_key           priv_key;
      safety_information        fsi;

   private:
      qc_chain_t     get_qc_chain(const block_state_ptr&  proposal, const fork_database_if_t::branch_type& branch) const;
      VoteDecision   decide_vote(const block_state_ptr& proposal, const fork_database_if_t& fork_db);

   public:
      std::optional<vote_message> maybe_vote(const block_state_ptr& bsp, const digest_type& digest,
                                             const fork_database_if_t& fork_db);

      friend std::strong_ordering operator<=>(const finalizer& a, const finalizer& b) {
         return a.pub_key <=> b.pub_key;
      }

      friend std::strong_ordering operator<=>(const finalizer& a, const bls_public_key& k) {
         return a.pub_key <=> k;
      }
   };

   struct finalizer_set {
      const std::filesystem::path      persist_file_path;
      std::set<finalizer, std::less<>> finalizers;
      fc::datastream<fc::cfile>        persist_file;

      template<class F>
      void maybe_vote(const finalizer_policy &fin_pol,
                      const block_state_ptr& bsp,
                      const fork_database_if_t& fork_db,
                      const digest_type& digest,
                      F&& process_vote) {
         std::vector<vote_message> votes;
         votes.reserve(finalizers.size());

         // first accumulate all the votes
         for (const auto& f : fin_pol.finalizers) {
            if (auto it = finalizers.find(f.public_key); it != finalizers.end()) {
               std::optional<vote_message> vote_msg = const_cast<finalizer&>(*it).maybe_vote(bsp, digest, fork_db);
               if (vote_msg)
                  votes.push_back(std::move(*vote_msg));
            }
         }
         // then save the safety info and, if successful, gossip the votes
         if (!votes.empty()) {
            try {
               save_finalizer_safety_info();
               for (const auto& vote : votes)
                  std::forward<F>(process_vote)(vote);
            } catch(...) {
               throw;
            }
         }
      }

      void set_keys(const std::map<std::string, std::string>& finalizer_keys) {
         finalizers.clear();
         if (finalizer_keys.empty())
            return;

         fsi_map safety_info = load_finalizer_safety_info();
         for (const auto& [pub_key_str, priv_key_str] : finalizer_keys) {
            auto public_key {bls_public_key{pub_key_str}};
            auto it  = safety_info.find(public_key);
            auto fsi = it != safety_info.end() ? it->second : finalizer::safety_information{};
            finalizers.insert(finalizer{public_key, bls_private_key{priv_key_str}, fsi});
         }
      }

   private:
      using fsi_map = std::map<bls_public_key, finalizer::safety_information>;

      void    save_finalizer_safety_info();
      fsi_map load_finalizer_safety_info();
   };

}

FC_REFLECT(eosio::chain::finalizer::proposal_ref, (id)(timestamp))
FC_REFLECT(eosio::chain::finalizer::safety_information, (last_vote_range_start)(last_vote)(lock))
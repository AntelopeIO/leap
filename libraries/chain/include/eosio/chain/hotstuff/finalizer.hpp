#pragma once
#include <eosio/chain/hotstuff/hotstuff.hpp>
#include <eosio/chain/fork_database.hpp>
#include <fc/io/cfile.hpp>
#include <fc/reflect/reflect.hpp>
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
      std::filesystem::path     safety_file_path;
      fc::datastream<fc::cfile> safety_file;

   private:
      qc_chain_t     get_qc_chain(const block_state_ptr&  proposal, const fork_database_if_t::branch_type& branch) const;
      VoteDecision   decide_vote(const block_state_ptr& proposal, const fork_database_if_t& fork_db);
      void           save_finalizer_safety_info();
      void           load_finalizer_safety_info();

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
      const std::filesystem::path persist_dir;
      std::set<finalizer, std::less<>> finalizers;

      template<class F>
      void vote_if_found(const block_state_ptr& bsp,
                         const fork_database_if_t& fork_db,
                         const bls_public_key& pub_key,
                         const digest_type& digest,
                         F&& f) {

         if (auto it = finalizers.find(pub_key); it != finalizers.end()) {
            std::optional<vote_message> vote_msg = const_cast<finalizer&>(*it).maybe_vote(bsp, digest, fork_db);
            if (vote_msg)
               std::forward<F>(f)(*vote_msg);
         }
      }

      void reset(const std::map<std::string, std::string>& finalizer_keys) {
         finalizers.clear();
         for (const auto& [pub_key_str, priv_key_str] : finalizer_keys) {
            std::filesystem::path safety_file_path = persist_dir / (std::string("finalizer_safety_") + pub_key_str);
            finalizers.insert(finalizer{bls_public_key{pub_key_str}, bls_private_key{priv_key_str}, {}, safety_file_path});
         }
      }

   };

}

FC_REFLECT(eosio::chain::finalizer::proposal_ref, (id)(timestamp))
FC_REFLECT(eosio::chain::finalizer::safety_information, (last_vote_range_start)(last_vote)(lock))
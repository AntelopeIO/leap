#pragma once
#include <eosio/chain/hotstuff/hotstuff.hpp>
#include <eosio/chain/fork_database.hpp>
#include <compare>

namespace eosio::chain {
   using fork_db_t = fork_database<block_state_ptr>;

   struct qc_chain_t {
      block_state_ptr b2; // first phase,  prepare
      block_state_ptr b1; // second phase, precommit
      block_state_ptr b;  // third phase,  commit
   };

   struct finalizer {
      enum class VoteDecision { StrongVote, WeakVote, NoVote };

      struct time_range_t {
         block_timestamp_type start;
         block_timestamp_type end;
      };

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
         time_range_t     last_vote_range;
         proposal_ref     last_vote;          // v_height under hotstuff
         proposal_ref     lock;               // b_lock under hotstuff
         bool             recovery_mode;

         safety_information() = default;
      };

      bls_public_key      pub_key;
      bls_private_key     priv_key;
      safety_information  fsi;

   private:
      qc_chain_t   get_qc_chain(const block_state_ptr&  proposal, const fork_db_t::branch_type& branch) const;
      VoteDecision decide_vote(const block_state_ptr& proposal, const fork_db_t& fork_db);

   public:

      std::strong_ordering operator<=>(const finalizer& o) const
      {
         return pub_key <=> o.pub_key;
      }
   };


   struct finalizer_set {
      std::set<finalizer> finalizers;

      template<class F>
      void vote_if_found(const block_id_type& proposal_id,
                         const bls_public_key& pub_key,
                         const digest_type& digest,
                         F&& f) {

         if (auto it = std::find_if(finalizers.begin(), finalizers.end(), [&](const finalizer& fin) { return fin.pub_key == pub_key; });
             it != finalizers.end()) {
            const auto& priv_key = it->priv_key;
            auto sig =  priv_key.sign(std::vector<uint8_t>(digest.data(), digest.data() + digest.data_size()));

#warning use decide_vote() for strong after it is implementd by https://github.com/AntelopeIO/leap/issues/2070
            bool strong = true;
            hs_vote_message vote{ proposal_id, strong, pub_key, sig };
            std::forward<F>(f)(vote);
         }
      }

      void reset(const std::map<std::string, std::string>& finalizer_keys) {
         finalizers.clear();
         for (const auto& [pub_key_str, priv_key_str] : finalizer_keys)
            finalizers.insert(finalizer{bls_public_key{pub_key_str}, bls_private_key{priv_key_str}, {}});
      }

   };

}
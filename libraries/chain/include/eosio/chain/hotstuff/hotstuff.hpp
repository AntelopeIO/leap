#pragma once

#include <eosio/chain/block_timestamp.hpp>
#include <eosio/chain/finality_core.hpp>
#include <fc/crypto/bls_private_key.hpp>
#include <fc/crypto/bls_public_key.hpp>
#include <fc/crypto/bls_signature.hpp>

#include <boost/dynamic_bitset.hpp>

#include <mutex>

namespace eosio::chain {

   using bls_public_key          = fc::crypto::blslib::bls_public_key;
   using bls_signature           = fc::crypto::blslib::bls_signature;
   using bls_aggregate_signature = fc::crypto::blslib::bls_aggregate_signature;
   using bls_private_key         = fc::crypto::blslib::bls_private_key;

   using hs_bitset     = boost::dynamic_bitset<uint32_t>;
   using bls_key_map_t = std::map<bls_public_key, bls_private_key>;

   struct vote_message {
      block_id_type                       block_id;
      bool                                strong{false};
      bls_public_key                      finalizer_key;
      bls_signature                       sig;
   };

   enum class vote_status {
      success,
      duplicate,
      unknown_public_key,
      invalid_signature,
      unknown_block
   };

   using bls_public_key  = fc::crypto::blslib::bls_public_key;
   using bls_signature   = fc::crypto::blslib::bls_signature;
   using bls_private_key = fc::crypto::blslib::bls_private_key;

   // valid_quorum_certificate
   struct valid_quorum_certificate {
      bool is_weak()   const { return !!_weak_votes; }
      bool is_strong() const { return !_weak_votes; }

      std::optional<hs_bitset> _strong_votes;
      std::optional<hs_bitset> _weak_votes;
      bls_aggregate_signature  _sig;
   };

   // quorum_certificate
   struct quorum_certificate {
      uint32_t                 block_num;
      valid_quorum_certificate qc;

      qc_claim_t to_qc_claim() const {
         return {.block_num = block_num, .is_strong_qc = qc.is_strong()};
      }
   };

   struct qc_data_t {
      std::optional<quorum_certificate> qc; // Comes either from traversing branch from parent and calling get_best_qc()
                                            // or from an incoming block extension.
      qc_claim_t qc_claim;                  // describes the above qc. In rare cases (bootstrap, starting from snapshot,
                                            // disaster recovery), we may not have a qc so we use the `lib` block_num
                                            // and specify `weak`.
   };

   // pending_quorum_certificate
   class pending_quorum_certificate {
   public:
      enum class state_t {
         unrestricted,  // No quorum reached yet, still possible to achieve any state.
         restricted,    // Enough `weak` votes received to know it is impossible to reach the `strong` state.
         weak_achieved, // Enough `weak` + `strong` votes for a valid `weak` QC, still possible to reach the `strong` state.
         weak_final,    // Enough `weak` + `strong` votes for a valid `weak` QC, `strong` not possible anymore.
         strong         // Enough `strong` votes to have a valid `strong` QC
      };

      struct votes_t : fc::reflect_init {
      private:
         friend struct fc::reflector<votes_t>;
         friend struct fc::reflector_init_visitor<votes_t>;
         friend struct fc::has_reflector_init<votes_t>;
         friend class pending_quorum_certificate;

         hs_bitset               _bitset;
         bls_aggregate_signature _sig;
         std::vector<std::atomic<bool>> _processed; // avoid locking mutex for _bitset duplicate check

         void reflector_init();
      public:
         explicit votes_t(size_t num_finalizers)
            : _bitset(num_finalizers)
            , _processed(num_finalizers) {}

         // thread safe
         bool has_voted(size_t index) const;

         vote_status add_vote(size_t index, const bls_signature& sig);
      };

      pending_quorum_certificate();

      explicit pending_quorum_certificate(size_t num_finalizers, uint64_t quorum, uint64_t max_weak_sum_before_weak_final);

      // thread safe
      bool is_quorum_met() const;
      static bool is_quorum_met(state_t s) {
         return s == state_t::strong || s == state_t::weak_achieved || s == state_t::weak_final;
      }

      // thread safe
      vote_status add_vote(block_num_type block_num,
                           bool strong,
                           std::span<const uint8_t> proposal_digest,
                           size_t index,
                           const bls_public_key& pubkey,
                           const bls_signature& sig,
                           uint64_t weight);

      // thread safe
      bool has_voted(size_t index) const;

      state_t state() const { std::lock_guard g(*_mtx); return _state; };

      std::optional<quorum_certificate> get_best_qc(block_num_type block_num) const;
      void set_valid_qc(const valid_quorum_certificate& qc);
      bool valid_qc_is_strong() const;
   private:
      friend struct fc::reflector<pending_quorum_certificate>;
      friend class qc_chain;
      std::unique_ptr<std::mutex> _mtx;
      std::optional<valid_quorum_certificate> _valid_qc; // best qc received from the network inside block extension
      uint64_t             _quorum {0};
      uint64_t             _max_weak_sum_before_weak_final {0}; // max weak sum before becoming weak_final
      state_t              _state { state_t::unrestricted };
      uint64_t             _strong_sum {0}; // accumulated sum of strong votes so far
      uint64_t             _weak_sum {0}; // accumulated sum of weak votes so far
      votes_t              _weak_votes {0};
      votes_t              _strong_votes {0};

      // called by add_vote, already protected by mutex
      vote_status add_strong_vote(size_t index,
                                  const bls_signature& sig,
                                  uint64_t weight);

      // called by add_vote, already protected by mutex
      vote_status add_weak_vote(size_t index,
                                const bls_signature& sig,
                                uint64_t weight);

      bool is_quorum_met_no_lock() const;
      bool has_voted_no_lock(bool strong, size_t index) const;
      valid_quorum_certificate to_valid_quorum_certificate() const;
   };
} //eosio::chain


FC_REFLECT(eosio::chain::vote_message, (block_id)(strong)(finalizer_key)(sig));
FC_REFLECT_ENUM(eosio::chain::vote_status, (success)(duplicate)(unknown_public_key)(invalid_signature)(unknown_block))
FC_REFLECT(eosio::chain::valid_quorum_certificate, (_strong_votes)(_weak_votes)(_sig));
FC_REFLECT(eosio::chain::pending_quorum_certificate, (_valid_qc)(_quorum)(_max_weak_sum_before_weak_final)(_state)(_strong_sum)(_weak_sum)(_weak_votes)(_strong_votes));
FC_REFLECT_ENUM(eosio::chain::pending_quorum_certificate::state_t, (unrestricted)(restricted)(weak_achieved)(weak_final)(strong));
FC_REFLECT(eosio::chain::pending_quorum_certificate::votes_t, (_bitset)(_sig));
FC_REFLECT(eosio::chain::quorum_certificate, (block_num)(qc));

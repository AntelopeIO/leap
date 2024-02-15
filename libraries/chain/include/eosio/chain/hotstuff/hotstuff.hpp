#pragma once

#include <eosio/chain/block_timestamp.hpp>
#include <fc/crypto/bls_private_key.hpp>
#include <fc/crypto/bls_public_key.hpp>
#include <fc/crypto/bls_signature.hpp>

#include <boost/dynamic_bitset.hpp>

#include <mutex>

namespace eosio::chain {

   using bls_public_key  = fc::crypto::blslib::bls_public_key;
   using bls_signature   = fc::crypto::blslib::bls_signature;
   using bls_private_key = fc::crypto::blslib::bls_private_key;

   using hs_bitset = boost::dynamic_bitset<uint32_t>;
   using bls_key_map_t = std::map<bls_public_key, bls_private_key>;

   struct vote_message {
      fc::sha256                          proposal_id; //vote on proposal
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
   class valid_quorum_certificate {
   public:
      valid_quorum_certificate(const std::vector<uint32_t>& strong_votes, //bitset encoding, following canonical order
                               const std::vector<uint32_t>& weak_votes,   //bitset encoding, following canonical order
                               const bls_signature& sig);

      valid_quorum_certificate() = default;
      valid_quorum_certificate(const valid_quorum_certificate&) = default;

      bool is_weak()   const { return !!_weak_votes; }
      bool is_strong() const { return !_weak_votes; }

      friend struct fc::reflector<valid_quorum_certificate>;
      std::optional<hs_bitset> _strong_votes;
      std::optional<hs_bitset> _weak_votes;
      bls_signature            _sig;
   };

   // quorum_certificate
   struct quorum_certificate {
      uint32_t                 block_num;
      block_timestamp_type     block_timestamp;
      valid_quorum_certificate qc;
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

      struct votes_t {
         hs_bitset     _bitset;
         bls_signature _sig;

         void resize(size_t num_finalizers) { _bitset.resize(num_finalizers); }
         size_t count() const { return _bitset.count(); }

         vote_status add_vote(std::span<const uint8_t> proposal_digest, size_t index, const bls_public_key& pubkey,
                              const bls_signature& new_sig);

         void reset(size_t num_finalizers);
      };

      pending_quorum_certificate();

      explicit pending_quorum_certificate(size_t num_finalizers, uint64_t quorum, uint64_t max_weak_sum_before_weak_final);

      // thread safe
      bool is_quorum_met() const;

      // thread safe
      std::pair<vote_status, bool> add_vote(bool strong,
                                            std::span<const uint8_t> proposal_digest,
                                            size_t index,
                                            const bls_public_key& pubkey,
                                            const bls_signature& sig,
                                            uint64_t weight);

      state_t state() const { std::lock_guard g(*_mtx); return _state; };
      valid_quorum_certificate to_valid_quorum_certificate() const;

   private:
      friend struct fc::reflector<pending_quorum_certificate>;
      friend class qc_chain;
      std::unique_ptr<std::mutex> _mtx;
      uint64_t             _quorum {0};
      uint64_t             _max_weak_sum_before_weak_final {0}; // max weak sum before becoming weak_final
      state_t              _state { state_t::unrestricted };
      uint64_t             _strong_sum {0}; // accumulated sum of strong votes so far
      uint64_t             _weak_sum {0}; // accumulated sum of weak votes so far
      votes_t              _weak_votes;
      votes_t              _strong_votes;

      // called by add_vote, already protected by mutex
      vote_status add_strong_vote(std::span<const uint8_t> proposal_digest,
                                  size_t index,
                                  const bls_public_key& pubkey,
                                  const bls_signature& sig,
                                  uint64_t weight);

      // called by add_vote, already protected by mutex
      vote_status add_weak_vote(std::span<const uint8_t> proposal_digest,
                                size_t index,
                                const bls_public_key& pubkey,
                                const bls_signature& sig,
                                uint64_t weight);

      bool is_quorum_met_no_lock() const;
   };
} //eosio::chain


FC_REFLECT(eosio::chain::vote_message, (proposal_id)(strong)(finalizer_key)(sig));
FC_REFLECT(eosio::chain::valid_quorum_certificate, (_strong_votes)(_weak_votes)(_sig));
FC_REFLECT(eosio::chain::pending_quorum_certificate, (_quorum)(_max_weak_sum_before_weak_final)(_state)(_strong_sum)(_weak_sum)(_weak_votes)(_strong_votes));
FC_REFLECT(eosio::chain::pending_quorum_certificate::votes_t, (_bitset)(_sig));
FC_REFLECT(eosio::chain::quorum_certificate, (block_num)(block_timestamp)(qc));

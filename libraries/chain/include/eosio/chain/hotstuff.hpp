#pragma once
#include <eosio/chain/block_header.hpp>
#include <fc/bitutil.hpp>
#include <fc/crypto/bls_private_key.hpp>
#include <fc/crypto/bls_public_key.hpp>
#include <fc/crypto/bls_signature.hpp>
#include <fc/crypto/bls_utils.hpp>

#include <boost/dynamic_bitset.hpp>

namespace eosio::chain {

   using hs_bitset = boost::dynamic_bitset<uint8_t>;
   using bls_key_map_t = std::map<fc::crypto::blslib::bls_public_key, fc::crypto::blslib::bls_private_key>;

   inline digest_type get_digest_to_sign(const block_id_type& block_id, uint8_t phase_counter, const fc::sha256& final_on_qc) {
      digest_type h1 = digest_type::hash( std::make_pair( std::cref(block_id), phase_counter ) );
      digest_type h2 = digest_type::hash( std::make_pair( std::cref(h1), std::cref(final_on_qc) ) );
      return h2;
   }

   inline uint64_t compute_height(uint32_t block_height, uint32_t phase_counter) {
      return (uint64_t{block_height} << 32) | phase_counter;
   }

   struct view_number {
      view_number() : bheight(0), pcounter(0) {}
      explicit view_number(uint32_t block_height, uint8_t phase_counter) : bheight(block_height), pcounter(phase_counter) {}
      auto operator<=>(const view_number&) const = default;
      friend std::ostream& operator<<(std::ostream& os, const view_number& vn) {
         os << "view_number(" << vn.bheight << ", " << vn.pcounter << ")\n";
      }

      uint32_t block_height() const { return bheight; }
      uint8_t phase_counter() const { return pcounter; }
      uint64_t get_key() const { return compute_height(bheight, pcounter); }
      std::string to_string() const { return std::to_string(bheight) + "::" + std::to_string(pcounter); }

      uint32_t bheight;
      uint8_t pcounter;
   };

   struct extended_schedule {
      producer_authority_schedule                          producer_schedule;
      std::map<name, fc::crypto::blslib::bls_public_key>   bls_pub_keys;
   };

   struct quorum_certificate_message {
      fc::sha256                          proposal_id;
      std::vector<unsigned_int>           active_finalizers; //bitset encoding, following canonical order
      fc::crypto::blslib::bls_signature   active_agg_sig;
   };

   struct hs_vote_message {
      fc::sha256                          proposal_id; //vote on proposal
      fc::crypto::blslib::bls_public_key  finalizer_key;
      fc::crypto::blslib::bls_signature   sig;
   };

   struct hs_proposal_message {
      fc::sha256                          proposal_id; //vote on proposal
      block_id_type                       block_id;
      fc::sha256                          parent_id; //new proposal
      fc::sha256                          final_on_qc;
      quorum_certificate_message          justify; //justification
      uint8_t                             phase_counter = 0;

      digest_type get_proposal_id() const { return get_digest_to_sign(block_id, phase_counter, final_on_qc); };

      uint32_t block_num() const { return block_header::num_from_id(block_id); }
      uint64_t get_key() const { return compute_height(block_header::num_from_id(block_id), phase_counter); };

      view_number get_view_number() const { return view_number(block_header::num_from_id(block_id), phase_counter); };
   };

   struct hs_new_block_message {
      block_id_type                block_id; //new proposal
      quorum_certificate_message   justify; //justification
   };

   struct hs_new_view_message {
      quorum_certificate_message   high_qc; //justification
   };

   using hs_message = std::variant<hs_vote_message, hs_proposal_message, hs_new_block_message, hs_new_view_message>;

   enum class hs_message_warning {
      discarded,               // default code for dropped messages (irrelevant, redundant, ...)
      duplicate_signature,     // same message signature already seen
      invalid_signature,       // invalid message signature
      invalid                  // invalid message (other reason)
   };

   struct finalizer_state {
      bool chained_mode = false;
      fc::sha256 b_leaf;
      fc::sha256 b_lock;
      fc::sha256 b_exec;
      fc::sha256 b_finality_violation;
      block_id_type block_exec;
      block_id_type pending_proposal_block;
      eosio::chain::view_number v_height;
      eosio::chain::quorum_certificate_message high_qc;
      eosio::chain::quorum_certificate_message current_qc;
      eosio::chain::extended_schedule schedule;
      map<fc::sha256, hs_proposal_message> proposals;

      const hs_proposal_message* get_proposal(const fc::sha256& id) const {
         auto it = proposals.find(id);
         if (it == proposals.end())
            return nullptr;
         return & it->second;
      }
   };

} //eosio::chain


FC_REFLECT(eosio::chain::view_number, (bheight)(pcounter));
FC_REFLECT(eosio::chain::quorum_certificate_message, (proposal_id)(active_finalizers)(active_agg_sig));
FC_REFLECT(eosio::chain::extended_schedule, (producer_schedule)(bls_pub_keys));
FC_REFLECT(eosio::chain::hs_vote_message, (proposal_id)(finalizer_key)(sig));
FC_REFLECT(eosio::chain::hs_proposal_message, (proposal_id)(block_id)(parent_id)(final_on_qc)(justify)(phase_counter));
FC_REFLECT(eosio::chain::hs_new_block_message, (block_id)(justify));
FC_REFLECT(eosio::chain::hs_new_view_message, (high_qc));
FC_REFLECT(eosio::chain::finalizer_state, (chained_mode)(b_leaf)(b_lock)(b_exec)(b_finality_violation)(block_exec)(pending_proposal_block)(v_height)(high_qc)(current_qc)(schedule)(proposals));

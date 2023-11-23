#pragma once
#include <eosio/chain/block_header.hpp>
#include <fc/bitutil.hpp>
#include <fc/crypto/bls_private_key.hpp>
#include <fc/crypto/bls_public_key.hpp>
#include <fc/crypto/bls_signature.hpp>

#include <boost/dynamic_bitset.hpp>

namespace eosio::hotstuff {

   using hs_bitset = boost::dynamic_bitset<uint8_t>;
   using bls_key_map_t = std::map<fc::crypto::blslib::bls_public_key, fc::crypto::blslib::bls_private_key>;

   inline chain::digest_type get_digest_to_sign(const chain::block_id_type& block_id, uint8_t phase_counter, const fc::sha256& final_on_qc) {
      chain::digest_type h1 = chain::digest_type::hash( std::make_pair( std::cref(block_id), phase_counter ) );
      chain::digest_type h2 = chain::digest_type::hash( std::make_pair( std::cref(h1), std::cref(final_on_qc) ) );
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
         return os;
      }

      uint32_t block_height() const { return bheight; }
      uint8_t phase_counter() const { return pcounter; }
      uint64_t get_key() const { return compute_height(bheight, pcounter); }
      std::string to_string() const { return std::to_string(bheight) + "::" + std::to_string(pcounter); }

      uint32_t bheight;
      uint8_t pcounter;
   };

   struct extended_schedule {
      chain::producer_authority_schedule                          producer_schedule;
      std::map<chain::name, fc::crypto::blslib::bls_public_key>   bls_pub_keys;
   };

   struct quorum_certificate_message {
      fc::sha256                          proposal_id;
      std::vector<chain::unsigned_int>    active_finalizers; //bitset encoding, following canonical order
      fc::crypto::blslib::bls_signature   active_agg_sig;
   };

   struct hs_vote_message {
      fc::sha256                          proposal_id; //vote on proposal
      fc::crypto::blslib::bls_public_key  finalizer_key;
      fc::crypto::blslib::bls_signature   sig;
   };

   struct hs_proposal_message {
      fc::sha256                          proposal_id; //vote on proposal
      chain::block_id_type                block_id;
      fc::sha256                          parent_id; //new proposal
      fc::sha256                          final_on_qc;
      quorum_certificate_message          justify; //justification
      uint8_t                             phase_counter = 0;

      chain::digest_type get_proposal_id() const { return get_digest_to_sign(block_id, phase_counter, final_on_qc); };

      uint32_t block_num() const { return chain::block_header::num_from_id(block_id); }
      uint64_t get_key() const { return compute_height(chain::block_header::num_from_id(block_id), phase_counter); };

      view_number get_view_number() const { return view_number(chain::block_header::num_from_id(block_id), phase_counter); };
   };

   struct hs_new_view_message {
      quorum_certificate_message   high_qc; //justification
   };

   struct hs_message {
      std::variant<hs_vote_message, hs_proposal_message, hs_new_view_message> msg;
   };

   enum class hs_message_warning {
      discarded,               // default code for dropped messages (irrelevant, redundant, ...)
      duplicate_signature,     // same message signature already seen
      invalid_signature,       // invalid message signature
      invalid                  // invalid message (other reason)
   };

   struct finalizer_state {
      fc::sha256 b_leaf;
      fc::sha256 b_lock;
      fc::sha256 b_exec;
      fc::sha256 b_finality_violation;
      chain::block_id_type block_exec;
      chain::block_id_type pending_proposal_block;
      view_number v_height;
      quorum_certificate_message high_qc;
      quorum_certificate_message current_qc;
      extended_schedule schedule;
      std::map<fc::sha256, hs_proposal_message> proposals;

      const hs_proposal_message* get_proposal(const fc::sha256& id) const {
         auto it = proposals.find(id);
         if (it == proposals.end())
            return nullptr;
         return & it->second;
      }
   };

} //eosio::hotstuff


FC_REFLECT(eosio::hotstuff::view_number, (bheight)(pcounter));
FC_REFLECT(eosio::hotstuff::quorum_certificate_message, (proposal_id)(active_finalizers)(active_agg_sig));
FC_REFLECT(eosio::hotstuff::extended_schedule, (producer_schedule)(bls_pub_keys));
FC_REFLECT(eosio::hotstuff::hs_vote_message, (proposal_id)(finalizer_key)(sig));
FC_REFLECT(eosio::hotstuff::hs_proposal_message, (proposal_id)(block_id)(parent_id)(final_on_qc)(justify)(phase_counter));
FC_REFLECT(eosio::hotstuff::hs_new_view_message, (high_qc));
FC_REFLECT(eosio::hotstuff::finalizer_state, (b_leaf)(b_lock)(b_exec)(b_finality_violation)(block_exec)(pending_proposal_block)(v_height)(high_qc)(current_qc)(schedule)(proposals));
FC_REFLECT(eosio::hotstuff::hs_message, (msg));

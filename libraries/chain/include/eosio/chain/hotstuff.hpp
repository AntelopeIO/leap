#pragma once
#include <eosio/chain/block_header.hpp>
#include <fc/bitutil.hpp>
#include <fc/crypto/bls_private_key.hpp>
#include <fc/crypto/bls_public_key.hpp>
#include <fc/crypto/bls_signature.hpp>
#include <fc/crypto/bls_utils.hpp>

#include <boost/dynamic_bitset.hpp>

namespace eosio::chain {

   const block_id_type NULL_BLOCK_ID = block_id_type("00");
   const fc::sha256 NULL_PROPOSAL_ID = fc::sha256("00");

   using hs_dynamic_bitset = boost::dynamic_bitset<uint32_t>;

   inline uint64_t compute_height(uint32_t block_height, uint32_t phase_counter) {
      return (uint64_t{block_height} << 32) | phase_counter;
   }

   struct extended_schedule {
      producer_authority_schedule                          producer_schedule;
      std::map<name, fc::crypto::blslib::bls_public_key>   bls_pub_keys;
   };

   struct quorum_certificate_message {
      fc::sha256                          proposal_id = NULL_PROPOSAL_ID;
      std::vector<unsigned_int>           active_finalizers; //bitset encoding, following canonical order
      fc::crypto::blslib::bls_signature   active_agg_sig;
   };

   struct hs_vote_message {
      fc::sha256                          proposal_id = NULL_PROPOSAL_ID; //vote on proposal
      name                                finalizer;
      fc::crypto::blslib::bls_signature   sig;
   };

   struct hs_proposal_message {
      fc::sha256                          proposal_id = NULL_PROPOSAL_ID; //vote on proposal
      block_id_type                       block_id = NULL_BLOCK_ID;
      fc::sha256                          parent_id = NULL_PROPOSAL_ID; //new proposal
      fc::sha256                          final_on_qc = NULL_PROPOSAL_ID;
      quorum_certificate_message          justify; //justification
      uint8_t                             phase_counter = 0;

      uint32_t block_num() const { return block_header::num_from_id(block_id); }
      uint64_t get_height() const { return compute_height(block_header::num_from_id(block_id), phase_counter); };
   };

   struct hs_new_block_message {
      block_id_type                block_id = NULL_BLOCK_ID; //new proposal
      quorum_certificate_message   justify; //justification
   };

   struct hs_new_view_message {
      quorum_certificate_message   high_qc; //justification
   };

   struct finalizer_state {
      bool chained_mode = false;
      fc::sha256 b_leaf = NULL_PROPOSAL_ID;
      fc::sha256 b_lock = NULL_PROPOSAL_ID;
      fc::sha256 b_exec = NULL_PROPOSAL_ID;
      fc::sha256 b_finality_violation = NULL_PROPOSAL_ID;
      block_id_type block_exec = NULL_BLOCK_ID;
      block_id_type pending_proposal_block = NULL_BLOCK_ID;
      uint32_t v_height = 0;
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

// // @ignore quorum_met
FC_REFLECT(eosio::chain::quorum_certificate_message, (proposal_id)(active_finalizers)(active_agg_sig));
FC_REFLECT(eosio::chain::extended_schedule, (producer_schedule)(bls_pub_keys));
FC_REFLECT(eosio::chain::hs_vote_message, (proposal_id)(finalizer)(sig));
FC_REFLECT(eosio::chain::hs_proposal_message, (proposal_id)(block_id)(parent_id)(final_on_qc)(justify)(phase_counter));
FC_REFLECT(eosio::chain::hs_new_block_message, (block_id)(justify));
FC_REFLECT(eosio::chain::hs_new_view_message, (high_qc));
FC_REFLECT(eosio::chain::finalizer_state, (chained_mode)(b_leaf)(b_lock)(b_exec)(b_finality_violation)(block_exec)(pending_proposal_block)(v_height)(high_qc)(current_qc)(schedule)(proposals));

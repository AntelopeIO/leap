#pragma once
#include <eosio/chain/block_header.hpp>
#include <fc/bitutil.hpp>
#include <eosio/chain/block_header.hpp>
#include <fc/crypto/bls_private_key.hpp>
#include <fc/crypto/bls_public_key.hpp>
#include <fc/crypto/bls_signature.hpp>
#include <fc/crypto/bls_utils.hpp>

namespace eosio { namespace chain {

   const block_id_type NULL_BLOCK_ID = block_id_type("00");
   const fc::sha256 NULL_PROPOSAL_ID = fc::sha256("00");

   static uint32_t compute_block_num(block_id_type block_id){
      return fc::endian_reverse_u32(block_id._hash[0]);
   }

   static uint64_t compute_height(uint32_t block_height, uint32_t phase_counter){
      return (uint64_t{block_height} << 32) | phase_counter;
   }

   struct extended_schedule {
      producer_authority_schedule producer_schedule;
      std::map<name, fc::crypto::blslib::bls_public_key> bls_pub_keys;
   };

   struct quorum_certificate {
      fc::sha256                          proposal_id = NULL_PROPOSAL_ID;
      bool                                quorum_met = false;
      fc::unsigned_int                    active_finalizers; //bitset encoding, following canonical order
      fc::crypto::blslib::bls_signature   active_agg_sig;
   };

   struct hs_vote_message {
      fc::sha256                          proposal_id = NULL_PROPOSAL_ID; //vote on proposal
      name                                finalizer;
      fc::crypto::blslib::bls_signature   sig;

      hs_vote_message() = default;
   };

   struct hs_proposal_message {
      fc::sha256                          proposal_id = NULL_PROPOSAL_ID; //vote on proposal
      block_id_type                       block_id = NULL_BLOCK_ID;
      uint8_t                             phase_counter = 0;
      fc::sha256                          parent_id = NULL_PROPOSAL_ID; //new proposal
      fc::sha256                          final_on_qc = NULL_PROPOSAL_ID;
      quorum_certificate                  justify; //justification

      hs_proposal_message() = default;

      uint32_t block_num()const {
         return compute_block_num(block_id);
      }

      uint64_t get_height()const {
         return compute_height(compute_block_num(block_id), phase_counter);
      };
   };

   struct hs_new_block_message {
      block_id_type        block_id = NULL_BLOCK_ID; //new proposal
      quorum_certificate   justify; //justification
      hs_new_block_message() = default;
   };

   struct hs_new_view_message {
      quorum_certificate   high_qc; //justification
      hs_new_view_message() = default;
   };

   using hs_proposal_message_ptr = std::shared_ptr<hs_proposal_message>;
   using hs_vote_message_ptr = std::shared_ptr<hs_vote_message>;

   using hs_new_view_message_ptr = std::shared_ptr<hs_new_view_message>;
   using hs_new_block_message_ptr = std::shared_ptr<hs_new_block_message>;

}} //eosio::chain

FC_REFLECT(eosio::chain::quorum_certificate, (proposal_id)(active_finalizers)(active_agg_sig));
FC_REFLECT(eosio::chain::hs_vote_message, (proposal_id)(finalizer)(sig));
FC_REFLECT(eosio::chain::hs_proposal_message, (proposal_id)(block_id)(phase_counter)(parent_id)(final_on_qc)(justify));
FC_REFLECT(eosio::chain::hs_new_block_message, (block_id)(justify));
FC_REFLECT(eosio::chain::hs_new_view_message, (high_qc));

#pragma once
#include <eosio/chain/block_header.hpp>
#include <fc/bitutil.hpp>
#include <fc/crypto/bls_private_key.hpp>
#include <fc/crypto/bls_public_key.hpp>
#include <fc/crypto/bls_signature.hpp>
#include <fc/crypto/bls_utils.hpp>

namespace eosio::chain {

   const block_id_type NULL_BLOCK_ID = block_id_type("00");
   const fc::sha256 NULL_PROPOSAL_ID = fc::sha256("00");

   inline uint64_t compute_height(uint32_t block_height, uint32_t phase_counter) {
      return (uint64_t{block_height} << 32) | phase_counter;
   }

   struct view_number{

      view_number(){
         _data = std::make_pair(0,0);
      }
      view_number(std::pair<uint32_t, uint8_t> data){
         _data = data;
      }

      auto operator<=>(const view_number&) const = default;

      uint32_t block_height(){
         return _data.first;
      }

      uint8_t phase_counter(){
         return _data.second;
      }

      uint64_t to_uint64_t(){
         return compute_height(_data.first, _data.second);
      }

      std::string to_string(){
         return _data.first + "::" + _data.second;
      }
      
      std::pair<uint32_t, uint8_t> _data;

   };

   struct extended_schedule {
      producer_authority_schedule                          producer_schedule;
      std::map<name, fc::crypto::blslib::bls_public_key>   bls_pub_keys;
   };

   struct quorum_certificate {
      fc::sha256                          proposal_id = NULL_PROPOSAL_ID;
      fc::unsigned_int                    active_finalizers = 0; //bitset encoding, following canonical order
      fc::crypto::blslib::bls_signature   active_agg_sig;
      bool                                quorum_met = false;
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
      quorum_certificate                  justify; //justification
      uint8_t                             phase_counter = 0;

      uint32_t block_num() const { return block_header::num_from_id(block_id); }
      uint64_t get_key() const { return compute_height(block_header::num_from_id(block_id), phase_counter); };

      view_number get_view_number() const { return std::make_pair(block_header::num_from_id(block_id), phase_counter); };

   };

   struct hs_new_block_message {
      block_id_type        block_id = NULL_BLOCK_ID; //new proposal
      quorum_certificate   justify; //justification
   };

   struct hs_new_view_message {
      quorum_certificate   high_qc; //justification
   };

   struct finalizer_state {

      bool chained_mode = false;
      fc::sha256 b_leaf = NULL_PROPOSAL_ID;
      fc::sha256 b_lock = NULL_PROPOSAL_ID;
      fc::sha256 b_exec = NULL_PROPOSAL_ID;
      fc::sha256 b_finality_violation = NULL_PROPOSAL_ID;
      block_id_type block_exec = NULL_BLOCK_ID;
      block_id_type pending_proposal_block = NULL_BLOCK_ID;
      eosio::chain::view_number v_height;
      eosio::chain::quorum_certificate high_qc;
      eosio::chain::quorum_certificate current_qc;
      eosio::chain::extended_schedule schedule;
      map<fc::sha256, hs_proposal_message> proposals;

      const hs_proposal_message* get_proposal(const fc::sha256& id) const {
         auto it = proposals.find(id);
         if (it == proposals.end())
            return nullptr;
         return & it->second;
      }
   };

   using hs_proposal_message_ptr = std::shared_ptr<hs_proposal_message>;
   using hs_vote_message_ptr = std::shared_ptr<hs_vote_message>;
   using hs_new_view_message_ptr = std::shared_ptr<hs_new_view_message>;
   using hs_new_block_message_ptr = std::shared_ptr<hs_new_block_message>;

} //eosio::chain

FC_REFLECT(eosio::chain::view_number, (_data));
FC_REFLECT(eosio::chain::quorum_certificate, (proposal_id)(active_finalizers)(active_agg_sig));
FC_REFLECT(eosio::chain::extended_schedule, (producer_schedule)(bls_pub_keys));
FC_REFLECT(eosio::chain::hs_vote_message, (proposal_id)(finalizer)(sig));
FC_REFLECT(eosio::chain::hs_proposal_message, (proposal_id)(block_id)(parent_id)(final_on_qc)(justify)(phase_counter));
FC_REFLECT(eosio::chain::hs_new_block_message, (block_id)(justify));
FC_REFLECT(eosio::chain::hs_new_view_message, (high_qc));
FC_REFLECT(eosio::chain::finalizer_state, (chained_mode)(b_leaf)(b_lock)(b_exec)(b_finality_violation)(block_exec)(pending_proposal_block)(v_height)(high_qc)(current_qc)(schedule)(proposals));

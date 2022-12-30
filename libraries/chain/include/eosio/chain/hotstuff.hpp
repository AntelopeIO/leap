#pragma once
#include <eosio/chain/block_header.hpp>
#include <fc/bitutil.hpp>
#include <eosio/chain/block_header.hpp>
#include <fc/crypto/bls_private_key.hpp>
#include <fc/crypto/bls_public_key.hpp>
#include <fc/crypto/bls_signature.hpp>
#include <fc/crypto/bls_utils.hpp>

namespace eosio { namespace chain {

   //using namespace fc::crypto::blslib;

   //todo : fetch from chain / nodeos config

   const uint32_t block_interval = 500;
   const uint32_t blocks_per_round = 12;

   const uint32_t _threshold = 15;
   
   static uint32_t compute_block_num(block_id_type block_id)
   {
      return fc::endian_reverse_u32(block_id._hash[0]);
   }

   struct extended_schedule {

      producer_authority_schedule producer_schedule;

      std::map<name, fc::crypto::blslib::bls_public_key> bls_pub_keys; 

   };

/*   struct qc_height {
      
      uint32_t block_height;
      uint8_t phase;

      bool operator == (const qc_height& rhs) {
         if (block_height != rhs.block_height) return false;
         if (phase != rhs.phase) return false;
         return true;
      }

      bool operator != (const qc_height& rhs) {
         if (block_height != rhs.block_height) return true;
         if (phase != rhs.phase) return true;
         return false;
      }

      bool operator<(const qc_height& rhs) {
         if (block_height < rhs.block_height) return true;
         else if (block_height == rhs.block_height){
            if (phase < rhs.phase) return true;
         }
         else return false;
      }

      bool operator>(const qc_height& rhs) {
         if (block_height > rhs.block_height) return true;
         else if (block_height == rhs.block_height){
            if (phase > rhs.phase) return true;
         }
         else return false;
      }

   };*/

   struct quorum_certificate {

      public:

         block_id_type                                      block_id;

         vector<name>                                       active_finalizers;
         fc::crypto::blslib::bls_signature                  active_agg_sig;

         std::optional<vector<name>>                        incoming_finalizers;
         std::optional<fc::crypto::blslib::bls_signature>   incoming_agg_sig;
               
         uint32_t block_num()const{
            return compute_block_num(block_id);
         }

         /*bool quorum_met(extended_schedule es, bool dual_set_mode){

            if (  dual_set_mode && 
                  incoming_finalizers.has_value() && 
                  incoming_agg_sig.has_value()){
               return _quorum_met(es, active_finalizers, active_agg_sig) && _quorum_met(es, incoming_finalizers.value(), incoming_agg_sig.value());
            }
            else {
               return _quorum_met(es, active_finalizers, active_agg_sig);
            }

         };

      private:
         bool _quorum_met(extended_schedule es, vector<name> finalizers, fc::crypto::blslib::bls_signature agg_sig){
            
            ilog("evaluating if _quorum_met");

            if (finalizers.size() != _threshold){
            
               ilog("finalizers.size() ${size}", ("size",finalizers.size()));
               return false;
            
            }

            ilog("correct threshold");
            
            fc::crypto::blslib::bls_public_key agg_key;

            for (name f : finalizers) {

               auto itr = es.bls_pub_keys.find(f);

               if (itr==es.bls_pub_keys.end()) return false;
   
               agg_key = fc::crypto::blslib::aggregate({agg_key, itr->second });

            }

            std::vector<unsigned char> msg = std::vector<unsigned char>(block_id.data(), block_id.data() + 32);

            bool ok = fc::crypto::blslib::verify(agg_key, msg, agg_sig);

            return ok;

            return true; //temporary

         }*/

   };

   struct hs_vote_message {

      block_id_type                       block_id; //vote on proposal

      name                                finalizer;
      fc::crypto::blslib::bls_signature   sig;

      hs_vote_message() = default;

      uint32_t block_num()const{
         return compute_block_num(block_id);
      }

   };

   struct hs_proposal_message {

      block_id_type                       block_id; //new proposal

      std::optional<quorum_certificate>   justify; //justification

      hs_proposal_message() = default;
      
      uint32_t block_num()const{
         return compute_block_num(block_id);
      }

   };

   struct hs_new_block_message {

      block_id_type                       block_id; //new proposal

      std::optional<quorum_certificate>   justify; //justification

      hs_new_block_message() = default;
      
      uint32_t block_num()const{
         return compute_block_num(block_id);
      }

   };

   struct hs_new_view_message {

      std::optional<quorum_certificate>   high_qc; //justification

      hs_new_view_message() = default;
      
   };

   using hs_proposal_message_ptr = std::shared_ptr<hs_proposal_message>;
   using hs_vote_message_ptr = std::shared_ptr<hs_vote_message>;
   
   using hs_new_view_message_ptr = std::shared_ptr<hs_new_view_message>;
   using hs_new_block_message_ptr = std::shared_ptr<hs_new_block_message>;

}} //eosio::chain


//FC_REFLECT_ENUM( eosio::chain::consensus_msg_type,
//                 (cm_new_view)(cm_prepare)(cm_pre_commit)(cm_commit)(cm_decide) );

//FC_REFLECT(eosio::chain::consensus_node, (header)(previous_bmroot)(schedule_hash)(digest_to_sign));
FC_REFLECT(eosio::chain::quorum_certificate, (block_id)(active_finalizers)(active_agg_sig)(incoming_finalizers)(incoming_agg_sig));
//FC_REFLECT(eosio::chain::proposal, (block)(justify));
FC_REFLECT(eosio::chain::hs_vote_message, (block_id)(finalizer)(sig));
FC_REFLECT(eosio::chain::hs_proposal_message, (block_id)(justify));
FC_REFLECT(eosio::chain::hs_new_block_message, (block_id)(justify));
FC_REFLECT(eosio::chain::hs_new_view_message, (high_qc));
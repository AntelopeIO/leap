#pragma once
#include <eosio/chain/block_header.hpp>
#include <fc/crypto/bls_private_key.hpp>

namespace eosio { namespace chain {

   using bls_signature_type	= fc::crypto::blslib::bls_signature;

   enum consensus_msg_type {
         cm_new_view = 1,
         cm_prepare = 2, 
         cm_pre_commit = 3,
         cm_commit = 4,
         cm_decide = 5
   };

   struct consensus_node {
   
      block_header         header;
      fc::sha256           previous_bmroot;
      fc::sha256           schedule_hash;
      fc::sha256           digest_to_sign;

   };

   struct confirmation_message {

      consensus_msg_type      msg_type;
      uint32_t                view_number;
      consensus_node          node;

      name                    finalizer;
      bls_signature_type      sig;

      confirmation_message() = default;

   };

   struct quorum_certificate {

      consensus_msg_type      msg_type;
      uint32_t                view_number;
      consensus_node          node;

      vector<name>            finalizers;
      bls_signature_type      sig;

   };

   struct consensus_message {

      consensus_msg_type      msg_type;
      uint32_t                view_number;
      consensus_node          node;

      std::optional<quorum_certificate>      justify;

      consensus_message() = default;
      
   };

   using consensus_message_ptr = std::shared_ptr<consensus_message>;
   using confirmation_message_ptr = std::shared_ptr<confirmation_message>;
   
}} //eosio::chain


FC_REFLECT_ENUM( eosio::chain::consensus_msg_type,
                 (cm_new_view)(cm_prepare)(cm_pre_commit)(cm_commit)(cm_decide) );

FC_REFLECT(eosio::chain::consensus_node, (header)(previous_bmroot)(schedule_hash)(digest_to_sign));
FC_REFLECT(eosio::chain::confirmation_message, (msg_type)(view_number)(node)(finalizer)(sig));
FC_REFLECT(eosio::chain::quorum_certificate, (msg_type)(view_number)(node)(finalizers)(sig));
FC_REFLECT(eosio::chain::consensus_message, (msg_type)(view_number)(node)(justify));
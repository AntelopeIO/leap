#pragma once
#include <fc/crypto/sha256.hpp>
#include <fc/crypto/bls_private_key.hpp>

namespace eosio { namespace chain {

   using bls_signature_type	= fc::crypto::blslib::bls_signature;

   enum consensus_msg_type{
   		new_view = 1,
   		prepare = 2, 
   		pre_commit = 3,
   		commit = 4,
   		decide = 5
   };

   struct consensus_node {

   		uint32_t  					block_height;
   		fc::sha256  				digest;

   };

   struct quorum_certificate {

   		consensus_msg_type					msg_type;
   		uint32_t							view_number;
   		consensus_node						node;

   		vector<uint8_t>						canonical_producers;
   		bls_signature_type 					sig;

   };

   struct consensus_message {

   		consensus_msg_type					msg_type;
   		uint32_t							view_number;
   		consensus_node						node;

   		quorum_certificate 					justify;

   };

}}


FC_REFLECT_ENUM( eosio::chain::consensus_msg_type,
                 (new_view)(prepare)(pre_commit)(commit)(decide) );

FC_REFLECT(eosio::chain::consensus_node, (block_height)(digest));
FC_REFLECT(eosio::chain::quorum_certificate, (msg_type)(view_number)(node)(canonical_producers)(sig));
FC_REFLECT(eosio::chain::consensus_message, (msg_type)(view_number)(node)(justify));
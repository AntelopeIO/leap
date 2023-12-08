#pragma once

#include <eosio/chain/block_header_state.hpp>
#include <eosio/chain/block.hpp>
#include <eosio/chain/transaction_metadata.hpp>
#include <eosio/chain/action_receipt.hpp>

namespace eosio::chain {

struct block_state {
   block_header_state         bhs; // provides parent link
   block_id_type              id;
   signed_block_ptr           block;
   digest_type                finalizer_digest;
   pending_quorum_certificate pending_qc;             // where we accumulate votes we receive
   std::optional<valid_quorum_certificate> valid_qc;  // qc received from the network
};

}
#pragma once

#include <eosio/chain/block_header_state.hpp>
#include <eosio/chain/block.hpp>
#include <eosio/chain/transaction_metadata.hpp>
#include <eosio/chain/action_receipt.hpp>

namespace eosio::chain {

struct block_state_legacy;
struct block_state_accessor;

struct block_state : public block_header_state {     // block_header_state provides parent link
   // ------ data members -------------------------------------------------------------
   signed_block_ptr           block;
   digest_type                strong_digest;         // finalizer_digest (strong, cached so we can quickly validate votes)
   digest_type                weak_digest;           // finalizer_digest (weak, cached so we can quickly validate votes)
   pending_quorum_certificate pending_qc;            // where we accumulate votes we receive
   std::optional<valid_quorum_certificate> valid_qc; // best qc received from the network inside block extension

   // ------ updated for votes, used for fork_db ordering ------------------------------
private:
   bool                       validated = false;     // We have executed the block's trxs and verified that action merkle root (block id) matches.
   block_header_state_core    current_core;          // only modify/access while holding forkdb lock

   // ------ data members caching information available elsewhere ----------------------
   bool                       pub_keys_recovered = false;
   deque<transaction_metadata_ptr> cached_trxs;

   // ------ private methods -----------------------------------------------------------
   bool is_valid() const { return validated; }
   bool                                is_pub_keys_recovered() const { return pub_keys_recovered; }
   deque<transaction_metadata_ptr>     extract_trxs_metas();
   void                                set_trxs_metas(deque<transaction_metadata_ptr>&& trxs_metas, bool keys_recovered);
   const deque<transaction_metadata_ptr>& trxs_metas()  const { return cached_trxs; }

   friend struct block_state_accessor;
   friend struct fc::reflector<block_state>;
   friend struct controller_impl;
   friend struct completed_block;
public:
   // ------ functions -----------------------------------------------------------------
   const block_id_type&   id()                const { return block_header_state::id; }
   const block_id_type&   previous()          const { return block_header_state::previous(); }
   uint32_t               block_num()         const { return block_header_state::block_num(); }
   block_timestamp_type   timestamp()         const { return block_header_state::timestamp(); }
   const extensions_type& header_extensions() const { return block_header_state::header.header_extensions; }
   uint32_t               irreversible_blocknum() const { return core.last_final_block_num; }
   std::optional<quorum_certificate> get_best_qc() const;
   bool is_best_qc_strong() const;

   // vote_status, pending_qc state, last_final_block_num
   std::tuple<vote_status, pending_quorum_certificate::state_t, std::optional<uint32_t>>
   aggregate_vote(const vote_message& vote); // aggregate vote into pending_qc
   void verify_qc(const valid_quorum_certificate& qc) const; // verify given qc is valid with respect block_state

   using bhs_t  = block_header_state;
   using bhsp_t = block_header_state_ptr;
   using fork_db_block_state_accessor = block_state_accessor;

   block_state() = default;

   block_state(const block_header_state& prev, signed_block_ptr b, const protocol_feature_set& pfs,
               const validator_t& validator, bool skip_validate_signee);

   block_state(const block_header_state& bhs, deque<transaction_metadata_ptr>&& trx_metas,
               deque<transaction_receipt>&& trx_receipts, const std::optional<quorum_certificate>& qc);

   explicit block_state(const block_state_legacy& bsp);
};

using block_state_ptr = std::shared_ptr<block_state>;
   
} // namespace eosio::chain

// not exporting pending_qc or valid_qc
FC_REFLECT_DERIVED( eosio::chain::block_state, (eosio::chain::block_header_state), (block)(strong_digest)(weak_digest)(pending_qc)(valid_qc)(validated)(current_core) )

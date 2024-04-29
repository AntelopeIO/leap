#pragma once

#include <eosio/chain/block_header_state.hpp>
#include <eosio/chain/block.hpp>
#include <eosio/chain/transaction_metadata.hpp>
#include <eosio/chain/action_receipt.hpp>
#include <eosio/chain/incremental_merkle.hpp>
#include <eosio/chain/thread_utils.hpp>

namespace eosio::chain {

using signer_callback_type = std::function<std::vector<signature_type>(const digest_type&)>;

constexpr std::array weak_bls_sig_postfix = { 'W', 'E', 'A', 'K' };
using weak_digest_t = std::array<uint8_t, sizeof(digest_type) + weak_bls_sig_postfix.size()>;

inline weak_digest_t create_weak_digest(const digest_type& digest) {
   weak_digest_t res;
   std::memcpy(res.begin(), digest.data(), digest.data_size());
   std::memcpy(res.begin() + digest.data_size(), weak_bls_sig_postfix.data(), weak_bls_sig_postfix.size());
   return res;
}

struct block_state_legacy;
struct block_state_accessor;

/*
 * Important concepts:
 * 1. A Finality Merkle Tree is a Merkle tree over a sequence of Finality Leaf Nodes,
 *    one for each block starting from the IF Genesis Block and ending at some
 *    specified descendant block.
 * 2. The Validation Tree associated with a target block is the Finality Merkle
 *    Tree over Finality Leaf Nodes starting with the one for the IF Genesis Block
 *    and ending with the one for the target Block.
 * 3. The Finality Tree associated with a target block is the Validation Tree of the
 *    block referenced by the target block's final_on_strong_qc_block_num.
 *    That is, validation_tree(core.final_on_strong_qc_block_num))
 * */
struct valid_t {
   struct finality_leaf_node_t {
      uint32_t       major_version{light_header_protocol_version_major};
      uint32_t       minor_version{light_header_protocol_version_minor};
      block_num_type block_num{0};   // the block number
      digest_type    finality_digest; // finality digest for the block
      digest_type    action_mroot;    // digest of the root of the action Merkle tree of the block
   };

   // The Finality Merkle Tree, containing leaf nodes from IF genesis block to current block
   incremental_merkle_tree validation_tree;

   // The sequence of root digests of the validation trees associated
   // with an unbroken sequence of blocks consisting of the blocks
   // starting with the one that has a block number equal
   // to core.last_final_block_num, and ending with the current block
   std::vector<digest_type> validation_mroots;
};

// This is mostly used by SHiP to stream finality_data
struct finality_data_t {
   uint32_t     major_version{light_header_protocol_version_major};
   uint32_t     minor_version{light_header_protocol_version_minor};
   uint32_t     active_finalizer_policy_generation{0};
   digest_type  action_mroot{};
   digest_type  base_digest{};
};

struct block_state : public block_header_state {     // block_header_state provides parent link
   // ------ data members -------------------------------------------------------------
   signed_block_ptr           block;
   digest_type                strong_digest;         // finalizer_digest (strong, cached so we can quickly validate votes)
   weak_digest_t              weak_digest;           // finalizer_digest (weak, cached so we can quickly validate votes)
   pending_quorum_certificate pending_qc;            // where we accumulate votes we receive
   std::optional<valid_t>     valid;

   // ------ updated for votes, used for fork_db ordering ------------------------------
private:
   copyable_atomic<bool>      validated{false};     // We have executed the block's trxs and verified that action merkle root (block id) matches.

   // ------ data members caching information available elsewhere ----------------------
   bool                       pub_keys_recovered = false;
   deque<transaction_metadata_ptr> cached_trxs;
   digest_type                action_mroot; // For finality_data sent to SHiP
   std::optional<digest_type> base_digest;  // For finality_data sent to SHiP, computed on demand in get_finality_data()

   // ------ private methods -----------------------------------------------------------
   bool                                is_valid() const { return validated.load(); }
   bool                                is_pub_keys_recovered() const { return pub_keys_recovered; }
   deque<transaction_metadata_ptr>     extract_trxs_metas();
   void                                set_trxs_metas(deque<transaction_metadata_ptr>&& trxs_metas, bool keys_recovered);
   const deque<transaction_metadata_ptr>& trxs_metas()  const { return cached_trxs; }

   friend struct block_state_accessor;
   friend struct fc::reflector<block_state>;
   friend struct controller_impl;
   friend struct completed_block;
   friend struct building_block;
public:
   // ------ functions -----------------------------------------------------------------
   const block_id_type&   id()                const { return block_header_state::id(); }
   const block_id_type&   previous()          const { return block_header_state::previous(); }
   uint32_t               block_num()         const { return block_header_state::block_num(); }
   block_timestamp_type   timestamp()         const { return block_header_state::timestamp(); }
   const extensions_type& header_extensions() const { return block_header_state::header.header_extensions; }
   uint32_t               irreversible_blocknum() const { return core.last_final_block_num(); } // backwards compatibility
   uint32_t               last_final_block_num() const { return core.last_final_block_num(); }
   std::optional<quorum_certificate> get_best_qc() const { return pending_qc.get_best_qc(block_num()); } // thread safe
   bool valid_qc_is_strong() const { return pending_qc.valid_qc_is_strong(); } // thread safe
   void set_valid_qc(const valid_quorum_certificate& qc) { pending_qc.set_valid_qc(qc); }

   protocol_feature_activation_set_ptr get_activated_protocol_features() const { return block_header_state::activated_protocol_features; }
   uint32_t               last_qc_block_num() const { return core.latest_qc_claim().block_num; }
   uint32_t               final_on_strong_qc_block_num() const { return core.final_on_strong_qc_block_num; }

   // build next valid structure from current one with input of next
   valid_t new_valid(const block_header_state& bhs, const digest_type& action_mroot, const digest_type& strong_digest) const;

   // Returns the root digest of the finality tree associated with the target_block_num
   // [core.last_final_block_num, block_num]
   digest_type get_validation_mroot( block_num_type target_block_num ) const;

   // Returns finality_mroot_claim of the current block
   digest_type get_finality_mroot_claim(const qc_claim_t& qc_claim) const;

   // Returns finality_data of the current block
   finality_data_t get_finality_data();

   // vote_status
   vote_status aggregate_vote(const vote_message& vote); // aggregate vote into pending_qc
   bool has_voted(const bls_public_key& key) const;
   void verify_qc(const valid_quorum_certificate& qc) const; // verify given qc is valid with respect block_state

   using bhs_t  = block_header_state;
   using bhsp_t = block_header_state_ptr;
   using fork_db_block_state_accessor_t = block_state_accessor;

   block_state() = default;
   block_state(const block_state&) = delete;
   block_state(block_state&&) = default;

   block_state(const block_header_state& prev, signed_block_ptr b, const protocol_feature_set& pfs,
               const validator_t& validator, bool skip_validate_signee);

   block_state(const block_header_state&                bhs,
               deque<transaction_metadata_ptr>&&        trx_metas,
               deque<transaction_receipt>&&             trx_receipts,
               const std::optional<valid_t>&            valid,
               const std::optional<quorum_certificate>& qc,
               const signer_callback_type&              signer,
               const block_signing_authority&           valid_block_signing_authority,
               const digest_type&                       action_mroot);

   static std::shared_ptr<block_state> create_if_genesis_block(const block_state_legacy& bsp);

   explicit block_state(snapshot_detail::snapshot_block_state_v7&& sbs);

   void sign(const signer_callback_type& signer, const block_signing_authority& valid_block_signing_authority);
   void verify_signee(const std::vector<signature_type>& additional_signatures, const block_signing_authority& valid_block_signing_authority) const;
};

using block_state_ptr       = std::shared_ptr<block_state>;
using block_state_pair      = std::pair<std::shared_ptr<block_state_legacy>, block_state_ptr>;

} // namespace eosio::chain

// not exporting pending_qc or valid_qc
FC_REFLECT( eosio::chain::valid_t::finality_leaf_node_t, (major_version)(minor_version)(block_num)(finality_digest)(action_mroot) )
FC_REFLECT( eosio::chain::valid_t, (validation_tree)(validation_mroots))
FC_REFLECT( eosio::chain::finality_data_t, (major_version)(minor_version)(active_finalizer_policy_generation)(action_mroot)(base_digest))
FC_REFLECT_DERIVED( eosio::chain::block_state, (eosio::chain::block_header_state), (block)(strong_digest)(weak_digest)(pending_qc)(valid)(validated) )

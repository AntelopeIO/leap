Below, `parent` refers to the `block_state` of the parent block from which a new block is being constructed.

## dpos data

currently in controller.cpp, we have the `building_block` whose members are:

```c++
struct building_block {
   pending_block_header_state                 _pending_block_header_state;       // IF: Remove from building_block. See below for replacements.
   std::optional<producer_authority_schedule> _new_pending_producer_schedule;    // IF: Replaced by new_proposal_policy.
   vector<digest_type>                        _new_protocol_feature_activations; // IF: Comes from building_block_input::new_protocol_feature_activations
   size_t                                     _num_new_protocol_features_that_have_activated = 0; // Stays only in building_block
   deque<transaction_metadata_ptr>            _pending_trx_metas;                // Moved from building_block to assembled_block
   deque<transaction_receipt>                 _pending_trx_receipts;             // Moved from building_block to the transactions in the constructed block
   std::variant<checksum256_type, digests_t>  _trx_mroot_or_receipt_digests;     // IF: Extract computed trx mroot to assembled_block_input::transaction_mroot 
   digests_t                                  _action_receipt_digests;           // IF: Extract computed action mroot to assembled_block_input::action_mroot
};
```

the `assembled_block`:


```c++
struct assembled_block {
   block_id_type                     _id;                                        // Cache of _unsigned_block->calculate_id().
   pending_block_header_state        _pending_block_header_state;                // IF: Remove from assembled_block. See below for replacements.
   deque<transaction_metadata_ptr>   _trx_metas;                                 // Comes from building_block::_pending_trx_metas
                                                                                 // Carried over to put into block_state (optimization for fork reorgs)
   signed_block_ptr                  _unsigned_block;                            // IF: keep same member

   // if the _unsigned_block pre-dates block-signing authorities this may be present.
   std::optional<producer_authority_schedule> _new_producer_authority_cache;     // IF: Remove from assembled_block 
                                                                                 // pending_producers() not needed in IF. proposed_proposers() sufficient.
};
```

and the `pending_block_header_state`:

```c++

struct block_header_state_legacy_common {
  uint32_t                          block_num = 0;                               // IF: block_header::num_from_id(parent_id) + 1
  uint32_t                          dpos_proposed_irreversible_blocknum = 0;     // Unneeded for IF
  uint32_t                          dpos_irreversible_blocknum = 0;              // Unneeded during the building block stage for IF
  producer_authority_schedule       active_schedule;                             // IF: Replaced by active_proposer_policy stored in building_block.
  incremental_merkle                blockroot_merkle;                            // Unneeded during the building block stage for IF
  flat_map<account_name,uint32_t>   producer_to_last_produced;                   // Unneeded for IF
  flat_map<account_name,uint32_t>   producer_to_last_implied_irb;                // Unneeded for IF
  block_signing_authority           valid_block_signing_authority;               // IF: Get from within active_proposer_policy for building_block.producer.
  vector<uint8_t>                   confirm_count;                               // Unneeded for IF
};

struct pending_block_header_state : public detail::block_header_state_legacy_common {
   protocol_feature_activation_set_ptr  prev_activated_protocol_features;        // IF: building_block.prev_activated_protocol_features
   detail::schedule_info                prev_pending_schedule;                   // Unneeded for IF
   bool                                 was_pending_promoted = false;            // Unneeded for IF
   block_id_type                        previous;                                // Not needed but present anyway at building_block.parent_id
   account_name                         producer;                                // IF: building_block.producer
   block_timestamp_type                 timestamp;                               // IF: building_block.timestamp
   uint32_t                             active_schedule_version = 0;             // Unneeded for IF
   uint16_t                             confirmed = 1;                           // Unneeded for IF
};
```

and all this lives in `pending_state` which I believe can stay unchanged.

## IF data

The new storage for IF is:

```c++
struct block_header_state_core {
   uint32_t                last_final_block_num = 0;     // last irreversible (final) block.
   std::optional<uint32_t> final_on_strong_qc_block_num; // will become final if this header achives a strong QC.
   uint32_t                last_qc_block_num;            //
   uint32_t                finalizer_policy_generation;  

   block_header_state_core next(uint32_t last_qc_block_num, bool is_last_qc_strong) const;
};

struct quorum_certificate {
  uint32_t                 block_num;
  valid_quorum_certificate qc;
};

struct block_header_state {
   block_header                         header;
   protocol_feature_activation_set_ptr  activated_protocol_features;
   block_header_state_core              core;
   incremental_merkle_tree              proposal_mtree;
   incremental_merkle_tree              finality_mtree;
   finalizer_policy_ptr                 active_finalizer_policy; // finalizer set + threshold + generation, supports `digest()`
   proposer_policy_ptr                  active_proposer_policy;  // producer authority schedule, supports `digest()`
   
   flat_map<block_timestamp_type, proposer_policy_ptr>  proposer_policies;
   flat_map<uint32_t, finalizer_policy_ptr>             finalizer_policies;

   digest_type           compute_finalizer_digest() const;
   
   proposer_policy_ptr   get_next_active_proposer_policy(block_timestamp_type next_timestamp) const {
      // Find latest proposer policy within proposer_policies that has an active_time <= next_timestamp.
      // If found, return the proposer policy that was found.
      // Otherwise, return active_proposer_policy.
   }
   
   block_timestamp_type  timestamp() const  { return header.timestamp; }
   account_name          producer() const  { return header.producer; }
   block_id_type         previous() const  { return header.previous; }
   uint32_t              block_num() const { return block_header::num_from_id(previous()) + 1; }
   
   // block descending from this need the provided qc in the block extension
   bool                  is_needed(const quorum_certificate& qc) const {
      return qc.block_num > core.last_qc_block_num;
   }
   
   block_header_state   next(const block_header_state_input& data) const;
};

struct block_state {
   const block_header_state          bhs; 
   const signed_block_ptr            block;
   
   const block_id_type               id;  // cache of bhs.header.calculate_id() (indexed on this field)
   const digest_type                 finalizer_digest; // cache of bhs.compute_finalizer_digest()

   std::optional<pending_quorum_certificate>  pending_qc;
   std::optional<valid_quorum_certificate>    valid_qc; 
   
   std::optional<quorum_certificate> get_best_qc() const { 
      // If pending_qc does not have a valid QC, return valid_qc.
      // Otherwise, extract the valid QC from *pending_qc.
      // Compare that to valid_qc to determine which is better: Strong beats Weak. Break tie with highest accumulated weight.
      // Return the better one.
   }
   
   uint64_t block_num() const { return block_header::num_from_id(id); }
};

```

In addition, in IF `pending_state._block_stage` will still contain the three stages: `building_block`, `assembled_block`, and `completed_block`.

1. `building_block`:

```c++
struct building_block {
   const block_header_state&                        parent;                           // Needed for block_header_state::next()
   const block_id_type                              parent_id;                        // Comes from building_block_input::parent_id
   const block_timestamp_type                       timestamp;                        // Comes from building_block_input::timestamp
   const account_name                               producer;                         // Comes from building_block_input::producer
   const vector<digest_type>                        new_protocol_feature_activations; // Comes from building_block_input::new_protocol_feature_activations
   const protocol_feature_activation_set_ptr        prev_activated_protocol_features; // Cached: parent.bhs.activated_protocol_features
   const proposer_policy_ptr                        active_proposer_policy;           // Cached: parent.bhs.get_next_active_proposer_policy(timestamp)

   // Members below start from initial state and are mutated as the block is built.
   size_t                                     num_new_protocol_features_that_have_activated = 0;
   std::optional<proposer_policy>             new_proposer_policy;
   std::optional<finalizer_policy>            new_finalizer_policy;
   deque<transaction_metadata_ptr>            pending_trx_metas;
   deque<transaction_receipt>                 pending_trx_receipts;
   std::variant<checksum256_type, digests_t>  trx_mroot_or_receipt_digests;
   digests_t                                  action_receipt_digests;
};
```

```
struct building_block {
   pending_block_header_state                 _pending_block_header_state;       // IF: Remove from building_block. See below for replacements.
   std::optional<producer_authority_schedule> _new_pending_producer_schedule;    // IF: Replaced by new_proposal_policy.
   vector<digest_type>                        _new_protocol_feature_activations; // IF: Comes from building_block_input::new_protocol_feature_activations
   size_t                                     _num_new_protocol_features_that_have_activated = 0; // Stays only in building_block
   deque<transaction_metadata_ptr>            _pending_trx_metas;                // Moved from building_block to assembled_block
   deque<transaction_receipt>                 _pending_trx_receipts;             // Moved from building_block to the transactions in the constructed block
   std::variant<checksum256_type, digests_t>  _trx_mroot_or_receipt_digests;     // IF: Extract computed trx mroot to assembled_block_input::transaction_mroot 
   digests_t                                  _action_receipt_digests;           // IF: Extract computed action mroot to assembled_block_input::action_mroot
};
```

which is constructed from:

```c++
struct building_block_input {
   block_id_type                     parent_id;
   block_timestamp_type              timestamp;
   account_name                      producer;
   vector<digest_type>               new_protocol_feature_activations;
};
```

When done with building the block, from `building_block` we can extract:

```c++
struct qc_info_t {
   uint32_t last_qc_block_num; // The block height of the most recent ancestor block that has a QC justification
   bool     is_last_qc_strong; // Whether the QC for the block referenced by last_qc_block_height is strong or weak.
};

struct block_header_state_input : public building_block_input {
   digest_type                       transaction_mroot;    // Comes from std::get<checksum256_type>(building_block::trx_mroot_or_receipt_digests)
   digest_type                       action_mroot;         // Compute root from  building_block::action_receipt_digests
   std::optional<proposer_policy>    new_proposer_policy;  // Comes from building_block::new_proposer_policy
   std::optional<finalizer_policy>   new_finalizer_policy; // Comes from building_block::new_finalizer_policy
   std::optional<qc_info_t>          qc_info; 
   // ... ?
};
```

which is the input needed to `block_header_state::next` to compute the new block header state.

2. `assembled_block`:


```c++
struct assembled_block {
   block_header_state                new_block_header_state;
   deque<transaction_metadata_ptr>   trx_metas;                 // Comes from building_block::pending_trx_metas
                                                                // Carried over to put into block_state (optimization for fork reorgs)
   deque<transaction_receipt>        trx_receipts;              // Comes from building_block::pending_trx_receipts
   std::optional<quorum_certificate> qc;                        // QC to add as block extension to new block               
};
```

which is constructed from `building_block` and `parent.bhs`.

3. `completed_block`:

```c++
struct completed_block {
   block_state_ptr  block_state;
};
```

which is constructed from `assembled_block` and a block header signature provider.
#include <eosio/chain/controller.hpp>
#include <eosio/chain/transaction_context.hpp>

#include <eosio/chain/block_log.hpp>
#include <eosio/chain/fork_database.hpp>
#include <eosio/chain/exceptions.hpp>

#include <eosio/chain/account_object.hpp>
#include <eosio/chain/code_object.hpp>
#include <eosio/chain/block_summary_object.hpp>
#include <eosio/chain/eosio_contract.hpp>
#include <eosio/chain/global_property_object.hpp>
#include <eosio/chain/protocol_state_object.hpp>
#include <eosio/chain/contract_table_objects.hpp>
#include <eosio/chain/generated_transaction_object.hpp>
#include <eosio/chain/transaction_object.hpp>
#include <eosio/chain/genesis_intrinsics.hpp>
#include <eosio/chain/whitelisted_intrinsics.hpp>
#include <eosio/chain/database_header_object.hpp>

#include <eosio/chain/protocol_feature_manager.hpp>
#include <eosio/chain/authorization_manager.hpp>
#include <eosio/chain/resource_limits.hpp>
#include <eosio/chain/subjective_billing.hpp>
#include <eosio/chain/chain_snapshot.hpp>
#include <eosio/chain/snapshot_detail.hpp>
#include <eosio/chain/thread_utils.hpp>
#include <eosio/chain/platform_timer.hpp>
#include <eosio/chain/block_header_state_utils.hpp>
#include <eosio/chain/deep_mind.hpp>
#include <eosio/chain/hotstuff/finalizer.hpp>
#include <eosio/chain/hotstuff/finalizer_policy.hpp>
#include <eosio/chain/hotstuff/finalizer_authority.hpp>
#include <eosio/chain/hotstuff/hotstuff.hpp>

#include <chainbase/chainbase.hpp>
#include <eosio/vm/allocator.hpp>
#include <fc/io/json.hpp>
#include <fc/log/logger_config.hpp>
#include <fc/scoped_exit.hpp>
#include <fc/variant_object.hpp>
#include <bls12-381/bls12-381.hpp>

#include <new>
#include <shared_mutex>
#include <utility>

namespace eosio::chain {

using resource_limits::resource_limits_manager;

using controller_index_set = index_set<
   account_index,
   account_metadata_index,
   account_ram_correction_index,
   global_property_multi_index,
   protocol_state_multi_index,
   dynamic_global_property_multi_index,
   block_summary_multi_index,
   transaction_multi_index,
   generated_transaction_multi_index,
   table_id_multi_index,
   code_index,
   database_header_multi_index
>;

using contract_database_index_set = index_set<
   key_value_index,
   index64_index,
   index128_index,
   index256_index,
   index_double_index,
   index_long_double_index
>;

class maybe_session {
   public:
      maybe_session() = default;

      maybe_session( maybe_session&& other) noexcept
         :_session(std::move(other._session))
      {
      }

      explicit maybe_session(database& db) {
         _session.emplace(db.start_undo_session(true));
      }

      maybe_session(const maybe_session&) = delete;

      void squash() {
         if (_session)
            _session->squash();
      }

      void undo() {
         if (_session)
            _session->undo();
      }

      void push() {
         if (_session)
            _session->push();
      }

      maybe_session& operator=( maybe_session&& mv )  noexcept {
         if (mv._session) {
            _session.emplace(std::move(*mv._session));
            mv._session.reset();
         } else {
            _session.reset();
         }

         return *this;
      };

   private:
      std::optional<database::session>     _session;
};

// apply methods of block_handle defined here as access to internal block_handle restricted to controller
template <class R, class F>
R apply(const block_handle& bh, F&& f) {
   if constexpr (std::is_same_v<void, R>)
      std::visit<void>(overloaded{[&](const block_state_legacy_ptr& head) { std::forward<F>(f)(head); },
                                  [&](const block_state_ptr& head) { std::forward<F>(f)(head); }
                       }, bh.internal());
   else
      return std::visit<R>(overloaded{[&](const block_state_legacy_ptr& head) -> R { return std::forward<F>(f)(head); },
                                      [&](const block_state_ptr& head) -> R { return std::forward<F>(f)(head); }
                           }, bh.internal());
}

// apply savanna block_state
template <class R, class F>
R apply_s(const block_handle& bh, F&& f) {
   if constexpr (std::is_same_v<void, R>)
      std::visit<void>(overloaded{[&](const block_state_legacy_ptr&) {},
                                  [&](const block_state_ptr& head)   { std::forward<F>(f)(head); }}, bh.internal());
   else
      return std::visit<R>(overloaded{[&](const block_state_legacy_ptr&) -> R { return {}; },
                                      [&](const block_state_ptr& head)   -> R { return std::forward<F>(f)(head); }}, bh.internal());
}

// apply legancy block_state_legacy
template <class R, class F>
R apply_l(const block_handle& bh, F&& f) {
   if constexpr (std::is_same_v<void, R>)
      std::visit<void>(overloaded{[&](const block_state_legacy_ptr& head) { std::forward<F>(f)(head); },
                                  [&](const block_state_ptr&)             {}}, bh.internal());
   else
      return std::visit<R>(overloaded{[&](const block_state_legacy_ptr& head) -> R { return std::forward<F>(f)(head); },
                                      [&](const block_state_ptr&)             -> R { return {}; }}, bh.internal());
}

struct completed_block {
   block_handle bsp;

   // to be used during Legacy to Savanna transistion where action_mroot
   // needs to be converted from Legacy merkle to Savanna merkle
   std::optional<digests_t> action_receipt_digests_savanna;

   bool is_legacy() const { return std::holds_alternative<block_state_legacy_ptr>(bsp.internal()); }

   deque<transaction_metadata_ptr> extract_trx_metas() {
      return apply<deque<transaction_metadata_ptr>>(bsp, [](auto& bsp) { return bsp->extract_trxs_metas(); });
   }

   const flat_set<digest_type>& get_activated_protocol_features() const {
      return apply<const flat_set<digest_type>&>(bsp, [](const auto& bsp) -> const flat_set<digest_type>& {
         return bsp->get_activated_protocol_features()->protocol_features;
      });
   }

   const block_id_type& id() const { return bsp.id(); }
   uint32_t block_num() const { return bsp.block_num(); }
   block_timestamp_type timestamp() const { return bsp.block_time(); }
   account_name producer() const { return bsp.producer(); }

   const producer_authority_schedule& active_producers() const {
      return apply<const producer_authority_schedule&>(bsp, [](const auto& bsp) -> const producer_authority_schedule& {
         return bsp->active_schedule_auth();
      });
   }

   const producer_authority_schedule* next_producers() const {
      return apply<const producer_authority_schedule*>(bsp,
         overloaded{[](const block_state_legacy_ptr& bsp) -> const producer_authority_schedule* {
                       return bsp->pending_schedule_auth();
                    },
                    [](const block_state_ptr& bsp) -> const producer_authority_schedule* {
                       return bsp->proposer_policies.empty()
                                 ? nullptr
                                 : &bsp->proposer_policies.begin()->second->proposer_schedule;
                    }
         });
   }

   const producer_authority_schedule* pending_producers_legacy() const {
      return apply<const producer_authority_schedule*>(bsp,
         overloaded{[](const block_state_legacy_ptr& bsp) -> const producer_authority_schedule* {
                       return &bsp->pending_schedule.schedule;
                    },
                    [](const block_state_ptr&) -> const producer_authority_schedule* { return nullptr; }
         });
   }

   bool is_protocol_feature_activated(const digest_type& digest) const {
      const auto& activated_features = get_activated_protocol_features();
      return (activated_features.find(digest) != activated_features.end());
   }

   const block_signing_authority& pending_block_signing_authority() const {
      // this should never be called on completed_block because `controller::is_building_block()` returns false
      assert(0); 
      static block_signing_authority bsa; return bsa; // just so it builds
   }
};

struct assembled_block {
   // --------------------------------------------------------------------------------
   struct assembled_block_legacy {
      block_id_type                     id;
      pending_block_header_state_legacy pending_block_header_state;
      deque<transaction_metadata_ptr>   trx_metas;
      signed_block_ptr                  unsigned_block;

      // if the unsigned_block pre-dates block-signing authorities this may be present.
      std::optional<producer_authority_schedule> new_producer_authority_cache;

      // Passed to completed_block, to be used by Legacy to Savanna transisition
      std::optional<digests_t>          action_receipt_digests_savanna;
   };

   // --------------------------------------------------------------------------------
   struct assembled_block_if {
      producer_authority                active_producer_authority; 
      block_header_state                bhs;
      deque<transaction_metadata_ptr>   trx_metas;                 // Comes from building_block::pending_trx_metas
                                                                   // Carried over to put into block_state (optimization for fork reorgs)
      deque<transaction_receipt>        trx_receipts;              // Comes from building_block::pending_trx_receipts
      std::optional<valid_t>            valid;                     // Comes from assemble_block
      std::optional<quorum_certificate> qc;                        // QC to add as block extension to new block

      block_header_state& get_bhs() { return bhs; }
   };

   std::variant<assembled_block_legacy, assembled_block_if> v;

   bool is_legacy() const { return std::holds_alternative<assembled_block_legacy>(v); }

   template <class R, class F>
   R apply_legacy(F&& f) {
      if constexpr (std::is_same_v<void, R>)
         std::visit(overloaded{[&](assembled_block_legacy& ab) { std::forward<F>(f)(ab); },
                               [&](assembled_block_if& ab)     {}}, v);
      else
         return std::visit(overloaded{[&](assembled_block_legacy& ab) -> R { return std::forward<F>(f)(ab); },
                                      [&](assembled_block_if& ab)     -> R { return {}; }}, v);
   }

   deque<transaction_metadata_ptr> extract_trx_metas() {
      return std::visit([](auto& ab) { return std::move(ab.trx_metas); }, v);
   }

   bool is_protocol_feature_activated(const digest_type& digest) const {
      // Calling is_protocol_feature_activated during the assembled_block stage is not efficient.
      // We should avoid doing it.
      // In fact for now it isn't even implemented.
      EOS_THROW( misc_exception,
                 "checking if protocol feature is activated in the assembled_block stage is not yet supported" );
      // TODO: implement this
   }

   const block_id_type& id() const {
      return std::visit(
         overloaded{[](const assembled_block_legacy& ab) -> const block_id_type& { return ab.id; },
                    [](const assembled_block_if& ab)     -> const block_id_type& { return ab.bhs.id(); }},
         v);
   }
   
   block_timestamp_type timestamp() const {
      return std::visit(
         overloaded{[](const assembled_block_legacy& ab) { return ab.pending_block_header_state.timestamp; },
                    [](const assembled_block_if& ab)     { return ab.bhs.header.timestamp; }},
         v);
   }

   uint32_t block_num() const {
      return std::visit(
         overloaded{[](const assembled_block_legacy& ab) { return ab.pending_block_header_state.block_num; },
                    [](const assembled_block_if& ab)     { return ab.bhs.block_num(); }},
         v);
   }

   account_name producer() const {
      return std::visit(
         overloaded{[](const assembled_block_legacy& ab) { return ab.pending_block_header_state.producer; },
                    [](const assembled_block_if& ab)     { return ab.active_producer_authority.producer_name; }},
         v);
   }

   const block_header& header() const {
      return std::visit(
         overloaded{[](const assembled_block_legacy& ab) -> const block_header& { return *ab.unsigned_block; },
                    [](const assembled_block_if& ab)     -> const block_header& { return ab.bhs.header; }},
         v);
   }

   const producer_authority_schedule& active_producers() const {
      return std::visit(overloaded{[](const assembled_block_legacy& ab) -> const producer_authority_schedule& {
                                      return ab.pending_block_header_state.active_schedule;
                                   },
                                   [](const assembled_block_if& ab) -> const producer_authority_schedule& {
                                      return ab.bhs.active_schedule_auth();
                                   }},
                        v);
   }

   std::optional<digests_t> get_action_receipt_digests_savanna() const {
      return std::visit(
         overloaded{[](const assembled_block_legacy& ab) -> std::optional<digests_t> { return ab.action_receipt_digests_savanna; },
                    [](const assembled_block_if& ab)     -> std::optional<digests_t> { return {}; }},
         v);
   }

   const producer_authority_schedule* next_producers() const {
      return std::visit(overloaded{[](const assembled_block_legacy& ab) -> const producer_authority_schedule* {
                                      return ab.new_producer_authority_cache.has_value()
                                                ? &ab.new_producer_authority_cache.value()
                                                : nullptr;
                                   },
                                   [](const assembled_block_if& ab) -> const producer_authority_schedule* {
                                      return ab.bhs.proposer_policies.empty()
                                                ? nullptr
                                                : &ab.bhs.proposer_policies.begin()->second->proposer_schedule;
                                   }},
                        v);
   }

   const producer_authority_schedule* pending_producers_legacy() const {
      return std::visit(
         overloaded{[](const assembled_block_legacy& ab) -> const producer_authority_schedule* {
                       return ab.new_producer_authority_cache.has_value() ? &ab.new_producer_authority_cache.value()
                                                                          : nullptr;
                    },
                    [](const assembled_block_if&) -> const producer_authority_schedule* { return nullptr; }},
         v);
   }

   const block_signing_authority& pending_block_signing_authority() const {
      return std::visit(overloaded{[](const assembled_block_legacy& ab) -> const block_signing_authority& {
                                      return ab.pending_block_header_state.valid_block_signing_authority;
                                   },
                                   [](const assembled_block_if& ab) -> const block_signing_authority& {
                                      return ab.active_producer_authority.authority;
                                   }},
                        v);
   }

   completed_block complete_block(const protocol_feature_set& pfs, validator_t validator,
                                  const signer_callback_type& signer, const block_signing_authority& valid_block_signing_authority) {
      return std::visit(overloaded{[&](assembled_block_legacy& ab) {
                                      auto bsp = std::make_shared<block_state_legacy>(
                                         std::move(ab.pending_block_header_state), std::move(ab.unsigned_block),
                                         std::move(ab.trx_metas), pfs, validator, signer);

                                      return completed_block{block_handle{std::move(bsp)}, std::move(ab.action_receipt_digests_savanna)};
                                   },
                                   [&](assembled_block_if& ab) {
                                      auto bsp = std::make_shared<block_state>(ab.bhs, std::move(ab.trx_metas),
                                                                               std::move(ab.trx_receipts), ab.valid, ab.qc, signer,
                                                                               valid_block_signing_authority);
                                      return completed_block{block_handle{std::move(bsp)}, {}};
                                   }},
                        v);
   }
};

struct building_block {
   // --------------------------------------------------------------------------------
   struct building_block_common {
      using checksum_or_digests = std::variant<checksum256_type, digests_t>;

      const vector<digest_type>           new_protocol_feature_activations;
      size_t                              num_new_protocol_features_that_have_activated = 0;
      deque<transaction_metadata_ptr>     pending_trx_metas;
      deque<transaction_receipt>          pending_trx_receipts;
      checksum_or_digests                 trx_mroot_or_receipt_digests {digests_t{}};
      action_digests_t                    action_receipt_digests;
      std::optional<finalizer_policy>     new_finalizer_policy;

      building_block_common(const vector<digest_type>& new_protocol_feature_activations,
                            action_digests_t::store_which_t store_which) :
         new_protocol_feature_activations(new_protocol_feature_activations),
         action_receipt_digests(store_which)
      {
      }
      
      bool is_protocol_feature_activated(const digest_type& digest, const flat_set<digest_type>& activated_features) const {
         if (activated_features.find(digest) != activated_features.end())
            return true;
         if (num_new_protocol_features_that_have_activated == 0)
            return false;
         auto end = new_protocol_feature_activations.begin() + num_new_protocol_features_that_have_activated;
         return (std::find(new_protocol_feature_activations.begin(), end, digest) != end);
      }

      std::function<void()> make_block_restore_point() {
         auto orig_trx_receipts_size           = pending_trx_receipts.size();
         auto orig_trx_metas_size              = pending_trx_metas.size();
         auto orig_trx_receipt_digests_size    = std::holds_alternative<digests_t>(trx_mroot_or_receipt_digests) ?
                                                 std::get<digests_t>(trx_mroot_or_receipt_digests).size() : 0;
         auto orig_action_receipt_digests_size = action_receipt_digests.size();
         return [this,
                 orig_trx_receipts_size,
                 orig_trx_metas_size,
                 orig_trx_receipt_digests_size,
                 orig_action_receipt_digests_size]()
         {
            pending_trx_receipts.resize(orig_trx_receipts_size);
            pending_trx_metas.resize(orig_trx_metas_size);
            if (std::holds_alternative<digests_t>(trx_mroot_or_receipt_digests))
               std::get<digests_t>(trx_mroot_or_receipt_digests).resize(orig_trx_receipt_digests_size);
            action_receipt_digests.resize(orig_action_receipt_digests_size);
         };
      }
   };
   
   // --------------------------------------------------------------------------------
   struct building_block_legacy : public building_block_common {
      pending_block_header_state_legacy          pending_block_header_state;
      std::optional<producer_authority_schedule> new_pending_producer_schedule;

      building_block_legacy( const block_header_state_legacy& prev,
                             block_timestamp_type when,
                             uint16_t num_prev_blocks_to_confirm,
                             const vector<digest_type>& new_protocol_feature_activations,
                             action_digests_t::store_which_t store_which)
         : building_block_common(new_protocol_feature_activations, store_which),
           pending_block_header_state(prev.next(when, num_prev_blocks_to_confirm))
      {}

      bool is_protocol_feature_activated(const digest_type& digest) const {
         return building_block_common::is_protocol_feature_activated(
            digest, pending_block_header_state.prev_activated_protocol_features->protocol_features);
      }

      uint32_t get_block_num() const { return pending_block_header_state.block_num; }
   };

   // --------------------------------------------------------------------------------
   struct building_block_if : public building_block_common {
      const block_state&                         parent;
      const block_timestamp_type                 timestamp;                        // Comes from building_block_input::timestamp
      const producer_authority                   active_producer_authority;        // Comes from parent.get_scheduled_producer(timestamp)
      const protocol_feature_activation_set_ptr  prev_activated_protocol_features; // Cached: parent.activated_protocol_features()
      const proposer_policy_ptr                  active_proposer_policy;           // Cached: parent.get_next_active_proposer_policy(timestamp)
      const uint32_t                             block_num;                        // Cached: parent.block_num() + 1

      building_block_if(const block_state& parent, const building_block_input& input, action_digests_t::store_which_t store_which)
         : building_block_common(input.new_protocol_feature_activations, store_which)
         , parent (parent)
         , timestamp(input.timestamp)
         , active_producer_authority{input.producer,
                              [&]() -> block_signing_authority {
                                 const auto& pas = parent.active_proposer_policy->proposer_schedule;
                                 for (const auto& pa : pas.producers)
                                    if (pa.producer_name == input.producer)
                                       return pa.authority;
                                 assert(0); // we should find the authority
                                 return {};
                              }()}
         , prev_activated_protocol_features(parent.activated_protocol_features)
         , active_proposer_policy(parent.active_proposer_policy)
         , block_num(parent.block_num() + 1) {}

      bool is_protocol_feature_activated(const digest_type& digest) const {
         return building_block_common::is_protocol_feature_activated(digest, prev_activated_protocol_features->protocol_features);
      }

      uint32_t get_block_num() const { return block_num; }

      uint32_t get_next_proposer_schedule_version() const {
         if (!parent.proposer_policies.empty()) {
            return (--parent.proposer_policies.end())->second->proposer_schedule.version + 1;
         }
         assert(active_proposer_policy);
         return active_proposer_policy->proposer_schedule.version + 1;
      }

   };

   std::variant<building_block_legacy, building_block_if> v;

   // legacy constructor
   building_block(const block_header_state_legacy& prev, block_timestamp_type when, uint16_t num_prev_blocks_to_confirm,
                  const vector<digest_type>& new_protocol_feature_activations) :
      v(building_block_legacy(prev, when, num_prev_blocks_to_confirm, new_protocol_feature_activations,
                              action_digests_t::store_which_t::both)) // [todo] should be both only when transition starts
   {}

   // if constructor
   building_block(const block_state& prev, const building_block_input& input) :
      v(building_block_if(prev, input, action_digests_t::store_which_t::savanna))
   {}

   bool is_legacy() const { return std::holds_alternative<building_block_legacy>(v); }

   // apply legacy, building_block_legacy
   template <class R, class F>
   R apply_l(F&& f) {
      if constexpr (std::is_same_v<void, R>)
         std::visit(overloaded{[&](building_block_legacy& bb) { std::forward<F>(f)(bb); },
                               [&](building_block_if& bb)   {}}, v);
      else
         return std::visit(overloaded{[&](building_block_legacy& bb) -> R { return std::forward<F>(f)(bb); },
                                      [&](building_block_if& bb)   -> R { return {}; }}, v);
   }

   void set_proposed_finalizer_policy(const finalizer_policy& fin_pol) {
      std::visit([&](auto& bb) { bb.new_finalizer_policy = fin_pol; }, v);
   }

   deque<transaction_metadata_ptr> extract_trx_metas() {
      return std::visit([](auto& bb) { return std::move(bb.pending_trx_metas); }, v);
   }

   bool is_protocol_feature_activated(const digest_type& digest) const {
      return std::visit([&digest](const auto& bb) { return bb.is_protocol_feature_activated(digest); }, v);
   }

   std::function<void()> make_block_restore_point() {
      return std::visit([](auto& bb) { return bb.make_block_restore_point(); }, v);
   }

   uint32_t block_num() const {
      return std::visit([](const auto& bb) { return bb.get_block_num(); }, v);
   }

   block_timestamp_type timestamp() const {
      return std::visit(
         overloaded{[](const building_block_legacy& bb)  { return bb.pending_block_header_state.timestamp; },
                    [](const building_block_if& bb)    { return bb.timestamp; }},
         v);
   }

   account_name producer() const {
      return std::visit(
         overloaded{[](const building_block_legacy& bb)  { return bb.pending_block_header_state.producer; },
                    [](const building_block_if& bb)    { return bb.active_producer_authority.producer_name; }},
         v);
   }

   const vector<digest_type>& new_protocol_feature_activations() {
      return std::visit([](auto& bb) -> const vector<digest_type>& { return bb.new_protocol_feature_activations; }, v);
   }

   const block_signing_authority& pending_block_signing_authority() const {
      return std::visit(overloaded{[](const building_block_legacy& bb) -> const block_signing_authority& {
                                      return bb.pending_block_header_state.valid_block_signing_authority;
                                   },
                                   [](const building_block_if& bb) -> const block_signing_authority& {
                                      return bb.active_producer_authority.authority;
                                   }},
                        v);
   }

   int64_t get_next_proposer_schedule_version() const {
      return std::visit(
         overloaded{[](const building_block_legacy&) -> int64_t { return -1; },
                    [&](const building_block_if& bb) -> int64_t { return bb.get_next_proposer_schedule_version(); }
                   },
         v);
   }

   size_t& num_new_protocol_features_activated() {
      return std::visit([](auto& bb) -> size_t& { return bb.num_new_protocol_features_that_have_activated; }, v);
   }

   deque<transaction_metadata_ptr>& pending_trx_metas() {
      return std::visit([](auto& bb) -> deque<transaction_metadata_ptr>& { return bb.pending_trx_metas; }, v);
   }

   deque<transaction_receipt>& pending_trx_receipts() {
      return std::visit([](auto& bb) -> deque<transaction_receipt>& { return bb.pending_trx_receipts; }, v);
   }

   building_block_common::checksum_or_digests& trx_mroot_or_receipt_digests() {
      return std::visit(
         [](auto& bb) -> building_block_common::checksum_or_digests& { return bb.trx_mroot_or_receipt_digests; }, v);
   }

   action_digests_t& action_receipt_digests() {
       return std::visit([](auto& bb) -> action_digests_t& { return bb.action_receipt_digests; }, v);
   }

   const producer_authority_schedule& active_producers() const {
      return std::visit(overloaded{[](const building_block_legacy& bb) -> const producer_authority_schedule& {
                                      return bb.pending_block_header_state.active_schedule;
                                   },
                                   [](const building_block_if& bb) -> const producer_authority_schedule& {
                                      return bb.active_proposer_policy->proposer_schedule;
                                   }},
                        v);
   }

   const producer_authority_schedule* next_producers() const {
      return std::visit(overloaded{[](const building_block_legacy& bb) -> const producer_authority_schedule* {
                                      if (bb.new_pending_producer_schedule)
                                         return &bb.new_pending_producer_schedule.value();
                                      return &bb.pending_block_header_state.prev_pending_schedule.schedule;
                                   },
                                   [](const building_block_if& bb) -> const producer_authority_schedule* {
                                      if (!bb.parent.proposer_policies.empty())
                                         return &bb.parent.proposer_policies.begin()->second->proposer_schedule;
                                      return nullptr;
                                   }},
                        v);
   }

   const producer_authority_schedule* pending_producers_legacy() const {
      return std::visit(overloaded{[](const building_block_legacy& bb) -> const producer_authority_schedule* {
                                      if (bb.new_pending_producer_schedule)
                                         return &bb.new_pending_producer_schedule.value();
                                      return &bb.pending_block_header_state.prev_pending_schedule.schedule;
                                   },
                                   [](const building_block_if&) -> const producer_authority_schedule* {
                                      return nullptr;
                                   }},
                        v);
   }

   qc_data_t get_qc_data(fork_database& fork_db, const block_state& parent) {
      // find most recent ancestor block that has a QC by traversing fork db
      // branch from parent

      return fork_db.apply_s<qc_data_t>([&](const auto& forkdb) {
         auto branch = forkdb.fetch_branch(parent.id());

         for( auto it = branch.begin(); it != branch.end(); ++it ) {
            if( auto qc = (*it)->get_best_qc(); qc ) {
               EOS_ASSERT( qc->block_num <= block_header::num_from_id(parent.id()), block_validate_exception,
                           "most recent ancestor QC block number (${a}) cannot be greater than parent's block number (${p})",
                           ("a", qc->block_num)("p", block_header::num_from_id(parent.id())) );
               auto qc_claim = qc->to_qc_claim();
               if( parent.is_needed(qc_claim) ) {
                  return qc_data_t{ *qc, qc_claim };
               } else {
                  // no new qc info, repeat existing
                  return qc_data_t{ {},  parent.core.latest_qc_claim() };
               }
            }
         }

         // This only happens when parent block is the IF genesis block or starting from snapshot.
         // There is no ancestor block which has a QC. Construct a default QC claim.
         return qc_data_t{ {}, parent.core.latest_qc_claim() };
      });
   }

   assembled_block assemble_block(boost::asio::io_context& ioc,
                                  const protocol_feature_set& pfs,
                                  fork_database& fork_db,
                                  std::unique_ptr<proposer_policy> new_proposer_policy,
                                  bool validating,
                                  std::optional<qc_data_t> validating_qc_data,
                                  const block_state_ptr& validating_bsp) {
      auto& action_receipts = action_receipt_digests();
      return std::visit(
         overloaded{
            [&](building_block_legacy& bb) -> assembled_block {
               // compute the action_mroot and transaction_mroot
               auto [transaction_mroot, action_mroot] = std::visit(
                  overloaded{[&](digests_t& trx_receipts) { // calculate the two merkle roots in separate threads
                                auto trx_merkle_fut =
                                   post_async_task(ioc, [&]() { return legacy_merkle(std::move(trx_receipts)); });
                                auto action_merkle_fut =
                                   post_async_task(ioc, [&]() { return legacy_merkle(std::move(*action_receipts.digests_l)); });
                                return std::make_pair(trx_merkle_fut.get(), action_merkle_fut.get());
                             },
                             [&](const checksum256_type& trx_checksum) {
                                return std::make_pair(trx_checksum, legacy_merkle(std::move(*action_receipts.digests_l)));
                             }},
                  trx_mroot_or_receipt_digests());

               // in dpos, we create a signed_block here. In IF mode, we do it later (when we are ready to sign it)
               auto block_ptr = std::make_shared<signed_block>(bb.pending_block_header_state.make_block_header(
                  transaction_mroot, action_mroot, bb.new_pending_producer_schedule, std::move(bb.new_finalizer_policy),
                  vector<digest_type>(bb.new_protocol_feature_activations), pfs));

               block_ptr->transactions = std::move(bb.pending_trx_receipts);

               return assembled_block{
                  .v = assembled_block::assembled_block_legacy{block_ptr->calculate_id(),
                                                             std::move(bb.pending_block_header_state),
                                                             std::move(bb.pending_trx_metas), std::move(block_ptr),
                                                             std::move(bb.new_pending_producer_schedule),
                                                             std::move(bb.action_receipt_digests.digests_s)}
               };
            },
            [&](building_block_if& bb) -> assembled_block {
               // compute the action_mroot and transaction_mroot
               auto [transaction_mroot, action_mroot] = std::visit(
                  overloaded{[&](digests_t& trx_receipts) { // calculate the two merkle roots in separate threads
                                auto trx_merkle_fut =
                                   post_async_task(ioc, [&]() { return calculate_merkle(std::move(trx_receipts)); });
                                auto action_merkle_fut =
                                   post_async_task(ioc, [&]() { return calculate_merkle(std::move(*action_receipts.digests_s)); });
                                return std::make_pair(trx_merkle_fut.get(), action_merkle_fut.get());
                             },
                             [&](const checksum256_type& trx_checksum) {
                                return std::make_pair(trx_checksum, calculate_merkle(std::move(*action_receipts.digests_s)));
                             }},
                  trx_mroot_or_receipt_digests());

               qc_data_t qc_data;
               digest_type finality_mroot_claim;

               if (validating) {
                  // we are simulating a block received from the network. Use the embedded qc from the block
                  assert(validating_qc_data);
                  qc_data = *validating_qc_data;

                  assert(validating_bsp);
                  // Use the action_mroot from raceived block's header for
                  // finality_mroot_claim at the first stage such that the next
                  // block's header and block id can be built. The actual
                  // finality_mroot will be validated by apply_block at the
                  // second stage
                  finality_mroot_claim = validating_bsp->header.action_mroot;
               } else {
                  qc_data = get_qc_data(fork_db, bb.parent);;
                  finality_mroot_claim = bb.parent.get_finality_mroot_claim(qc_data.qc_claim);
               }

               building_block_input bb_input {
                  .parent_id = bb.parent.id(),
                  .parent_timestamp = bb.parent.timestamp(),
                  .timestamp = timestamp(),
                  .producer  = producer(),
                  .new_protocol_feature_activations = new_protocol_feature_activations()
               };

               block_header_state_input bhs_input{
                  bb_input,
                  transaction_mroot,
                  std::move(new_proposer_policy),
                  std::move(bb.new_finalizer_policy),
                  qc_data.qc_claim,
                  std::move(finality_mroot_claim)
               };

               auto bhs = bb.parent.next(bhs_input);

               std::optional<valid_t> valid; // used for producing

               if (validating) {
                  // Create the valid structure for validating_bsp if it does not
                  // have one.
                  if (!validating_bsp->valid) {
                     validating_bsp->valid = bb.parent.new_valid(bhs, action_mroot);
                  }
               } else {
                  // Create the valid structure for producing
                  valid = bb.parent.new_valid(bhs, action_mroot);
               }

               assembled_block::assembled_block_if ab{
                  bb.active_producer_authority,
                  bhs,
                  std::move(bb.pending_trx_metas),
                  std::move(bb.pending_trx_receipts),
                  valid,
                  qc_data.qc
               };

               return assembled_block{.v = std::move(ab)};
            }},
         v);
   }
};


using block_stage_type = std::variant<building_block, assembled_block, completed_block>;
      
struct pending_state {
   maybe_session                  _db_session;
   block_stage_type               _block_stage;
   controller::block_status       _block_status = controller::block_status::ephemeral;
   std::optional<block_id_type>   _producer_block_id;
   controller::block_report       _block_report{};

   pending_state(maybe_session&& s,
                 const block_header_state_legacy& prev,
                 block_timestamp_type when,
                 uint16_t num_prev_blocks_to_confirm,
                 const vector<digest_type>& new_protocol_feature_activations)
   :_db_session(std::move(s))
   ,_block_stage(building_block(prev, when, num_prev_blocks_to_confirm, new_protocol_feature_activations))
   {}

   pending_state(maybe_session&& s,
                 const block_state& prev,
                 const building_block_input& input) :
      _db_session(std::move(s)),
      _block_stage(building_block(prev, input))
   {}

   deque<transaction_metadata_ptr> extract_trx_metas() {
      return std::visit([](auto& stage) { return stage.extract_trx_metas(); }, _block_stage);
   }

   bool is_protocol_feature_activated(const digest_type& digest) const {
      return std::visit([&](const auto& stage) { return stage.is_protocol_feature_activated(digest); }, _block_stage);
   }

   block_timestamp_type timestamp() const {
      return std::visit([](const auto& stage) { return stage.timestamp(); }, _block_stage);
   }
   
   uint32_t block_num() const {
      return std::visit([](const auto& stage) { return stage.block_num(); }, _block_stage);
   }

   account_name producer() const {
      return std::visit([](const auto& stage) { return stage.producer(); }, _block_stage);
   }

   void push() {
      _db_session.push();
   }

   bool is_legacy() const { return std::visit([](const auto& stage) { return stage.is_legacy(); }, _block_stage); }
   
   const block_signing_authority& pending_block_signing_authority() const {
      return std::visit(
         [](const auto& stage) -> const block_signing_authority& { return stage.pending_block_signing_authority(); },
         _block_stage);
   }

   const producer_authority_schedule& active_producers() const {
      return std::visit(
         [](const auto& stage) -> const producer_authority_schedule& { return stage.active_producers(); },
         _block_stage);
   }

   const producer_authority_schedule* pending_producers_legacy() const {
      return std::visit(
         [](const auto& stage) -> const producer_authority_schedule* { return stage.pending_producers_legacy(); },
         _block_stage);
   }

   const producer_authority_schedule* next_producers()const {
      return std::visit(
         [](const auto& stage) -> const producer_authority_schedule* { return stage.next_producers(); },
         _block_stage);
   }

   int64_t get_next_proposer_schedule_version() const {
      return std::visit(overloaded{
                           [](const building_block& stage) -> int64_t { return stage.get_next_proposer_schedule_version(); },
                           [](const assembled_block&) -> int64_t { assert(false); return -1; },
                           [](const completed_block&) -> int64_t { assert(false); return -1; }
                        },
                        _block_stage);
   }
};

struct controller_impl {
   enum class app_window_type {
      write, // Only main thread is running; read-only threads are not running.
             // All read-write and read-only tasks are sequentially executed.
      read   // Main thread and read-only threads are running read-ony tasks in parallel.
             // Read-write tasks are not being executed.
   };

#if LLVM_VERSION_MAJOR < 9
   // LLVM versions prior to 9 do a set_new_handler() in a static global initializer. Reset LLVM's custom handler
   // back to the default behavior of throwing bad_alloc so we can possibly exit cleanly and not just abort as LLVM's
   // handler does.
   // LLVM 9+ doesn't install this handler unless calling InitLLVM(), which we don't.
   // See https://reviews.llvm.org/D64505
   struct reset_new_handler {
      reset_new_handler() { std::set_new_handler([](){ throw std::bad_alloc(); }); }
   };

   reset_new_handler               rnh; // placed here to allow for this to be set before constructing the other fields
#endif
   controller&                     self;
   std::function<void()>           shutdown;
   chainbase::database             db;
   block_log                       blog;
   std::optional<pending_state>    pending;
   block_handle                    chain_head;
   fork_database                   fork_db;
   block_id_type                   if_irreversible_block_id;
   resource_limits_manager         resource_limits;
   subjective_billing              subjective_bill;
   authorization_manager           authorization;
   protocol_feature_manager        protocol_features;
   controller::config              conf;
   const chain_id_type             chain_id; // read by thread_pool threads, value will not be changed
   bool                            replaying = false;
   bool                            is_producer_node = false; // true if node is configured as a block producer
   db_read_mode                    read_mode = db_read_mode::HEAD;
   bool                            in_trx_requiring_checks = false; ///< if true, checks that are normally skipped on replay (e.g. auth checks) cannot be skipped
   std::optional<fc::microseconds> subjective_cpu_leeway;
   bool                            trusted_producer_light_validation = false;
   uint32_t                        snapshot_head_block = 0;
   struct chain; // chain is a namespace so use an embedded type for the named_thread_pool tag
   named_thread_pool<chain>        thread_pool;
   deep_mind_handler*              deep_mind_logger = nullptr;
   bool                            okay_to_print_integrity_hash_on_stop = false;
   my_finalizers_t                 my_finalizers;
   std::atomic<bool>               writing_snapshot = false;

   thread_local static platform_timer timer; // a copy for main thread and each read-only thread
#if defined(EOSIO_EOS_VM_RUNTIME_ENABLED) || defined(EOSIO_EOS_VM_JIT_RUNTIME_ENABLED)
   thread_local static vm::wasm_allocator wasm_alloc; // a copy for main thread and each read-only thread
#endif
   wasm_interface wasmif;
   app_window_type app_window = app_window_type::write;

   typedef pair<scope_name,action_name>                   handler_key;
   map< account_name, map<handler_key, apply_handler> >   apply_handlers;
   unordered_map< builtin_protocol_feature_t, std::function<void(controller_impl&)>, enum_hash<builtin_protocol_feature_t> > protocol_feature_activation_handlers;

   signal<void(uint32_t)>                    block_start;
   signal<void(const block_signal_params&)>  accepted_block_header;
   signal<void(const block_signal_params&)>  accepted_block;
   signal<void(const block_signal_params&)>  irreversible_block;
   signal<void(std::tuple<const transaction_trace_ptr&, const packed_transaction_ptr&>)> applied_transaction;
   signal<void(const vote_message&)>         voted_block;

   int64_t set_proposed_producers( vector<producer_authority> producers );
   int64_t set_proposed_producers_legacy( vector<producer_authority> producers );

   protocol_feature_activation_set_ptr head_activated_protocol_features() const {
      return apply<protocol_feature_activation_set_ptr>(chain_head, [](const auto& head) {
         return head->get_activated_protocol_features();
      });
   }

   const producer_authority_schedule& head_active_schedule_auth() const {
      return apply<const producer_authority_schedule&>(chain_head, [](const auto& head) -> const producer_authority_schedule& {
         return head->active_schedule_auth();
      });
   }

   const producer_authority_schedule* head_pending_schedule_auth_legacy() const {
      return apply<const producer_authority_schedule*>(chain_head,
         overloaded{[](const block_state_legacy_ptr& head) -> const producer_authority_schedule* { return head->pending_schedule_auth(); },
                    [](const block_state_ptr&) -> const producer_authority_schedule* { return nullptr; }
         });
   }

   const producer_authority_schedule* next_producers() {
      return apply<const producer_authority_schedule*>(chain_head,
         overloaded{
         [](const block_state_legacy_ptr& head) -> const producer_authority_schedule* {
            return head->pending_schedule_auth();
         },
         [](const block_state_ptr& head) -> const producer_authority_schedule* {
            return head->proposer_policies.empty()
                      ? nullptr
                      : &head->proposer_policies.begin()->second->proposer_schedule;
         }
      });
   }

   void replace_producer_keys( const public_key_type& key ) {
      ilog("Replace producer keys with ${k}", ("k", key));

      apply<void>(chain_head,
         overloaded{
            [&](const block_state_legacy_ptr& head) {
               auto version = head->pending_schedule.schedule.version;
               head->pending_schedule = {};
               head->pending_schedule.schedule.version = version;
               for (auto& prod: head->active_schedule.producers ) {
                  ilog("${n}", ("n", prod.producer_name));
                  std::visit([&](auto &auth) {
                     auth.threshold = 1;
                     auth.keys = {key_weight{key, 1}};
                  }, prod.authority);
               }
            },
            [](const block_state_ptr&) {
               // TODO IF: add instant-finality implementation, will need to replace finalizers as well
            }
         });
   }

   // --------------- access fork_db head ----------------------------------------------------------------------
   bool fork_db_has_head() const {
      return fork_db.apply<uint32_t>([&](const auto& forkdb) { return !!forkdb.head(); });
   }

   template <typename ForkDB>
   typename ForkDB::bsp_t fork_db_head(const ForkDB& forkdb, bool irreversible_mode) const {
      if (irreversible_mode) {
         // When in IRREVERSIBLE mode fork_db blocks are marked valid when they become irreversible so that
         // fork_db.head() returns irreversible block
         // Use pending_head since this method should return the chain head and not last irreversible.
         return forkdb.pending_head();
      } else {
         return forkdb.head();
      }
   }

   uint32_t fork_db_head_block_num() const {
      return fork_db.apply<uint32_t>(
         [&](const auto& forkdb) { return fork_db_head(forkdb, irreversible_mode())->block_num(); });
   }

   block_id_type fork_db_head_block_id() const {
      return fork_db.apply<block_id_type>(
         [&](const auto& forkdb) { return fork_db_head(forkdb, irreversible_mode())->id(); });
   }

   uint32_t fork_db_head_irreversible_blocknum() const {
      return fork_db.apply<uint32_t>(
         [&](const auto& forkdb) { return fork_db_head(forkdb, irreversible_mode())->irreversible_blocknum(); });
   }

   // --------------- access fork_db root ----------------------------------------------------------------------
   bool fork_db_has_root() const {
      return fork_db.apply<bool>([&](const auto& forkdb) { return !!forkdb.has_root(); });
   }

   block_id_type fork_db_root_block_id() const {
      return fork_db.apply<block_id_type>([&](const auto& forkdb) { return forkdb.root()->id(); });
   }

   uint32_t fork_db_root_block_num() const {
      return fork_db.apply<uint32_t>([&](const auto& forkdb) { return forkdb.root()->block_num(); });
   }

   block_timestamp_type  fork_db_root_timestamp() const {
      return fork_db.apply<block_timestamp_type>([&](const auto& forkdb) { return forkdb.root()->timestamp(); });
   }

   // ---------------  fork_db APIs ----------------------------------------------------------------------
   template<typename ForkDB>
   uint32_t pop_block(ForkDB& forkdb) {
      typename ForkDB::bsp_t prev = forkdb.get_block( chain_head.previous() );

      if( !prev ) {
         EOS_ASSERT( forkdb.root()->id() == chain_head.previous(), block_validate_exception,
                     "attempt to pop beyond last irreversible block" );
         prev = forkdb.root();
      }

      EOS_ASSERT( chain_head.block(), block_validate_exception,
                  "attempting to pop a block that was sparsely loaded from a snapshot");
      chain_head = block_handle{prev};

      return prev->block_num();
   }

   bool fork_db_block_exists( const block_id_type& id ) const {
      return fork_db.apply<bool>([&](const auto& forkdb) {
         return forkdb.block_exists(id);
      });
   }

   void fork_db_reset_root_to_chain_head() {
      fork_db.reset_root(chain_head.internal());
   }

   signed_block_ptr fork_db_fetch_block_by_id( const block_id_type& id ) const {
      return fork_db.apply<signed_block_ptr>([&](const auto& forkdb) {
         auto bsp = forkdb.get_block(id);
         return bsp ? bsp->block : nullptr;
      });
   }

   signed_block_ptr fetch_block_on_head_branch_by_num(uint32_t block_num) const {
      return fork_db.apply<signed_block_ptr>([&](const auto& forkdb) {
         auto bsp = forkdb.search_on_head_branch(block_num);
         if (bsp) return bsp->block;
         return signed_block_ptr{};
      });
   }

   std::optional<block_id_type> fetch_block_id_on_head_branch_by_num(uint32_t block_num) const {
      return fork_db.apply<std::optional<block_id_type>>([&](const auto& forkdb) -> std::optional<block_id_type> {
         auto bsp = forkdb.search_on_head_branch(block_num, include_root_t::yes);
         if (bsp) return bsp->id();
         return {};
      });
   }

   // search on the branch of head
   block_state_ptr fetch_bsp_on_head_branch_by_num(uint32_t block_num) const {
      return fork_db.apply<block_state_ptr>(
         overloaded{
            [](const fork_database_legacy_t&) -> block_state_ptr { return nullptr; },
            [&](const fork_database_if_t& forkdb) -> block_state_ptr {
               return forkdb.search_on_head_branch(block_num, include_root_t::yes);
            }
         }
      );
   }

   // search on the branch of given id
   block_state_ptr fetch_bsp_on_branch_by_num(const block_id_type& id, uint32_t block_num) const {
      return fork_db.apply<block_state_ptr>(
         overloaded{
            [](const fork_database_legacy_t&) -> block_state_ptr { return nullptr; },
            [&](const fork_database_if_t& forkdb) -> block_state_ptr {
               return forkdb.search_on_branch(id, block_num, include_root_t::yes);
            }
         }
      );
   }

   block_state_ptr fetch_bsp(const block_id_type& id) const {
      return fork_db.apply<block_state_ptr>(
         overloaded{
            [](const fork_database_legacy_t&) -> block_state_ptr { return nullptr; },
            [&](const fork_database_if_t& forkdb) -> block_state_ptr {
               return forkdb.get_block(id, include_root_t::yes);
            }
         }
      );
   }

   void pop_block() {
      uint32_t prev_block_num = fork_db.apply<uint32_t>([&](auto& forkdb) {
         return pop_block(forkdb);
      });
      db.undo();
      protocol_features.popped_blocks_to(prev_block_num);
   }

   // -------------------------------------------

   template<builtin_protocol_feature_t F>
   void on_activation();

   template<builtin_protocol_feature_t F>
   inline void set_activation_handler() {
      auto res = protocol_feature_activation_handlers.emplace( F, &controller_impl::on_activation<F> );
      EOS_ASSERT( res.second, misc_exception, "attempting to set activation handler twice" );
   }

   inline void trigger_activation_handler( builtin_protocol_feature_t f ) {
      auto itr = protocol_feature_activation_handlers.find( f );
      if( itr == protocol_feature_activation_handlers.end() ) return;
      (itr->second)( *this );
   }

   void set_apply_handler( account_name receiver, account_name contract, action_name action, apply_handler v ) {
      apply_handlers[receiver][make_pair(contract,action)] = v;
   }

   controller_impl( const controller::config& cfg, controller& s, protocol_feature_set&& pfs, const chain_id_type& chain_id )
   :rnh(),
    self(s),
    db( cfg.state_dir,
        cfg.read_only ? database::read_only : database::read_write,
        cfg.state_size, false, cfg.db_map_mode ),
    blog( cfg.blocks_dir, cfg.blog ),
    fork_db(cfg.blocks_dir / config::reversible_blocks_dir_name),
    resource_limits( db, [&s](bool is_trx_transient) { return s.get_deep_mind_logger(is_trx_transient); }),
    authorization( s, db ),
    protocol_features( std::move(pfs), [&s](bool is_trx_transient) { return s.get_deep_mind_logger(is_trx_transient); } ),
    conf( cfg ),
    chain_id( chain_id ),
    read_mode( cfg.read_mode ),
    thread_pool(),
    my_finalizers{ .t_startup = fc::time_point::now(), .persist_file_path = cfg.finalizers_dir / "safety.dat" },
    wasmif( conf.wasm_runtime, conf.eosvmoc_tierup, db, conf.state_dir, conf.eosvmoc_config, !conf.profile_accounts.empty() )
   {
      thread_pool.start( cfg.thread_pool_size, [this]( const fc::exception& e ) {
         elog( "Exception in chain thread pool, exiting: ${e}", ("e", e.to_detail_string()) );
         if( shutdown ) shutdown();
      } );

      set_activation_handler<builtin_protocol_feature_t::preactivate_feature>();
      set_activation_handler<builtin_protocol_feature_t::replace_deferred>();
      set_activation_handler<builtin_protocol_feature_t::get_sender>();
      set_activation_handler<builtin_protocol_feature_t::webauthn_key>();
      set_activation_handler<builtin_protocol_feature_t::wtmsig_block_signatures>();
      set_activation_handler<builtin_protocol_feature_t::action_return_value>();
      set_activation_handler<builtin_protocol_feature_t::configurable_wasm_limits>();
      set_activation_handler<builtin_protocol_feature_t::blockchain_parameters>();
      set_activation_handler<builtin_protocol_feature_t::get_code_hash>();
      set_activation_handler<builtin_protocol_feature_t::get_block_num>();
      set_activation_handler<builtin_protocol_feature_t::crypto_primitives>();
      set_activation_handler<builtin_protocol_feature_t::bls_primitives>();
      set_activation_handler<builtin_protocol_feature_t::disable_deferred_trxs_stage_2>();
      set_activation_handler<builtin_protocol_feature_t::instant_finality>();

      irreversible_block.connect([this](const block_signal_params& t) {
         const auto& [ block, id] = t;
         wasmif.current_lib(block->block_num());
      });


#define SET_APP_HANDLER( receiver, contract, action) \
   set_apply_handler( account_name(#receiver), account_name(#contract), action_name(#action), \
                      &BOOST_PP_CAT(apply_, BOOST_PP_CAT(contract, BOOST_PP_CAT(_,action) ) ) )

   SET_APP_HANDLER( eosio, eosio, newaccount );
   SET_APP_HANDLER( eosio, eosio, setcode );
   SET_APP_HANDLER( eosio, eosio, setabi );
   SET_APP_HANDLER( eosio, eosio, updateauth );
   SET_APP_HANDLER( eosio, eosio, deleteauth );
   SET_APP_HANDLER( eosio, eosio, linkauth );
   SET_APP_HANDLER( eosio, eosio, unlinkauth );

   SET_APP_HANDLER( eosio, eosio, canceldelay );
   }

   void open_fork_db() {
      fork_db.open([this](block_timestamp_type timestamp, const flat_set<digest_type>& cur_features,
                          const vector<digest_type>& new_features) {
         check_protocol_features(timestamp, cur_features, new_features);
      });
   }

   /**
    *  Plugins / observers listening to signals emited might trigger
    *  errors and throw exceptions. Unless those exceptions are caught it could impact consensus and/or
    *  cause a node to fork.
    *
    *  If it is ever desirable to let a signal handler bubble an exception out of this method
    *  a full audit of its uses needs to be undertaken.
    *
    */
   template<typename Signal, typename Arg>
   void emit( const Signal& s, Arg&& a ) {
      try {
         s( std::forward<Arg>( a ));
      } catch (std::bad_alloc& e) {
         wlog( "std::bad_alloc: ${w}", ("w", e.what()) );
         throw e;
      } catch (boost::interprocess::bad_alloc& e) {
         wlog( "boost::interprocess::bad alloc: ${w}", ("w", e.what()) );
         throw e;
      } catch ( controller_emit_signal_exception& e ) {
         wlog( "controller_emit_signal_exception: ${details}", ("details", e.to_detail_string()) );
         throw e;
      } catch ( fc::exception& e ) {
         wlog( "fc::exception: ${details}", ("details", e.to_detail_string()) );
      } catch ( std::exception& e ) {
         wlog( "std::exception: ${details}", ("details", e.what()) );
      } catch ( ... ) {
         wlog( "signal handler threw exception" );
      }
   }

   void dmlog_applied_transaction(const transaction_trace_ptr& t, const signed_transaction* trx = nullptr) {
      // dmlog_applied_transaction is called by push_scheduled_transaction
      // where transient transactions are not possible, and by push_transaction
      // only when the transaction is not transient
      if (auto dm_logger = get_deep_mind_logger(false)) {
         if (trx && is_onblock(*t))
            dm_logger->on_onblock(*trx);
         dm_logger->on_applied_transaction(chain_head.block_num() + 1, t);
      }
   }

   void log_irreversible() {
      EOS_ASSERT( fork_db_has_root(), fork_database_exception, "fork database not properly initialized" );

      const std::optional<block_id_type> log_head_id = blog.head_id();
      const bool valid_log_head = !!log_head_id;

      const auto lib_num = valid_log_head ? block_header::num_from_id(*log_head_id) : (blog.first_block_num() - 1);

      auto root_id = fork_db_root_block_id();

      if( valid_log_head ) {
         EOS_ASSERT( root_id == log_head_id, fork_database_exception,
                     "fork database root ${rid} does not match block log head ${hid}", ("rid", root_id)("hid", log_head_id) );
      } else {
         EOS_ASSERT( fork_db_root_block_num() == lib_num, fork_database_exception,
                     "The first block ${lib_num} when starting with an empty block log should be the block after fork database root ${bn}.",
                     ("lib_num", lib_num)("bn", fork_db_root_block_num()) );
      }

      uint32_t if_lib_num = block_header::num_from_id(if_irreversible_block_id);
      const uint32_t new_lib_num = if_lib_num > 0 ? if_lib_num : fork_db_head_irreversible_blocknum();

      if( new_lib_num <= lib_num )
         return;

      auto mark_branch_irreversible = [&, this](auto& forkdb) {
         auto branch = (if_lib_num > 0) ? forkdb.fetch_branch( if_irreversible_block_id, new_lib_num)
                                        : forkdb.fetch_branch( fork_db_head(forkdb, irreversible_mode())->id(), new_lib_num );
         try {
            auto should_process = [&](auto& bsp) {
               // Only make irreversible blocks that have been validated. Blocks in the fork database may not be on our current best head
               // and therefore have not been validated.
               // An alternative more complex implementation would be to do a fork switch here and validate all blocks so they can be then made
               // irreversible. Instead this moves irreversible as much as possible and allows the next maybe_switch_forks call to apply these
               // non-validated blocks. After the maybe_switch_forks call (before next produced block or on next received block), irreversible
               // can then move forward on the then validated blocks.
               return read_mode == db_read_mode::IRREVERSIBLE || bsp->is_valid();
            };

            std::vector<std::future<std::vector<char>>> v;
            v.reserve( branch.size() );
            for( auto bitr = branch.rbegin(); bitr != branch.rend() && should_process(*bitr); ++bitr ) {
               v.emplace_back( post_async_task( thread_pool.get_executor(), [b=(*bitr)->block]() { return fc::raw::pack(*b); } ) );
            }
            auto it = v.begin();

            for( auto bitr = branch.rbegin(); bitr != branch.rend() && should_process(*bitr); ++bitr ) {
               if( read_mode == db_read_mode::IRREVERSIBLE ) {
                  controller::block_report br;
                  apply_block( br, *bitr, controller::block_status::complete, trx_meta_cache_lookup{} );
               }

               emit( irreversible_block, std::tie((*bitr)->block, (*bitr)->id()) );

               // blog.append could fail due to failures like running out of space.
               // Do it before commit so that in case it throws, DB can be rolled back.
               blog.append( (*bitr)->block, (*bitr)->id(), it->get() );
               ++it;

               db.commit( (*bitr)->block_num() );
               root_id = (*bitr)->id();
            }
         } catch( std::exception& ) {
            if( root_id != forkdb.root()->id() ) {
               forkdb.advance_root( root_id );
            }
            throw;
         }

         //db.commit( new_lib ); // redundant

         if( root_id != forkdb.root()->id() ) {
            branch.emplace_back(forkdb.root());
            forkdb.advance_root( root_id );
         }

         // delete branch in thread pool
         boost::asio::post( thread_pool.get_executor(), [branch{std::move(branch)}]() {} );
      };

      fork_db.apply<void>(mark_branch_irreversible);
   }

   void initialize_blockchain_state(const genesis_state& genesis) {
      ilog( "Initializing new blockchain with genesis state" );

      // genesis state starts in legacy mode
      producer_authority_schedule initial_schedule = { 0, { producer_authority{config::system_account_name, block_signing_authority_v0{ 1, {{genesis.initial_key, 1}} } } } };
      legacy::producer_schedule_type initial_legacy_schedule{ 0, {{config::system_account_name, genesis.initial_key}} };

      block_header_state_legacy genheader;
      genheader.active_schedule                = initial_schedule;
      genheader.pending_schedule.schedule      = initial_schedule;
      // NOTE: if wtmsig block signatures are enabled at genesis time this should be the hash of a producer authority schedule
      genheader.pending_schedule.schedule_hash = fc::sha256::hash(initial_legacy_schedule);
      genheader.header.timestamp               = genesis.initial_timestamp;
      genheader.header.action_mroot            = genesis.compute_chain_id();
      genheader.id                             = genheader.header.calculate_id();
      genheader.block_num                      = genheader.header.block_num();

      auto head = std::make_shared<block_state_legacy>();
      static_cast<block_header_state_legacy&>(*head) = genheader;
      head->activated_protocol_features = std::make_shared<protocol_feature_activation_set>(); // no activated protocol features in genesis
      head->block = std::make_shared<signed_block>(genheader.header);
      chain_head = block_handle{head};

      db.set_revision( chain_head.block_num() );
      initialize_database(genesis);
   }

   enum class startup_t { genesis, snapshot, existing_state };

   std::exception_ptr replay_block_log(const std::function<bool()>& check_shutdown) {
      auto blog_head = blog.head();
      if (!blog_head) {
         ilog( "no block log found" );
         return {};
      }

      auto start_block_num = chain_head.block_num() + 1;
      auto start = fc::time_point::now();

      std::exception_ptr except_ptr;
      if( start_block_num <= blog_head->block_num() ) {
         ilog( "existing block log, attempting to replay from ${s} to ${n} blocks", ("s", start_block_num)("n", blog_head->block_num()) );
         try {
            while( auto next = blog.read_block_by_num( chain_head.block_num() + 1 ) ) {
               apply<void>(chain_head, [&](const auto& head) {
                  replay_push_block<std::decay_t<decltype(head)>>( next, controller::block_status::irreversible );
               });
               if( check_shutdown() ) break;
               if( next->block_num() % 500 == 0 ) {
                  ilog( "${n} of ${head}", ("n", next->block_num())("head", blog_head->block_num()) );
               }
            }
         } catch(  const database_guard_exception& e ) {
            except_ptr = std::current_exception();
         }
         auto end = fc::time_point::now();
         ilog( "${n} irreversible blocks replayed", ("n", 1 + chain_head.block_num() - start_block_num) );
         ilog( "replayed ${n} blocks in ${duration} seconds, ${mspb} ms/block",
               ("n", chain_head.block_num() + 1 - start_block_num)("duration", (end-start).count()/1000000)
               ("mspb", ((end-start).count()/1000.0)/(chain_head.block_num()-start_block_num)) );

         // if the irreverible log is played without undo sessions enabled, we need to sync the
         // revision ordinal to the appropriate expected value here.
         if( skip_db_sessions( controller::block_status::irreversible ) )
            db.set_revision( chain_head.block_num() );
      } else {
         ilog( "no irreversible blocks need to be replayed" );
      }

      return except_ptr;
   }

   void replay(const std::function<bool()>& check_shutdown, startup_t startup) {
      replaying = true;

      auto blog_head = blog.head();
      auto start_block_num = chain_head.block_num() + 1;
      std::exception_ptr except_ptr;

      if (blog_head) {
         except_ptr = replay_block_log(check_shutdown);
      } else {
         ilog( "no block log found" );
      }

      try {
         if (startup != startup_t::existing_state)
           open_fork_db();
      } catch (const fc::exception& e) {
         elog( "Unable to open fork database, continuing without reversible blocks: ${e}", ("e", e));
      }

      if (startup == startup_t::genesis) {
         if (fork_db.version_in_use() == fork_database::in_use_t::legacy) {
            // switch to savanna if needed
            apply_s<void>(chain_head, [&](const auto& head) {
               fork_db.switch_from_legacy(chain_head.internal());
            });
         }
         auto do_startup = [&](auto& forkdb) {
            if( forkdb.head() ) {
               if( read_mode == db_read_mode::IRREVERSIBLE && forkdb.head()->id() != forkdb.root()->id() ) {
                  forkdb.rollback_head_to_root();
               }
               wlog( "No existing chain state. Initializing fresh blockchain state." );
            } else {
               wlog( "No existing chain state or fork database. Initializing fresh blockchain state and resetting fork database.");
            }

            if( !forkdb.head() ) {
               fork_db_reset_root_to_chain_head();
            }
         };
         fork_db.apply<void>(do_startup);
      }

      if( !fork_db_has_root() ) {
         fork_db_reset_root_to_chain_head();
      }

      auto replay_fork_db = [&](auto& forkdb) {
         using BSP = std::decay_t<decltype(forkdb.head())>;

         auto pending_head = forkdb.pending_head();
         if( pending_head && blog_head && start_block_num <= blog_head->block_num() ) {
            ilog( "fork database head ${h}, root ${r}", ("h", pending_head->block_num())( "r", forkdb.root()->block_num() ) );
            if( pending_head->block_num() < chain_head.block_num() || chain_head.block_num() < forkdb.root()->block_num() ) {
               ilog( "resetting fork database with new last irreversible block as the new root: ${id}", ("id", chain_head.id()) );
               fork_db_reset_root_to_chain_head();
            } else if( chain_head.block_num() != forkdb.root()->block_num() ) {
               auto new_root = forkdb.search_on_branch( pending_head->id(), chain_head.block_num() );
               EOS_ASSERT( new_root, fork_database_exception,
                           "unexpected error: could not find new LIB in fork database" );
               ilog( "advancing fork database root to new last irreversible block within existing fork database: ${id}",
                     ("id", new_root->id()) );
               forkdb.mark_valid( new_root );
               forkdb.advance_root( new_root->id() );
            }
         }

         if (snapshot_head_block != 0 && !blog.head()) {
            // loading from snapshot without a block log so fork_db can't be considered valid
            fork_db_reset_root_to_chain_head();
         } else if( !except_ptr && !check_shutdown() && forkdb.head() ) {
            auto head_block_num = chain_head.block_num();
            auto branch = fork_db.fetch_branch_from_head();
            int rev = 0;
            for( auto i = branch.rbegin(); i != branch.rend(); ++i ) {
               if( check_shutdown() ) break;
               if( (*i)->block_num() <= head_block_num ) continue;
               ++rev;
               replay_push_block<BSP>( *i, controller::block_status::validated );
            }
            ilog( "${n} reversible blocks replayed", ("n",rev) );
         }

         if( !forkdb.head() ) {
            fork_db_reset_root_to_chain_head();
         }

      };
      fork_db.apply<void>(replay_fork_db);

      replaying = false;

      if( except_ptr ) {
         std::rethrow_exception( except_ptr );
      }
   }

   void startup(std::function<void()> shutdown, std::function<bool()> check_shutdown, const snapshot_reader_ptr& snapshot) {
      EOS_ASSERT( snapshot, snapshot_exception, "No snapshot reader provided" );
      this->shutdown = shutdown;
      try {
         auto snapshot_load_start_time = fc::time_point::now();
         snapshot->validate();
         if( auto blog_head = blog.head() ) {
            ilog( "Starting initialization from snapshot and block log ${b}-${e}, this may take a significant amount of time",
                  ("b", blog.first_block_num())("e", blog_head->block_num()) );
            read_from_snapshot( snapshot, blog.first_block_num(), blog_head->block_num() );
         } else {
            ilog( "Starting initialization from snapshot and no block log, this may take a significant amount of time" );
            read_from_snapshot( snapshot, 0, std::numeric_limits<uint32_t>::max() );
            EOS_ASSERT( chain_head.block_num() > 0, snapshot_exception,
                        "Snapshot indicates controller head at block number 0, but that is not allowed. "
                        "Snapshot is invalid." );
            blog.reset( chain_id, chain_head.block_num() + 1 );
         }
         ilog( "Snapshot loaded, lib: ${lib}", ("lib", chain_head.block_num()) );

         init(std::move(check_shutdown), startup_t::snapshot);
         auto snapshot_load_time = (fc::time_point::now() - snapshot_load_start_time).to_seconds();
         ilog( "Finished initialization from snapshot (snapshot load time was ${t}s)", ("t", snapshot_load_time) );
      } catch (boost::interprocess::bad_alloc& e) {
         elog( "Failed initialization from snapshot - db storage not configured to have enough storage for the provided snapshot, please increase and retry snapshot" );
         shutdown();
      }
   }

   void startup(std::function<void()> shutdown, std::function<bool()> check_shutdown, const genesis_state& genesis) {
      EOS_ASSERT( db.revision() < 1, database_exception, "This version of controller::startup only works with a fresh state database." );
      const auto& genesis_chain_id = genesis.compute_chain_id();
      EOS_ASSERT( genesis_chain_id == chain_id, chain_id_type_exception,
                  "genesis state provided to startup corresponds to a chain ID (${genesis_chain_id}) that does not match the chain ID that controller was constructed with (${controller_chain_id})",
                  ("genesis_chain_id", genesis_chain_id)("controller_chain_id", chain_id)
      );

      this->shutdown = std::move(shutdown);

      initialize_blockchain_state(genesis); // sets chain_head to genesis state

      if( blog.head() ) {
         EOS_ASSERT( blog.first_block_num() == 1, block_log_exception, "block log does not start with genesis block" );
      } else {
         blog.reset( genesis, chain_head.block() );
      }

      init(std::move(check_shutdown), startup_t::genesis);
   }

   void startup(std::function<void()> shutdown, std::function<bool()> check_shutdown) {
      EOS_ASSERT( db.revision() >= 1, database_exception,
                  "This version of controller::startup does not work with a fresh state database." );

      open_fork_db();

      EOS_ASSERT( fork_db_has_head(), fork_database_exception,
                  "No existing fork database despite existing chain state. Replay required." );

      this->shutdown = std::move(shutdown);
      uint32_t lib_num = fork_db_root_block_num();
      auto first_block_num = blog.first_block_num();
      if( auto blog_head = blog.head() ) {
         EOS_ASSERT( first_block_num <= lib_num && lib_num <= blog_head->block_num(),
                     block_log_exception,
                     "block log (ranging from ${block_log_first_num} to ${block_log_last_num}) does not contain the last irreversible block (${fork_db_lib})",
                     ("block_log_first_num", first_block_num)
                     ("block_log_last_num", blog_head->block_num())
                     ("fork_db_lib", lib_num)
         );
         lib_num = blog_head->block_num();
      } else {
         if( first_block_num != (lib_num + 1) ) {
            blog.reset( chain_id, lib_num + 1 );
         }
      }

      auto do_startup = [&](auto& forkdb) {
         if( read_mode == db_read_mode::IRREVERSIBLE && forkdb.head()->id() != forkdb.root()->id() ) {
            forkdb.rollback_head_to_root();
         }
         chain_head = block_handle{forkdb.head()};
      };

      fork_db.apply<void>(do_startup);

      init(std::move(check_shutdown), startup_t::existing_state);
   }


   static auto validate_db_version( const chainbase::database& db ) {
      // check database version
      const auto& header_idx = db.get_index<database_header_multi_index>().indices().get<by_id>();

      EOS_ASSERT(header_idx.begin() != header_idx.end(), bad_database_version_exception,
                 "state database version pre-dates versioning, please restore from a compatible snapshot or replay!");

      auto header_itr = header_idx.begin();
      header_itr->validate();

      return header_itr;
   }

   void init(std::function<bool()> check_shutdown, startup_t startup) {
      auto header_itr = validate_db_version( db );

      {
         const auto& state_chain_id = db.get<global_property_object>().chain_id;
         EOS_ASSERT( state_chain_id == chain_id, chain_id_type_exception,
                     "chain ID in state (${state_chain_id}) does not match the chain ID that controller was constructed with (${controller_chain_id})",
                     ("state_chain_id", state_chain_id)("controller_chain_id", chain_id)
         );
      }

      // upgrade to the latest compatible version
      if (header_itr->version != database_header_object::current_version) {
         db.modify(*header_itr, [](auto& header) {
            header.version = database_header_object::current_version;
         });
      }

      // At this point head != nullptr
      EOS_ASSERT( db.revision() >= chain_head.block_num(), fork_database_exception,
                  "fork database head (${head}) is inconsistent with state (${db})",
                  ("db", db.revision())("head", chain_head.block_num()) );

      if( db.revision() > chain_head.block_num() ) {
         wlog( "database revision (${db}) is greater than head block number (${head}), "
               "attempting to undo pending changes",
               ("db", db.revision())("head", chain_head.block_num()) );
      }
      while( db.revision() > chain_head.block_num() ) {
         db.undo();
      }

      protocol_features.init( db );

      // At startup, no transaction specific logging is possible
      if (auto dm_logger = get_deep_mind_logger(false)) {
         dm_logger->on_startup(db, chain_head.block_num());
      }

      if( conf.integrity_hash_on_start )
         ilog( "chain database started with hash: ${hash}", ("hash", calculate_integrity_hash()) );
      okay_to_print_integrity_hash_on_stop = true;

      replay( check_shutdown, startup ); // replay any irreversible and reversible blocks ahead of current head

      if( check_shutdown() ) return;

      // At this point head != nullptr && fork_db.head() != nullptr && fork_db.root() != nullptr.
      // Furthermore, fork_db.root()->block_num() <= lib_num.
      // Also, even though blog.head() may still be nullptr, blog.first_block_num() is guaranteed to be lib_num + 1.

      auto finish_init = [&](auto& forkdb) {
         if( read_mode != db_read_mode::IRREVERSIBLE ) {
            auto pending_head = forkdb.pending_head();
            auto head         = forkdb.head();
            if ( head && pending_head && pending_head->id() != head->id() && head->id() == forkdb.root()->id() ) {
               wlog( "read_mode has changed from irreversible: applying best branch from fork database" );

               for( ; pending_head->id() != forkdb.head()->id(); pending_head = forkdb.pending_head() ) {
                  wlog( "applying branch from fork database ending with block: ${id}", ("id", pending_head->id()) );
                  controller::block_report br;
                  maybe_switch_forks( br, pending_head, controller::block_status::complete, {}, trx_meta_cache_lookup{} );
               }
            }
         }
      };

      fork_db.apply<void>(finish_init);

      // At Leap startup, we want to provide to our local finalizers the correct safety information
      // to use if they don't already have one.
      // If we start at a block prior to the IF transition, that information will be provided when
      // we create the new `fork_db_if`.
      // If we start at a block during or after the IF transition, we need to provide this information
      // at startup.
      // ---------------------------------------------------------------------------------------------
      if (auto in_use = fork_db.version_in_use(); in_use  == fork_database::in_use_t::both || in_use  == fork_database::in_use_t::savanna) {
         // we are already past the IF transition point where we create the updated fork_db.
         // so we can't rely on the finalizer safety information update happening during the transition.
         // see https://github.com/AntelopeIO/leap/issues/2070#issuecomment-1941901836
         // -------------------------------------------------------------------------------------------
         if (in_use  == fork_database::in_use_t::both) {
            // fork_db_legacy is present as well, which means that we have not completed the transition
            auto set_finalizer_defaults = [&](auto& forkdb) -> void {
               auto lib = forkdb.root();
               my_finalizers.set_default_safety_information(
                  finalizer_safety_information{ .last_vote_range_start = block_timestamp_type(0),
                                                .last_vote = {},
                                                .lock      = {lib->id(), lib->timestamp()} });
            };
            fork_db.apply_s<void>(set_finalizer_defaults);
         } else {
            // we are past the IF transition.
            auto set_finalizer_defaults = [&](auto& forkdb) -> void {
               auto lib = forkdb.root();
               my_finalizers.set_default_safety_information(
                  finalizer_safety_information{ .last_vote_range_start = block_timestamp_type(0),
                                                .last_vote = {},
                                                .lock      = {lib->id(), lib->timestamp()} });
            };
            fork_db.apply_s<void>(set_finalizer_defaults);
         }
      }
   }

   ~controller_impl() {
      pending.reset();
      //only log this not just if configured to, but also if initialization made it to the point we'd log the startup too
      if(okay_to_print_integrity_hash_on_stop && conf.integrity_hash_on_stop)
         ilog( "chain database stopped with hash: ${hash}", ("hash", calculate_integrity_hash()) );
   }

   void add_indices() {
      controller_index_set::add_indices(db);
      contract_database_index_set::add_indices(db);

      authorization.add_indices();
      resource_limits.add_indices();
   }

   void clear_all_undo() {
      // Rewind the database to the last irreversible block
      db.undo_all();
   }

   void add_contract_tables_to_snapshot( const snapshot_writer_ptr& snapshot ) const {
      snapshot->write_section("contract_tables", [this]( auto& section ) {
         index_utils<table_id_multi_index>::walk(db, [this, &section]( const table_id_object& table_row ){
            // add a row for the table
            section.add_row(table_row, db);

            // followed by a size row and then N data rows for each type of table
            contract_database_index_set::walk_indices([this, &section, &table_row]( auto utils ) {
               using utils_t = decltype(utils);
               using value_t = typename decltype(utils)::index_t::value_type;
               using by_table_id = object_to_table_id_tag_t<value_t>;

               auto tid_key = boost::make_tuple(table_row.id);
               auto next_tid_key = boost::make_tuple(table_id_object::id_type(table_row.id._id + 1));

               unsigned_int size = utils_t::template size_range<by_table_id>(db, tid_key, next_tid_key);
               section.add_row(size, db);

               utils_t::template walk_range<by_table_id>(db, tid_key, next_tid_key, [this, &section]( const auto &row ) {
                  section.add_row(row, db);
               });
            });
         });
      });
   }

   void read_contract_tables_from_snapshot( const snapshot_reader_ptr& snapshot ) {
      snapshot->read_section("contract_tables", [this]( auto& section ) {
         bool more = !section.empty();
         while (more) {
            // read the row for the table
            table_id_object::id_type t_id;
            index_utils<table_id_multi_index>::create(db, [this, &section, &t_id](auto& row) {
               section.read_row(row, db);
               t_id = row.id;
            });

            // read the size and data rows for each type of table
            contract_database_index_set::walk_indices([this, &section, &t_id, &more](auto utils) {
               using utils_t = decltype(utils);

               unsigned_int size;
               more = section.read_row(size, db);

               for (size_t idx = 0; idx < size.value; idx++) {
                  utils_t::create(db, [this, &section, &more, &t_id](auto& row) {
                     row.t_id = t_id;
                     more = section.read_row(row, db);
                  });
               }
            });
         }
      });
   }

   block_state_pair get_block_state_to_snapshot() const
   {
       return apply<block_state_pair>(
          chain_head,
          overloaded{[](const block_state_legacy_ptr& head) { return block_state_pair{ head, {} }; },
                     [](const block_state_ptr&        head) { return block_state_pair{ {}, head }; }});
   }

   void add_to_snapshot( const snapshot_writer_ptr& snapshot ) {
      // clear in case the previous call to clear did not finish in time of deadline
      clear_expired_input_transactions( fc::time_point::maximum() );

      snapshot->write_section<chain_snapshot_header>([this]( auto &section ){
         section.add_row(chain_snapshot_header(), db);
      });

      apply<void>(chain_head, [&](const auto& head) {
         snapshot_detail::snapshot_block_state_data_v7 block_state_data(get_block_state_to_snapshot());

         snapshot->write_section("eosio::chain::block_state", [&]( auto& section ) {
            section.add_row(block_state_data, db);
         });
      });
      
      controller_index_set::walk_indices([this, &snapshot]( auto utils ){
         using value_t = typename decltype(utils)::index_t::value_type;

         // skip the table_id_object as its inlined with contract tables section
         if (std::is_same<value_t, table_id_object>::value) {
            return;
         }

         // skip the database_header as it is only relevant to in-memory database
         if (std::is_same<value_t, database_header_object>::value) {
            return;
         }

         snapshot->write_section<value_t>([this]( auto& section ){
            decltype(utils)::walk(db, [this, &section]( const auto &row ) {
               section.add_row(row, db);
            });
         });
      });

      add_contract_tables_to_snapshot(snapshot);

      authorization.add_to_snapshot(snapshot);
      resource_limits.add_to_snapshot(snapshot);
   }

   static std::optional<genesis_state> extract_legacy_genesis_state( snapshot_reader& snapshot, uint32_t version ) {
      std::optional<genesis_state> genesis;
      using v2 = legacy::snapshot_global_property_object_v2;

      if (std::clamp(version, v2::minimum_version, v2::maximum_version) == version ) {
         genesis.emplace();
         snapshot.read_section<genesis_state>([&genesis=*genesis]( auto &section ){
            section.read_row(genesis);
         });
      }
      return genesis;
   }

   void read_from_snapshot( const snapshot_reader_ptr& snapshot, uint32_t blog_start, uint32_t blog_end ) {
      chain_snapshot_header header;
      snapshot->read_section<chain_snapshot_header>([this, &header]( auto &section ){
         section.read_row(header, db);
         header.validate();
      });

      auto read_block_state_section = [&](auto& forkdb) { /// load and upgrade the block header state
         using namespace snapshot_detail;
         using v7 = snapshot_block_state_data_v7;

         if (header.version >= v7::minimum_version) {
            // loading a snapshot saved by Leap 6.0 and above.
            // -----------------------------------------------
            if (std::clamp(header.version, v7::minimum_version, v7::maximum_version) == header.version ) {
               snapshot->read_section("eosio::chain::block_state", [this]( auto &section ){
                  v7 block_state_data;
                  section.read_row(block_state_data, db);
                  assert(block_state_data.bs_l || block_state_data.bs);
                  // todo: during the transition phase, both may be set. Restore appropriately!
                  if (block_state_data.bs_l)
                     chain_head = block_handle{std::make_shared<block_state_legacy>(std::move(*block_state_data.bs_l))};
                  else
                     chain_head = block_handle{std::make_shared<block_state>(std::move(*block_state_data.bs))};
               });
            } else {
               EOS_THROW(snapshot_exception, "Unsupported block_state version");
            }
         } else {
            // loading a snapshot saved by Leap up to version 5.
            // -------------------------------------------------
            auto head_header_state = std::make_shared<block_state_legacy>();
            using v2 = snapshot_block_header_state_legacy_v2;
            using v3 = snapshot_block_header_state_legacy_v3;

            if (std::clamp(header.version, v2::minimum_version, v2::maximum_version) == header.version ) {
               snapshot->read_section("eosio::chain::block_state", [this, &head_header_state]( auto &section ) {
                  v2 legacy_header_state;
                  section.read_row(legacy_header_state, db);
                  static_cast<block_header_state_legacy&>(*head_header_state) = block_header_state_legacy(std::move(legacy_header_state));
               });
            } else if (std::clamp(header.version, v3::minimum_version, v3::maximum_version) == header.version ) {
               snapshot->read_section("eosio::chain::block_state", [this,&head_header_state]( auto &section ){
                  v3 legacy_header_state;
                  section.read_row(legacy_header_state, db);
                  static_cast<block_header_state_legacy&>(*head_header_state) = block_header_state_legacy(std::move(legacy_header_state));
               });
            } else {
               EOS_THROW(snapshot_exception, "Unsupported block_header_state version");
            }
            chain_head = block_handle{head_header_state};
         }

         snapshot_head_block = chain_head.block_num();
         EOS_ASSERT( blog_start <= (snapshot_head_block + 1) && snapshot_head_block <= blog_end,
                     block_log_exception,
                     "Block log is provided with snapshot but does not contain the head block from the snapshot nor a block right after it",
                     ("snapshot_head_block", snapshot_head_block)
                     ("block_log_first_num", blog_start)
                     ("block_log_last_num", blog_end)
         );
      };
      fork_db.apply_l<void>(read_block_state_section);

      controller_index_set::walk_indices([this, &snapshot, &header]( auto utils ){
         using value_t = typename decltype(utils)::index_t::value_type;

         // skip the table_id_object as its inlined with contract tables section
         if (std::is_same_v<value_t, table_id_object>) {
            return;
         }

         // skip the database_header as it is only relevant to in-memory database
         if (std::is_same_v<value_t, database_header_object>) {
            return;
         }

         // special case for in-place upgrade of global_property_object
         if (std::is_same_v<value_t, global_property_object>) {
            using v2 = legacy::snapshot_global_property_object_v2;
            using v3 = legacy::snapshot_global_property_object_v3;
            using v4 = legacy::snapshot_global_property_object_v4;

            if (std::clamp(header.version, v2::minimum_version, v2::maximum_version) == header.version ) {
               std::optional<genesis_state> genesis = extract_legacy_genesis_state(*snapshot, header.version);
               EOS_ASSERT( genesis, snapshot_exception,
                           "Snapshot indicates chain_snapshot_header version 2, but does not contain a genesis_state. "
                           "It must be corrupted.");
               snapshot->read_section<global_property_object>([&db=this->db,gs_chain_id=genesis->compute_chain_id()]( auto &section ) {
                  v2 legacy_global_properties;
                  section.read_row(legacy_global_properties, db);

                  db.create<global_property_object>([&legacy_global_properties,&gs_chain_id](auto& gpo ){
                     gpo.initalize_from(legacy_global_properties, gs_chain_id, kv_database_config{},
                                       genesis_state::default_initial_wasm_configuration);
                  });
               });
               return; // early out to avoid default processing
            }

            if (std::clamp(header.version, v3::minimum_version, v3::maximum_version) == header.version ) {
               snapshot->read_section<global_property_object>([&db=this->db]( auto &section ) {
                  v3 legacy_global_properties;
                  section.read_row(legacy_global_properties, db);

                  db.create<global_property_object>([&legacy_global_properties](auto& gpo ){
                     gpo.initalize_from(legacy_global_properties, kv_database_config{},
                                        genesis_state::default_initial_wasm_configuration);
                  });
               });
               return; // early out to avoid default processing
            }

            if (std::clamp(header.version, v4::minimum_version, v4::maximum_version) == header.version) {
               snapshot->read_section<global_property_object>([&db = this->db](auto& section) {
                  v4 legacy_global_properties;
                  section.read_row(legacy_global_properties, db);

                  db.create<global_property_object>([&legacy_global_properties](auto& gpo) {
                     gpo.initalize_from(legacy_global_properties);
                  });
               });
               return; // early out to avoid default processing
            }
         }

         snapshot->read_section<value_t>([this]( auto& section ) {
            bool more = !section.empty();
            while(more) {
               decltype(utils)::create(db, [this, &section, &more]( auto &row ) {
                  more = section.read_row(row, db);
               });
            }
         });
      });

      read_contract_tables_from_snapshot(snapshot);

      authorization.read_from_snapshot(snapshot);
      resource_limits.read_from_snapshot(snapshot);

      db.set_revision( chain_head.block_num() );
      db.create<database_header_object>([](const auto& header){
         // nothing to do
      });

      const auto& gpo = db.get<global_property_object>();
      EOS_ASSERT( gpo.chain_id == chain_id, chain_id_type_exception,
                  "chain ID in snapshot (${snapshot_chain_id}) does not match the chain ID that controller was constructed with (${controller_chain_id})",
                  ("snapshot_chain_id", gpo.chain_id)("controller_chain_id", chain_id)
      );
   }

   digest_type get_strong_digest_by_id( const block_id_type& id ) const {
      return fork_db.apply<digest_type>(
         overloaded{
            [](const fork_database_legacy_t&) -> digest_type { return digest_type{}; },
            [&](const fork_database_if_t& forkdb) -> digest_type {
               auto bsp = forkdb.get_block(id);
               return bsp ? bsp->strong_digest : digest_type{};
            }
         }
      );
   }

   fc::sha256 calculate_integrity_hash() {
      fc::sha256::encoder enc;
      auto hash_writer = std::make_shared<integrity_hash_snapshot_writer>(enc);
      add_to_snapshot(hash_writer);
      hash_writer->finalize();

      return enc.result();
   }

   void create_native_account( const fc::time_point& initial_timestamp, account_name name, const authority& owner, const authority& active, bool is_privileged = false ) {
      db.create<account_object>([&](auto& a) {
         a.name = name;
         a.creation_date = initial_timestamp;

         if( name == config::system_account_name ) {
            // The initial eosio ABI value affects consensus; see  https://github.com/EOSIO/eos/issues/7794
            // TODO: This doesn't charge RAM; a fix requires a consensus upgrade.
            a.abi.assign(eosio_abi_bin, sizeof(eosio_abi_bin));
         }
      });
      db.create<account_metadata_object>([&](auto & a) {
         a.name = name;
         a.set_privileged( is_privileged );
      });

      const auto& owner_permission  = authorization.create_permission(name, config::owner_name, 0,
                                                                      owner, false, initial_timestamp );
      const auto& active_permission = authorization.create_permission(name, config::active_name, owner_permission.id,
                                                                      active, false, initial_timestamp );

      resource_limits.initialize_account(name, false);

      int64_t ram_delta = config::overhead_per_account_ram_bytes;
      ram_delta += 2*config::billable_size_v<permission_object>;
      ram_delta += owner_permission.auth.get_billable_size();
      ram_delta += active_permission.auth.get_billable_size();

      // This is only called at startup, no transaction specific logging is possible
      if (auto dm_logger = get_deep_mind_logger(false)) {
         dm_logger->on_ram_trace(RAM_EVENT_ID("${name}", ("name", name)), "account", "add", "newaccount");
      }

      resource_limits.add_pending_ram_usage(name, ram_delta, false); // false for doing dm logging
      resource_limits.verify_account_ram_usage(name);
   }

   void initialize_database(const genesis_state& genesis) {
      // create the database header sigil
      db.create<database_header_object>([&]( auto& header ){
         // nothing to do for now
      });

      // Initialize block summary index
      for (int i = 0; i < 0x10000; i++)
         db.create<block_summary_object>([&](block_summary_object&) {});

      const auto& tapos_block_summary = db.get<block_summary_object>(1);
      db.modify( tapos_block_summary, [&]( auto& bs ) {
         bs.block_id = chain_head.id();
      });

      genesis.initial_configuration.validate();
      db.create<global_property_object>([&genesis,&chain_id=this->chain_id](auto& gpo ){
         gpo.configuration = genesis.initial_configuration;
         // TODO: Update this when genesis protocol features are enabled.
         gpo.wasm_configuration = genesis_state::default_initial_wasm_configuration;
         gpo.chain_id = chain_id;
      });

      db.create<protocol_state_object>([&](auto& pso ){
         pso.num_supported_key_types = config::genesis_num_supported_key_types;
         for( const auto& i : genesis_intrinsics ) {
            add_intrinsic_to_whitelist( pso.whitelisted_intrinsics, i );
         }
      });

      db.create<dynamic_global_property_object>([](auto&){});

      authorization.initialize_database();
      resource_limits.initialize_database();

      authority system_auth(genesis.initial_key);
      create_native_account( genesis.initial_timestamp, config::system_account_name, system_auth, system_auth, true );

      auto empty_authority = authority(1, {}, {});
      auto active_producers_authority = authority(1, {}, {});
      active_producers_authority.accounts.push_back({{config::system_account_name, config::active_name}, 1});

      create_native_account( genesis.initial_timestamp, config::null_account_name, empty_authority, empty_authority );
      create_native_account( genesis.initial_timestamp, config::producers_account_name, empty_authority, active_producers_authority );
      const auto& active_permission       = authorization.get_permission({config::producers_account_name, config::active_name});
      const auto& majority_permission     = authorization.create_permission( config::producers_account_name,
                                                                             config::majority_producers_permission_name,
                                                                             active_permission.id,
                                                                             active_producers_authority,
                                                                             false,
                                                                             genesis.initial_timestamp );
                                            authorization.create_permission( config::producers_account_name,
                                                                             config::minority_producers_permission_name,
                                                                             majority_permission.id,
                                                                             active_producers_authority,
                                                                             false,
                                                                             genesis.initial_timestamp );

   }

   // The returned scoped_exit should not exceed the lifetime of the pending which existed when make_block_restore_point was called.
   fc::scoped_exit<std::function<void()>> make_block_restore_point( bool is_read_only = false ) {
      if ( is_read_only ) {
         std::function<void()> callback = []() { };
         return fc::make_scoped_exit( std::move(callback) );
      }

      auto& bb = std::get<building_block>(pending->_block_stage);
      return fc::make_scoped_exit(bb.make_block_restore_point());
   }

   transaction_trace_ptr apply_onerror( const generated_transaction& gtrx,
                                        fc::time_point block_deadline,
                                        fc::microseconds max_transaction_time,
                                        fc::time_point start,
                                        uint32_t& cpu_time_to_bill_us, // only set on failure
                                        uint32_t billed_cpu_time_us,
                                        bool explicit_billed_cpu_time = false,
                                        bool enforce_whiteblacklist = true
                                      )
   {
      signed_transaction etrx;
      // Deliver onerror action containing the failed deferred transaction directly back to the sender.
      etrx.actions.emplace_back( vector<permission_level>{{gtrx.sender, config::active_name}},
                                 onerror( gtrx.sender_id, gtrx.packed_trx.data(), gtrx.packed_trx.size() ) );
      if( is_builtin_activated( builtin_protocol_feature_t::no_duplicate_deferred_id ) ) {
         etrx.expiration = time_point_sec();
         etrx.ref_block_num = 0;
         etrx.ref_block_prefix = 0;
      } else {
         etrx.expiration = time_point_sec{pending_block_time() + fc::microseconds(999'999)}; // Round up to nearest second to avoid appearing expired
         etrx.set_reference_block( chain_head.id() );
      }

      auto& bb = std::get<building_block>(pending->_block_stage);

      transaction_checktime_timer trx_timer(timer);
      const packed_transaction trx( std::move( etrx ) );
      transaction_context trx_context( self, trx, trx.id(), std::move(trx_timer), bb.action_receipt_digests().store_which(), start );

      if (auto dm_logger = get_deep_mind_logger(trx_context.is_transient())) {
         dm_logger->on_onerror(etrx);
      }

      trx_context.block_deadline = block_deadline;
      trx_context.max_transaction_time_subjective = max_transaction_time;
      trx_context.explicit_billed_cpu_time = explicit_billed_cpu_time;
      trx_context.billed_cpu_time_us = billed_cpu_time_us;
      trx_context.enforce_whiteblacklist = enforce_whiteblacklist;

      transaction_trace_ptr trace = trx_context.trace;

      auto handle_exception = [&](const auto& e)
      {
         cpu_time_to_bill_us = trx_context.update_billed_cpu_time( fc::time_point::now() );
         trace->error_code = controller::convert_exception_to_error_code( e );
         trace->except = e;
         trace->except_ptr = std::current_exception();
      };

      try {
         trx_context.init_for_implicit_trx();
         trx_context.published = gtrx.published;
         trx_context.execute_action( trx_context.schedule_action( trx.get_transaction().actions.back(), gtrx.sender, false, 0, 0 ), 0 );
         trx_context.finalize(); // Automatically rounds up network and CPU usage in trace and bills payers if successful

         auto restore = make_block_restore_point();
         trace->receipt = push_receipt( gtrx.trx_id, transaction_receipt::soft_fail,
                                        trx_context.billed_cpu_time_us, trace->net_usage );

         bb.action_receipt_digests().append(std::move(trx_context.executed_action_receipts));

         trx_context.squash();
         restore.cancel();
         return trace;
      } catch( const disallowed_transaction_extensions_bad_block_exception& ) {
         throw;
      } catch( const protocol_feature_bad_block_exception& ) {
         throw;
      } catch ( const std::bad_alloc& ) {
         throw;
      } catch ( const boost::interprocess::bad_alloc& ) {
         throw;
      } catch( const fc::exception& e ) {
         handle_exception(e);
      } catch ( const std::exception& e ) {
         auto wrapper = fc::std_exception_wrapper::from_current_exception(e);
         handle_exception(wrapper);
      }
      return trace;
   }

   int64_t remove_scheduled_transaction( const generated_transaction_object& gto ) {
      // deferred transactions cannot be transient.
      if (auto dm_logger = get_deep_mind_logger(false)) {
         dm_logger->on_ram_trace(RAM_EVENT_ID("${id}", ("id", gto.id)), "deferred_trx", "remove", "deferred_trx_removed");
      }

      int64_t ram_delta = -(config::billable_size_v<generated_transaction_object> + gto.packed_trx.size());
      resource_limits.add_pending_ram_usage( gto.payer, ram_delta, false ); // false for doing dm logging
      // No need to verify_account_ram_usage since we are only reducing memory

      db.remove( gto );
      return ram_delta;
   }

   bool failure_is_subjective( const fc::exception& e ) const {
      auto code = e.code();
      return    (code == subjective_block_production_exception::code_value)
             || (code == block_net_usage_exceeded::code_value)
             || (code == greylist_net_usage_exceeded::code_value)
             || (code == block_cpu_usage_exceeded::code_value)
             || (code == greylist_cpu_usage_exceeded::code_value)
             || (code == deadline_exception::code_value)
             || (code == leeway_deadline_exception::code_value)
             || (code == actor_whitelist_exception::code_value)
             || (code == actor_blacklist_exception::code_value)
             || (code == contract_whitelist_exception::code_value)
             || (code == contract_blacklist_exception::code_value)
             || (code == action_blacklist_exception::code_value)
             || (code == key_blacklist_exception::code_value)
             || (code == sig_variable_size_limit_exception::code_value);
   }

   bool scheduled_failure_is_subjective( const fc::exception& e ) const {
      auto code = e.code();
      return    (code == tx_cpu_usage_exceeded::code_value)
             || failure_is_subjective(e);
   }

   transaction_trace_ptr push_scheduled_transaction( const transaction_id_type& trxid,
                                                     fc::time_point block_deadline, fc::microseconds max_transaction_time,
                                                     uint32_t billed_cpu_time_us, bool explicit_billed_cpu_time = false )
   {
      const auto& idx = db.get_index<generated_transaction_multi_index,by_trx_id>();
      auto itr = idx.find( trxid );
      EOS_ASSERT( itr != idx.end(), unknown_transaction_exception, "unknown transaction" );
      return push_scheduled_transaction( *itr, block_deadline, max_transaction_time, billed_cpu_time_us, explicit_billed_cpu_time );
   }

   transaction_trace_ptr push_scheduled_transaction( const generated_transaction_object& gto,
                                                     fc::time_point block_deadline, fc::microseconds max_transaction_time,
                                                     uint32_t billed_cpu_time_us, bool explicit_billed_cpu_time = false )
   { try {

      auto start = fc::time_point::now();
      const bool validating = !is_speculative_block();
      EOS_ASSERT( !validating || explicit_billed_cpu_time, transaction_exception, "validating requires explicit billing" );

      maybe_session undo_session;
      if ( !skip_db_sessions() )
         undo_session = maybe_session(db);

      auto gtrx = generated_transaction(gto);

      // remove the generated transaction object after making a copy
      // this will ensure that anything which affects the GTO multi-index-container will not invalidate
      // data we need to successfully retire this transaction.
      //
      // IF the transaction FAILs in a subjective way, `undo_session` should expire without being squashed
      // resulting in the GTO being restored and available for a future block to retire.
      int64_t trx_removal_ram_delta = remove_scheduled_transaction(gto);

      fc::datastream<const char*> ds( gtrx.packed_trx.data(), gtrx.packed_trx.size() );

      // check delay_until only before disable_deferred_trxs_stage_1 is activated.
      if( !is_builtin_activated( builtin_protocol_feature_t::disable_deferred_trxs_stage_1 ) ) {
         EOS_ASSERT( gtrx.delay_until <= pending_block_time(), transaction_exception, "this transaction isn't ready",
                    ("gtrx.delay_until",gtrx.delay_until)("pbt",pending_block_time()) );
      }

      signed_transaction dtrx;
      fc::raw::unpack(ds,static_cast<transaction&>(dtrx) );
      transaction_metadata_ptr trx =
            transaction_metadata::create_no_recover_keys( std::make_shared<packed_transaction>( std::move(dtrx)  ),
                                                          transaction_metadata::trx_type::scheduled );
      trx->accepted = true;

      // After disable_deferred_trxs_stage_1 is activated, a deferred transaction
      // can only be retired as expired, and it can be retired as expired
      // regardless of whether its delay_util or expiration times have been reached.
      transaction_trace_ptr trace;
      if( is_builtin_activated( builtin_protocol_feature_t::disable_deferred_trxs_stage_1 ) || gtrx.expiration < pending_block_time() ) {
         trace = std::make_shared<transaction_trace>();
         trace->id = gtrx.trx_id;
         trace->block_num = chain_head.block_num() + 1;
         trace->block_time = pending_block_time();
         trace->producer_block_id = pending_producer_block_id();
         trace->scheduled = true;
         trace->receipt = push_receipt( gtrx.trx_id, transaction_receipt::expired, billed_cpu_time_us, 0 ); // expire the transaction
         trace->account_ram_delta = account_delta( gtrx.payer, trx_removal_ram_delta );
         trace->elapsed = fc::time_point::now() - start;
         pending->_block_report.total_cpu_usage_us += billed_cpu_time_us;
         pending->_block_report.total_elapsed_time += trace->elapsed;
         pending->_block_report.total_time += trace->elapsed;
         dmlog_applied_transaction(trace);
         emit( applied_transaction, std::tie(trace, trx->packed_trx()) );
         undo_session.squash();
         return trace;
      }

      auto reset_in_trx_requiring_checks = fc::make_scoped_exit([old_value=in_trx_requiring_checks,this](){
         in_trx_requiring_checks = old_value;
      });
      in_trx_requiring_checks = true;

      uint32_t cpu_time_to_bill_us = billed_cpu_time_us;
      auto& bb = std::get<building_block>(pending->_block_stage);

      transaction_checktime_timer trx_timer( timer );
      transaction_context trx_context( self, *trx->packed_trx(), gtrx.trx_id, std::move(trx_timer), bb.action_receipt_digests().store_which() );
      trx_context.leeway =  fc::microseconds(0); // avoid stealing cpu resource
      trx_context.block_deadline = block_deadline;
      trx_context.max_transaction_time_subjective = max_transaction_time;
      trx_context.explicit_billed_cpu_time = explicit_billed_cpu_time;
      trx_context.billed_cpu_time_us = billed_cpu_time_us;
      trx_context.enforce_whiteblacklist = gtrx.sender.empty() ? true : !sender_avoids_whitelist_blacklist_enforcement( gtrx.sender );
      trace = trx_context.trace;

      auto handle_exception = [&](const auto& e)
      {
         cpu_time_to_bill_us = trx_context.update_billed_cpu_time( fc::time_point::now() );
         trace->error_code = controller::convert_exception_to_error_code( e );
         trace->except = e;
         trace->except_ptr = std::current_exception();
         trace->elapsed = fc::time_point::now() - start;

         // deferred transactions cannot be transient
         if (auto dm_logger = get_deep_mind_logger(false)) {
            dm_logger->on_fail_deferred();
         }
      };

      try {
         trx_context.init_for_deferred_trx( gtrx.published );

         if( trx_context.enforce_whiteblacklist && is_speculative_block() ) {
            flat_set<account_name> actors;
            for( const auto& act : trx->packed_trx()->get_transaction().actions ) {
               for( const auto& auth : act.authorization ) {
                  actors.insert( auth.actor );
               }
            }
            check_actor_list( actors );
         }

         trx_context.exec();
         trx_context.finalize(); // Automatically rounds up network and CPU usage in trace and bills payers if successful

         auto restore = make_block_restore_point();

         trace->receipt = push_receipt( gtrx.trx_id,
                                        transaction_receipt::executed,
                                        trx_context.billed_cpu_time_us,
                                        trace->net_usage );

         bb.action_receipt_digests().append(std::move(trx_context.executed_action_receipts));

         trace->account_ram_delta = account_delta( gtrx.payer, trx_removal_ram_delta );

         dmlog_applied_transaction(trace);
         emit( applied_transaction, std::tie(trace, trx->packed_trx()) );

         trx_context.squash();
         undo_session.squash();

         restore.cancel();

         pending->_block_report.total_net_usage += trace->net_usage;
         pending->_block_report.total_cpu_usage_us += trace->receipt->cpu_usage_us;
         pending->_block_report.total_elapsed_time += trace->elapsed;
         pending->_block_report.total_time += fc::time_point::now() - start;

         return trace;
      } catch( const disallowed_transaction_extensions_bad_block_exception& ) {
         throw;
      } catch( const protocol_feature_bad_block_exception& ) {
         throw;
      } catch ( const std::bad_alloc& ) {
         throw;
      } catch ( const boost::interprocess::bad_alloc& ) {
         throw;
      } catch( const fc::exception& e ) {
        handle_exception(e);
      } catch ( const std::exception& e) {
        auto wrapper = fc::std_exception_wrapper::from_current_exception(e);
        handle_exception(wrapper);
      }

      trx_context.undo();

      // Only subjective OR soft OR hard failure logic below:

      if( gtrx.sender != account_name() && !(validating ? failure_is_subjective(*trace->except) : scheduled_failure_is_subjective(*trace->except))) {
         // Attempt error handling for the generated transaction.

         auto error_trace = apply_onerror( gtrx, block_deadline, max_transaction_time, trx_context.pseudo_start,
                                           cpu_time_to_bill_us, billed_cpu_time_us, explicit_billed_cpu_time,
                                           trx_context.enforce_whiteblacklist );
         error_trace->failed_dtrx_trace = trace;
         trace = error_trace;
         if( !trace->except_ptr ) {
            trace->account_ram_delta = account_delta( gtrx.payer, trx_removal_ram_delta );
            trace->elapsed = fc::time_point::now() - start;
            dmlog_applied_transaction(trace);
            emit( applied_transaction, std::tie(trace, trx->packed_trx()) );
            undo_session.squash();
            pending->_block_report.total_net_usage += trace->net_usage;
            if( trace->receipt ) pending->_block_report.total_cpu_usage_us += trace->receipt->cpu_usage_us;
            pending->_block_report.total_elapsed_time += trace->elapsed;
            pending->_block_report.total_time += trace->elapsed;
            return trace;
         }
         trace->elapsed = fc::time_point::now() - start;
      }

      // Only subjective OR hard failure logic below:

      // subjectivity changes based on producing vs validating
      bool subjective  = false;
      if (validating) {
         subjective = failure_is_subjective(*trace->except);
      } else {
         subjective = scheduled_failure_is_subjective(*trace->except);
      }

      if ( !subjective ) {
         // hard failure logic

         if( !validating ) {
            resource_limits.update_account_usage( trx_context.bill_to_accounts, block_timestamp_type(pending_block_time()).slot );
            int64_t account_cpu_limit = 0;
            std::tie( std::ignore, account_cpu_limit, std::ignore, std::ignore ) = trx_context.max_bandwidth_billed_accounts_can_pay( true );

            uint32_t limited_cpu_time_to_bill_us = static_cast<uint32_t>( std::min(
                  std::min( static_cast<int64_t>(cpu_time_to_bill_us), account_cpu_limit ),
                  trx_context.initial_objective_duration_limit.count() ) );
            EOS_ASSERT( !explicit_billed_cpu_time || (cpu_time_to_bill_us == limited_cpu_time_to_bill_us),
                        transaction_exception, "cpu to bill ${cpu} != limited ${limit}", ("cpu", cpu_time_to_bill_us)("limit", limited_cpu_time_to_bill_us) );
            cpu_time_to_bill_us = limited_cpu_time_to_bill_us;
         }

         resource_limits.add_transaction_usage( trx_context.bill_to_accounts, cpu_time_to_bill_us, 0,
                                                block_timestamp_type(pending_block_time()).slot ); // Should never fail

         trace->receipt = push_receipt(gtrx.trx_id, transaction_receipt::hard_fail, cpu_time_to_bill_us, 0);
         trace->account_ram_delta = account_delta( gtrx.payer, trx_removal_ram_delta );

         dmlog_applied_transaction(trace);
         emit( applied_transaction, std::tie(trace, trx->packed_trx()) );

         undo_session.squash();
      } else {
         dmlog_applied_transaction(trace);
         emit( applied_transaction, std::tie(trace, trx->packed_trx()) );
      }

      pending->_block_report.total_net_usage += trace->net_usage;
      if( trace->receipt ) pending->_block_report.total_cpu_usage_us += trace->receipt->cpu_usage_us;
      pending->_block_report.total_elapsed_time += trace->elapsed;
      pending->_block_report.total_time += fc::time_point::now() - start;

      return trace;
   } FC_CAPTURE_AND_RETHROW() } /// push_scheduled_transaction


   /**
    *  Adds the transaction receipt to the pending block and returns it.
    */
   template<typename T>
   const transaction_receipt& push_receipt( const T& trx, transaction_receipt_header::status_enum status,
                                            uint64_t cpu_usage_us, uint64_t net_usage ) {
      uint64_t net_usage_words = net_usage / 8;
      EOS_ASSERT( net_usage_words*8 == net_usage, transaction_exception, "net_usage is not divisible by 8" );
      auto& bb = std::get<building_block>(pending->_block_stage);
      auto& receipts = bb.pending_trx_receipts();
      receipts.emplace_back( trx );
      transaction_receipt& r = receipts.back();
      r.cpu_usage_us         = cpu_usage_us;
      r.net_usage_words      = net_usage_words;
      r.status               = status;
      auto& mroot_or_digests = bb.trx_mroot_or_receipt_digests();
      if( std::holds_alternative<digests_t>(mroot_or_digests) )
         std::get<digests_t>(mroot_or_digests).emplace_back( r.digest() );
      return r;
   }

   /**
    *  This is the entry point for new transactions to the block state. It will check authorization and
    *  determine whether to execute it now or to delay it. Lastly it inserts a transaction receipt into
    *  the pending block.
    */
   transaction_trace_ptr push_transaction( const transaction_metadata_ptr& trx,
                                           fc::time_point block_deadline,
                                           fc::microseconds max_transaction_time,
                                           uint32_t billed_cpu_time_us,
                                           bool explicit_billed_cpu_time,
                                           int64_t subjective_cpu_bill_us )
   {
      EOS_ASSERT(block_deadline != fc::time_point(), transaction_exception, "deadline cannot be uninitialized");

      transaction_trace_ptr trace;
      try {
         auto start = fc::time_point::now();
         const bool check_auth = !skip_auth_check() && !trx->implicit() && !trx->is_read_only();
         const fc::microseconds sig_cpu_usage = trx->signature_cpu_usage();

         if( !explicit_billed_cpu_time ) {
            fc::microseconds already_consumed_time( EOS_PERCENT(sig_cpu_usage.count(), conf.sig_cpu_bill_pct) );

            if( start.time_since_epoch() <  already_consumed_time ) {
               start = fc::time_point();
            } else {
               start -= already_consumed_time;
            }
         }

         auto& bb = std::get<building_block>(pending->_block_stage);

         const signed_transaction& trn = trx->packed_trx()->get_signed_transaction();
         transaction_checktime_timer trx_timer(timer);
         transaction_context trx_context(self, *trx->packed_trx(), trx->id(), std::move(trx_timer),
                                         bb.action_receipt_digests().store_which(), start, trx->get_trx_type());
         if ((bool)subjective_cpu_leeway && is_speculative_block()) {
            trx_context.leeway = *subjective_cpu_leeway;
         }
         trx_context.block_deadline = block_deadline;
         trx_context.max_transaction_time_subjective = max_transaction_time;
         trx_context.explicit_billed_cpu_time = explicit_billed_cpu_time;
         trx_context.billed_cpu_time_us = billed_cpu_time_us;
         trx_context.subjective_cpu_bill_us = subjective_cpu_bill_us;
         trace = trx_context.trace;

         auto handle_exception =[&](const auto& e)
         {
            trace->error_code = controller::convert_exception_to_error_code( e );
            trace->except = e;
            trace->except_ptr = std::current_exception();
            trace->elapsed = fc::time_point::now() - trx_context.start;
         };

         try {
            if( trx->implicit() ) {
               trx_context.init_for_implicit_trx();
               trx_context.enforce_whiteblacklist = false;
            } else {
               trx_context.init_for_input_trx( trx->packed_trx()->get_unprunable_size(),
                                               trx->packed_trx()->get_prunable_size() );
            }

            trx_context.delay = fc::seconds(trn.delay_sec);

            if( check_auth ) {
               authorization.check_authorization(
                       trn.actions,
                       trx->recovered_keys(),
                       {},
                       trx_context.delay,
                       [&trx_context](){ trx_context.checktime(); },
                       false,
                       trx->is_dry_run()
               );
            }
            trx_context.exec();
            trx_context.finalize(); // Automatically rounds up network and CPU usage in trace and bills payers if successful

            auto restore = make_block_restore_point( trx->is_read_only() );

            trx->billed_cpu_time_us = trx_context.billed_cpu_time_us;
            if (!trx->implicit() && !trx->is_read_only()) {
               transaction_receipt::status_enum s = (trx_context.delay == fc::seconds(0))
                                                    ? transaction_receipt::executed
                                                    : transaction_receipt::delayed;
               trace->receipt = push_receipt(*trx->packed_trx(), s, trx_context.billed_cpu_time_us, trace->net_usage);
               bb.pending_trx_metas().emplace_back(trx);
            } else {
               transaction_receipt_header r;
               r.status = transaction_receipt::executed;
               r.cpu_usage_us = trx_context.billed_cpu_time_us;
               r.net_usage_words = trace->net_usage / 8;
               trace->receipt = r;
            }

            if ( !trx->is_read_only() ) {
               bb.action_receipt_digests().append(std::move(trx_context.executed_action_receipts));

               if ( !trx->is_dry_run() ) {
                  // call the accept signal but only once for this transaction
                  if (!trx->accepted) {
                     trx->accepted = true;
                  }

                  dmlog_applied_transaction(trace, &trn);
                  emit(applied_transaction, std::tie(trace, trx->packed_trx()));
               }
            }

            if ( trx->is_transient() ) {
               // remove trx from pending block by not canceling 'restore'
               trx_context.undo(); // this will happen automatically in destructor, but make it more explicit
            } else if ( read_mode != db_read_mode::SPECULATIVE && pending->_block_status == controller::block_status::ephemeral ) {
               // An ephemeral block will never become a full block, but on a producer node the trxs should be saved
               // in the un-applied transaction queue for execution during block production. For a non-producer node
               // save the trxs in the un-applied transaction queue for use during block validation to skip signature
               // recovery.
               restore.cancel();   // maintain trx metas for abort block
               trx_context.undo(); // this will happen automatically in destructor, but make it more explicit
            } else {
               restore.cancel();
               trx_context.squash();
            }

            if( !trx->is_transient() ) {
               pending->_block_report.total_net_usage += trace->net_usage;
               pending->_block_report.total_cpu_usage_us += trace->receipt->cpu_usage_us;
               pending->_block_report.total_elapsed_time += trace->elapsed;
               pending->_block_report.total_time += fc::time_point::now() - start;
            }

            return trace;
         } catch( const disallowed_transaction_extensions_bad_block_exception& ) {
            throw;
         } catch( const protocol_feature_bad_block_exception& ) {
            throw;
         } catch ( const std::bad_alloc& ) {
           throw;
         } catch ( const boost::interprocess::bad_alloc& ) {
           throw;
         } catch (const fc::exception& e) {
           handle_exception(e);
         } catch (const std::exception& e) {
           auto wrapper = fc::std_exception_wrapper::from_current_exception(e);
           handle_exception(wrapper);
         }

         if (!trx->is_transient()) {
            dmlog_applied_transaction(trace);
            emit(applied_transaction, std::tie(trace, trx->packed_trx()));

            pending->_block_report.total_net_usage += trace->net_usage;
            if( trace->receipt ) pending->_block_report.total_cpu_usage_us += trace->receipt->cpu_usage_us;
            pending->_block_report.total_elapsed_time += trace->elapsed;
            pending->_block_report.total_time += fc::time_point::now() - start;
         }

         return trace;
      } FC_CAPTURE_AND_RETHROW((trace))
   } /// push_transaction

   void start_block( block_timestamp_type when,
                     uint16_t confirm_block_count,
                     const vector<digest_type>& new_protocol_feature_activations,
                     controller::block_status s,
                     const std::optional<block_id_type>& producer_block_id,
                     const fc::time_point& deadline )
   {
      EOS_ASSERT( !pending, block_validate_exception, "pending block already exists" );

      emit( block_start, chain_head.block_num() + 1 );

      // at block level, no transaction specific logging is possible
      if (auto dm_logger = get_deep_mind_logger(false)) {
         // The head block represents the block just before this one that is about to start, so add 1 to get this block num
         dm_logger->on_start_block(chain_head.block_num() + 1);
      }

      auto guard_pending = fc::make_scoped_exit([this, head_block_num=chain_head.block_num()]() {
         protocol_features.popped_blocks_to( head_block_num );
         pending.reset();
      });

      EOS_ASSERT( skip_db_sessions(s) || db.revision() == chain_head.block_num(), database_exception,
                  "db revision is not on par with head block",
                  ("db.revision()", db.revision())("controller_head_block", chain_head.block_num())("fork_db_head_block", fork_db_head_block_num()) );

      apply<void>(chain_head, overloaded{
                    [&](const block_state_legacy_ptr& head) {
                       maybe_session session = skip_db_sessions(s) ? maybe_session() : maybe_session(db);
                       pending.emplace(std::move(session), *head, when, confirm_block_count, new_protocol_feature_activations);
                    },
                    [&](const block_state_ptr& head) {
                       maybe_session        session = skip_db_sessions(s) ? maybe_session() : maybe_session(db);
                       building_block_input bbi{head->id(), head->timestamp(), when, head->get_scheduled_producer(when).producer_name,
                                                new_protocol_feature_activations};
                       pending.emplace(std::move(session), *head, bbi);
                    }
                 });

      pending->_block_status = s;
      pending->_producer_block_id = producer_block_id;

      auto& bb = std::get<building_block>(pending->_block_stage);

      // block status is either ephemeral or incomplete. Modify state of speculative block only if we are building a
      // speculative incomplete block (otherwise we need clean state for head mode, ephemeral block)
      if ( pending->_block_status != controller::block_status::ephemeral ) {
         const auto& pso = db.get<protocol_state_object>();

         auto num_preactivated_protocol_features = pso.preactivated_protocol_features.size();
         bool handled_all_preactivated_features = (num_preactivated_protocol_features == 0);

         if( new_protocol_feature_activations.size() > 0 ) {
            flat_map<digest_type, bool> activated_protocol_features;
            activated_protocol_features.reserve( std::max( num_preactivated_protocol_features,
                                                           new_protocol_feature_activations.size() ) );
            for( const auto& feature_digest : pso.preactivated_protocol_features ) {
               activated_protocol_features.emplace( feature_digest, false );
            }

            size_t num_preactivated_features_that_have_activated = 0;

            const auto& pfs = protocol_features.get_protocol_feature_set();
            for( const auto& feature_digest : new_protocol_feature_activations ) {
               const auto& f = pfs.get_protocol_feature( feature_digest );

               auto res = activated_protocol_features.emplace( feature_digest, true );
               if( res.second ) {
                  // feature_digest was not preactivated
                  EOS_ASSERT( !f.preactivation_required, protocol_feature_exception,
                              "attempted to activate protocol feature without prior required preactivation: ${digest}",
                              ("digest", feature_digest)
                  );
               } else {
                  EOS_ASSERT( !res.first->second, block_validate_exception,
                              "attempted duplicate activation within a single block: ${digest}",
                              ("digest", feature_digest)
                  );
                  // feature_digest was preactivated
                  res.first->second = true;
                  ++num_preactivated_features_that_have_activated;
               }

               if( f.builtin_feature ) {
                  trigger_activation_handler( *f.builtin_feature );
               }

               protocol_features.activate_feature( feature_digest, bb.block_num() );

               ++bb.num_new_protocol_features_activated();
            }

            if( num_preactivated_features_that_have_activated == num_preactivated_protocol_features ) {
               handled_all_preactivated_features = true;
            }
         }

         EOS_ASSERT( handled_all_preactivated_features, block_validate_exception,
                     "There are pre-activated protocol features that were not activated at the start of this block"
         );

         if( new_protocol_feature_activations.size() > 0 ) {
            db.modify( pso, [&]( auto& ps ) {
               ps.preactivated_protocol_features.clear();

               for (const auto& digest : new_protocol_feature_activations)
                  ps.activated_protocol_features.emplace_back(digest, bb.block_num());
            });
         }

         const auto& gpo = db.get<global_property_object>();

         // instant finality uses alternative method for chaning producer schedule
         bb.apply_l<void>([&](building_block::building_block_legacy& bb_legacy) {
            pending_block_header_state_legacy& pbhs = bb_legacy.pending_block_header_state;

            if( gpo.proposed_schedule_block_num && // if there is a proposed schedule that was proposed in a block ...
                ( *gpo.proposed_schedule_block_num <= pbhs.dpos_irreversible_blocknum ) && // ... that has now become irreversible ...
                pbhs.prev_pending_schedule.schedule.producers.size() == 0 // ... and there was room for a new pending schedule prior to any possible promotion
               )
            {
               // Promote proposed schedule to pending schedule; happens in next block after hotstuff activated
               EOS_ASSERT( gpo.proposed_schedule.version == pbhs.active_schedule_version + 1,
                           producer_schedule_exception, "wrong producer schedule version specified" );

               bb_legacy.new_pending_producer_schedule = producer_authority_schedule::from_shared(gpo.proposed_schedule);

               if( !replaying ) {
                  ilog( "promoting proposed schedule (set in block ${proposed_num}) to pending; current block: ${n} lib: ${lib} schedule: ${schedule} ",
                        ("proposed_num", *gpo.proposed_schedule_block_num)("n", pbhs.block_num)
                        ("lib", pbhs.dpos_irreversible_blocknum)
                        ("schedule", bb_legacy.new_pending_producer_schedule ) );
               }

               db.modify( gpo, [&]( auto& gp ) {
                  gp.proposed_schedule_block_num = std::optional<block_num_type>();
                  gp.proposed_schedule.version=0;
                  gp.proposed_schedule.producers.clear();
               });
            }
         });

         try {
            transaction_metadata_ptr onbtrx =
                  transaction_metadata::create_no_recover_keys( std::make_shared<packed_transaction>( get_on_block_transaction() ),
                                                                transaction_metadata::trx_type::implicit );
            auto reset_in_trx_requiring_checks = fc::make_scoped_exit([old_value=in_trx_requiring_checks,this](){
                  in_trx_requiring_checks = old_value;
               });
            in_trx_requiring_checks = true;
            auto trace = push_transaction( onbtrx, fc::time_point::maximum(), fc::microseconds::maximum(),
                                           gpo.configuration.min_transaction_cpu_usage, true, 0 );
            if( trace->except ) {
               wlog("onblock ${block_num} is REJECTING: ${entire_trace}",("block_num", chain_head.block_num() + 1)("entire_trace", trace));
            }
         } catch( const std::bad_alloc& e ) {
            elog( "on block transaction failed due to a std::bad_alloc" );
            throw;
         } catch( const boost::interprocess::bad_alloc& e ) {
            elog( "on block transaction failed due to a bad allocation" );
            throw;
         } catch( const fc::exception& e ) {
            wlog( "on block transaction failed, but shouldn't impact block generation, system contract needs update" );
            edump((e.to_detail_string()));
         } catch( const std::exception& e ) {
            wlog( "on block transaction failed due to unexpected exception" );
            edump((e.what()));
         } catch( ... ) {
            elog( "on block transaction failed due to unknown exception" );
         }

         clear_expired_input_transactions(deadline);
         update_producers_authority();
      }

      guard_pending.cancel();
   } /// start_block

   void assemble_block(bool validating, std::optional<qc_data_t> validating_qc_data, const block_state_ptr& validating_bsp)
   {
      EOS_ASSERT( pending, block_validate_exception, "it is not valid to finalize when there is no pending block");
      EOS_ASSERT( std::holds_alternative<building_block>(pending->_block_stage), block_validate_exception, "already called finish_block");

      try {
         auto& bb = std::get<building_block>(pending->_block_stage);

         // Update resource limits:
         resource_limits.process_account_limit_updates();
         const auto& chain_config = db.get<global_property_object>().configuration;
         resource_limits.set_block_parameters(
            { EOS_PERCENT(chain_config.max_block_cpu_usage, chain_config.target_block_cpu_usage_pct),
              chain_config.max_block_cpu_usage,
              config::block_cpu_usage_average_window_ms / config::block_interval_ms,
              config::maximum_elastic_resource_multiplier, {99, 100}, {1000, 999}},
            { EOS_PERCENT(chain_config.max_block_net_usage, chain_config.target_block_net_usage_pct),
              chain_config.max_block_net_usage,
              config::block_size_average_window_ms / config::block_interval_ms,
              config::maximum_elastic_resource_multiplier, {99, 100}, {1000, 999}}
            );
         resource_limits.process_block_usage(bb.block_num());

         // Any proposer policy?
         std::unique_ptr<proposer_policy> new_proposer_policy;
         auto process_new_proposer_policy = [&](auto&) -> void {
            const auto& gpo = db.get<global_property_object>();
            if (gpo.proposed_schedule_block_num != 0) {
               new_proposer_policy                    = std::make_unique<proposer_policy>();
               new_proposer_policy->active_time       = detail::get_next_next_round_block_time(bb.timestamp());
               new_proposer_policy->proposer_schedule = producer_authority_schedule::from_shared(gpo.proposed_schedule);

               db.modify( gpo, [&]( auto& gp ) {
                  gp.proposed_schedule_block_num = 0;
                  gp.proposed_schedule.version = 0;
                  gp.proposed_schedule.producers.clear();
               });
            }
         };
         apply_s<void>(chain_head, process_new_proposer_policy);

         auto assembled_block =
            bb.assemble_block(thread_pool.get_executor(), protocol_features.get_protocol_feature_set(), fork_db, std::move(new_proposer_policy),
                              validating, std::move(validating_qc_data), validating_bsp);

         // Update TaPoS table:
         create_block_summary(  assembled_block.id() );

         pending->_block_stage = std::move(assembled_block);
      }
      FC_CAPTURE_AND_RETHROW()
   }

   /**
    * @post regardless of the success of commit block there is no active pending block
    */
   void commit_block( controller::block_status s ) {
      auto reset_pending_on_exit = fc::make_scoped_exit([this]{
         pending.reset();
      });

      try {
         EOS_ASSERT( std::holds_alternative<completed_block>(pending->_block_stage), block_validate_exception,
                     "cannot call commit_block until pending block is completed" );

         auto& cb = std::get<completed_block>(pending->_block_stage);

         if (s != controller::block_status::irreversible) {
            auto add_completed_block = [&](auto& forkdb) {
               const auto& bsp = std::get<std::decay_t<decltype(forkdb.head())>>(cb.bsp.internal());
               if( s == controller::block_status::incomplete ) {
                  forkdb.add( bsp, mark_valid_t::yes, ignore_duplicate_t::no );
                  emit( accepted_block_header, std::tie(bsp->block, bsp->id()) );
               } else {
                  assert(s != controller::block_status::irreversible);
                  forkdb.mark_valid( bsp );
               }
            };
            fork_db.apply<void>(add_completed_block);
         }

         chain_head = block_handle{cb.bsp};
         emit( accepted_block, std::tie(chain_head.block(), chain_head.id()) );

         apply<void>(chain_head, [&](const auto& head) {
#warning todo: support deep_mind_logger even when in IF mode
            if constexpr (std::is_same_v<block_state_legacy_ptr, typename std::decay_t<decltype(head)>>) {
               // at block level, no transaction specific logging is possible
               if (auto* dm_logger = get_deep_mind_logger(false)) {
                  dm_logger->on_accepted_block(head);
               }
            }
         });

         if( s == controller::block_status::incomplete ) {
            fork_db.apply_s<void>([&](auto& forkdb) {
               const auto& bsp = std::get<std::decay_t<decltype(forkdb.head())>>(cb.bsp.internal());

               uint16_t if_ext_id = instant_finality_extension::extension_id();
               assert(bsp->header_exts.count(if_ext_id) > 0); // in all instant_finality block headers
               const auto& if_ext = std::get<instant_finality_extension>(bsp->header_exts.lower_bound(if_ext_id)->second);
               if (if_ext.qc_claim.is_strong_qc) {
                  // claim has already been verified
                  auto claimed = forkdb.search_on_branch(bsp->id(), if_ext.qc_claim.block_num);
                  if (claimed) {
                     auto& final_on_strong_qc_block_ref = claimed->core.get_block_reference(claimed->core.final_on_strong_qc_block_num);
                     set_if_irreversible_block_id(final_on_strong_qc_block_ref.block_id);
                  }
               }
            });

            log_irreversible();
         }

         if ( s == controller::block_status::incomplete || s == controller::block_status::complete || s == controller::block_status::validated ) {
            apply_s<void>(chain_head, [&](const auto& head) { create_and_send_vote_msg(head); });
         }

         // TODO: temp transition to instant-finality, happens immediately after block with new_finalizer_policy
         auto transition = [&](const auto& head) -> bool {
            std::optional<block_header_extension> ext = head->block->extract_header_extension(instant_finality_extension::extension_id());
            if (ext) {
               const auto& if_extension = std::get<instant_finality_extension>(*ext);
               if (if_extension.new_finalizer_policy) {
                  ilog("Transition to instant finality happening after block ${b}", ("b", head->block_num()));
                  set_if_irreversible_block_id(head->id());

                  // cancel any proposed schedule changes, prepare for new ones under instant_finality
                  const auto& gpo = db.get<global_property_object>();
                  db.modify(gpo, [&](auto& gp) {
                     gp.proposed_schedule_block_num = 0;
                     gp.proposed_schedule.version   = 0;
                     gp.proposed_schedule.producers.clear();
                  });

                  {
                     // If Leap started at a block prior to the IF transition, it needs to provide a default safety
                     // information for those finalizers that don't already have one. This typically should be done when
                     // we create the non-legacy fork_db, as from this point we may need to cast votes to participate
                     // to the IF consensus.
                     // See https://github.com/AntelopeIO/leap/issues/2070#issuecomment-1941901836
                     // [if todo] set values accurately
                     // -----------------------------------------------------------------------------------------------
                     auto start_block = head;
                     auto lib_block   = head;
                     my_finalizers.set_default_safety_information(
                        finalizer_safety_information{ .last_vote_range_start = block_timestamp_type(0),
                                                      .last_vote = {start_block->id(), start_block->timestamp()},
                                                      .lock      = {lib_block->id(),   lib_block->timestamp()} });
                  }

                  if ( (s != controller::block_status::irreversible && read_mode != db_read_mode::IRREVERSIBLE) && s != controller::block_status::ephemeral)
                     log_irreversible();
                  return true;
               }
            }
            return false;
         };
         if (apply_l<bool>(chain_head, transition)) {
            assert(std::holds_alternative<block_state_legacy_ptr>(chain_head.internal()));
            block_state_legacy_ptr head = std::get<block_state_legacy_ptr>(chain_head.internal()); // will throw if called after transistion

            // Calculate Merkle tree root in Savanna way so that it is stored in
            // Leaf Node when building block_state.
            assert(cb.action_receipt_digests_savanna);
            auto action_mroot = calculate_merkle(std::move(*cb.action_receipt_digests_savanna));

            auto new_head = std::make_shared<block_state>(*head, action_mroot);

            chain_head = block_handle{std::move(new_head)};
            if (s != controller::block_status::irreversible) {
               fork_db.switch_from_legacy(chain_head.internal());
            }
         }

      } catch (...) {
         // dont bother resetting pending, instead abort the block
         reset_pending_on_exit.cancel();
         abort_block();
         throw;
      }

      // push the state for pending.
      pending->push();
   }

   void set_proposed_finalizers(const finalizer_policy& fin_pol) {
      assert(pending); // has to exist and be building_block since called from host function
      auto& bb = std::get<building_block>(pending->_block_stage);
      bb.set_proposed_finalizer_policy(fin_pol);
   }

   /**
    *  This method is called from other threads. The controller_impl should outlive those threads.
    *  However, to avoid race conditions, it means that the behavior of this function should not change
    *  after controller_impl construction.

    *  This should not be an issue since the purpose of this function is to ensure all of the protocol features
    *  in the supplied vector are recognized by the software, and the set of recognized protocol features is
    *  determined at startup and cannot be changed without a restart.
    */
   void check_protocol_features( block_timestamp_type timestamp,
                                 const flat_set<digest_type>& currently_activated_protocol_features,
                                 const vector<digest_type>& new_protocol_features )
   {
      const auto& pfs = protocol_features.get_protocol_feature_set();

      for( auto itr = new_protocol_features.begin(); itr != new_protocol_features.end(); ++itr ) {
         const auto& f = *itr;

         auto status = pfs.is_recognized( f, timestamp );
         switch( status ) {
            case protocol_feature_set::recognized_t::unrecognized:
               EOS_THROW( protocol_feature_exception,
                          "protocol feature with digest '${digest}' is unrecognized", ("digest", f) );
            break;
            case protocol_feature_set::recognized_t::disabled:
               EOS_THROW( protocol_feature_exception,
                          "protocol feature with digest '${digest}' is disabled", ("digest", f) );
            break;
            case protocol_feature_set::recognized_t::too_early:
               EOS_THROW( protocol_feature_exception,
                          "${timestamp} is too early for the earliest allowed activation time of the protocol feature with digest '${digest}'", ("digest", f)("timestamp", timestamp) );
            break;
            case protocol_feature_set::recognized_t::ready:
            break;
            default:
               EOS_THROW( protocol_feature_exception, "unexpected recognized_t status" );
            break;
         }

         EOS_ASSERT( currently_activated_protocol_features.find( f ) == currently_activated_protocol_features.end(),
                     protocol_feature_exception,
                     "protocol feature with digest '${digest}' has already been activated",
                     ("digest", f)
         );

         auto dependency_checker = [&currently_activated_protocol_features, &new_protocol_features, &itr]
                                   ( const digest_type& f ) -> bool
         {
            if( currently_activated_protocol_features.find( f ) != currently_activated_protocol_features.end() )
               return true;

            return (std::find( new_protocol_features.begin(), itr, f ) != itr);
         };

         EOS_ASSERT( pfs.validate_dependencies( f, dependency_checker ), protocol_feature_exception,
                     "not all dependencies of protocol feature with digest '${digest}' have been activated",
                     ("digest", f)
         );
      }
   }

   void report_block_header_diff( const block_header& b, const block_header& ab ) {

#define EOS_REPORT(DESC,A,B) \
      if( A != B ) {                                                    \
         elog("${desc}: ${bv} != ${abv}", ("desc", DESC)("bv", A)("abv", B)); \
      }

      EOS_REPORT( "timestamp", b.timestamp, ab.timestamp )
      EOS_REPORT( "producer", b.producer, ab.producer )
      EOS_REPORT( "confirmed", b.confirmed, ab.confirmed )
      EOS_REPORT( "previous", b.previous, ab.previous )
      EOS_REPORT( "transaction_mroot", b.transaction_mroot, ab.transaction_mroot )
      EOS_REPORT( "action_mroot", b.action_mroot, ab.action_mroot )
      EOS_REPORT( "schedule_version", b.schedule_version, ab.schedule_version )
      EOS_REPORT( "new_producers", b.new_producers, ab.new_producers )
      EOS_REPORT( "header_extensions", b.header_extensions, ab.header_extensions )

      if (b.header_extensions != ab.header_extensions) {
         flat_multimap<uint16_t, block_header_extension> bheader_exts = b.validate_and_extract_header_extensions();
         if (bheader_exts.count(instant_finality_extension::extension_id())) {
            const auto& if_extension =
               std::get<instant_finality_extension>(bheader_exts.lower_bound(instant_finality_extension::extension_id())->second);
            elog("b  if: ${i}", ("i", if_extension));
         }
         flat_multimap<uint16_t, block_header_extension> abheader_exts = ab.validate_and_extract_header_extensions();
         if (abheader_exts.count(instant_finality_extension::extension_id())) {
            const auto& if_extension =
               std::get<instant_finality_extension>(abheader_exts.lower_bound(instant_finality_extension::extension_id())->second);
            elog("ab if: ${i}", ("i", if_extension));
         }
      }

#undef EOS_REPORT
   }

   static std::optional<qc_data_t> extract_qc_data(const signed_block_ptr& b) {
      std::optional<qc_data_t> qc_data;
      auto hexts = b->validate_and_extract_header_extensions();
      if (auto if_entry = hexts.lower_bound(instant_finality_extension::extension_id()); if_entry != hexts.end()) {
         auto& if_ext   = std::get<instant_finality_extension>(if_entry->second);

         // get the matching qc extension if present
         auto exts = b->validate_and_extract_extensions();
         if (auto entry = exts.lower_bound(quorum_certificate_extension::extension_id()); entry != exts.end()) {
            auto& qc_ext = std::get<quorum_certificate_extension>(entry->second);
            return qc_data_t{ std::move(qc_ext.qc), if_ext.qc_claim };
         }
         return qc_data_t{ {}, if_ext.qc_claim };
      }
      return {};
   }

   template<class BSP>
   void apply_block( controller::block_report& br, const BSP& bsp, controller::block_status s,
                     const trx_meta_cache_lookup& trx_lookup ) {
      try {
         try {
            auto start = fc::time_point::now();
            const signed_block_ptr& b = bsp->block;
            const auto& new_protocol_feature_activations = bsp->get_new_protocol_feature_activations();

            auto producer_block_id = bsp->id();
            start_block( b->timestamp, b->confirmed, new_protocol_feature_activations, s, producer_block_id, fc::time_point::maximum() );

            // validated in create_block_handle()
            std::get<building_block>(pending->_block_stage).trx_mroot_or_receipt_digests() = b->transaction_mroot;

            const bool existing_trxs_metas = !bsp->trxs_metas().empty();
            const bool pub_keys_recovered = bsp->is_pub_keys_recovered();
            const bool skip_auth_checks = skip_auth_check();
            std::vector<std::tuple<transaction_metadata_ptr, recover_keys_future>> trx_metas;
            bool use_bsp_cached = false;
            if( pub_keys_recovered || (skip_auth_checks && existing_trxs_metas) ) {
               use_bsp_cached = true;
            } else {
               trx_metas.reserve( b->transactions.size() );
               for( const auto& receipt : b->transactions ) {
                  if( std::holds_alternative<packed_transaction>(receipt.trx)) {
                     const auto& pt = std::get<packed_transaction>(receipt.trx);
                     transaction_metadata_ptr trx_meta_ptr = trx_lookup ? trx_lookup( pt.id() ) : transaction_metadata_ptr{};
                     if( trx_meta_ptr && *trx_meta_ptr->packed_trx() != pt ) trx_meta_ptr = nullptr;
                     if( trx_meta_ptr && ( skip_auth_checks || !trx_meta_ptr->recovered_keys().empty() ) ) {
                        trx_metas.emplace_back( std::move( trx_meta_ptr ), recover_keys_future{} );
                     } else if( skip_auth_checks ) {
                        packed_transaction_ptr ptrx( b, &pt ); // alias signed_block_ptr
                        trx_metas.emplace_back(
                           transaction_metadata::create_no_recover_keys( std::move(ptrx), transaction_metadata::trx_type::input ),
                           recover_keys_future{} );
                     } else {
                        packed_transaction_ptr ptrx( b, &pt ); // alias signed_block_ptr
                        auto fut = transaction_metadata::start_recover_keys(
                           std::move( ptrx ), thread_pool.get_executor(), chain_id, fc::microseconds::maximum(),
                           transaction_metadata::trx_type::input  );
                        trx_metas.emplace_back( transaction_metadata_ptr{}, std::move( fut ) );
                     }
                  }
               }
            }

            transaction_trace_ptr trace;

            size_t packed_idx = 0;
            const auto& trx_receipts = std::get<building_block>(pending->_block_stage).pending_trx_receipts();
            for( const auto& receipt : b->transactions ) {
               auto num_pending_receipts = trx_receipts.size();
               if( std::holds_alternative<packed_transaction>(receipt.trx) ) {
                  const auto& trx_meta = (use_bsp_cached ? bsp->trxs_metas().at(packed_idx)
                                                         : (!!std::get<0>(trx_metas.at(packed_idx))
                                                               ? std::get<0>(trx_metas.at(packed_idx))
                                                               : std::get<1>(trx_metas.at(packed_idx)).get()));
                  trace = push_transaction(trx_meta, fc::time_point::maximum(), fc::microseconds::maximum(),
                                           receipt.cpu_usage_us, true, 0);
                  ++packed_idx;
               } else if( std::holds_alternative<transaction_id_type>(receipt.trx) ) {
                  trace = push_scheduled_transaction(std::get<transaction_id_type>(receipt.trx), fc::time_point::maximum(),
                                                     fc::microseconds::maximum(), receipt.cpu_usage_us, true);
               } else {
                  EOS_ASSERT( false, block_validate_exception, "encountered unexpected receipt type" );
               }

               bool transaction_failed   = trace && trace->except;
               bool transaction_can_fail = receipt.status == transaction_receipt_header::hard_fail &&
                                           std::holds_alternative<transaction_id_type>(receipt.trx);

               if( transaction_failed && !transaction_can_fail) {
                  edump((*trace));
                  throw *trace->except;
               }

               EOS_ASSERT(trx_receipts.size() > 0, block_validate_exception,
                          "expected a receipt, block_num ${bn}, block_id ${id}, receipt ${e}",
                          ("bn", b->block_num())("id", producer_block_id)("e", receipt));
               EOS_ASSERT(trx_receipts.size() == num_pending_receipts + 1, block_validate_exception,
                          "expected receipt was not added, block_num ${bn}, block_id ${id}, receipt ${e}",
                          ("bn", b->block_num())("id", producer_block_id)("e", receipt));
               const transaction_receipt_header& r = trx_receipts.back();
               EOS_ASSERT(r == static_cast<const transaction_receipt_header&>(receipt), block_validate_exception,
                          "receipt does not match, ${lhs} != ${rhs}",
                          ("lhs", r)("rhs", static_cast<const transaction_receipt_header&>(receipt)));
            }

            if constexpr (std::is_same_v<BSP, block_state_ptr>) {
               // assemble_block will mutate bsp by setting the valid structure
               assemble_block(true, extract_qc_data(b), bsp);

               // verify received finality digest in action_mroot is the same as the actual one

               // For proper IF blocks that do not have an associated Finality Tree defined,
               // its finality_mroot is empty
               digest_type actual_finality_mroot;

               if (!bsp->core.is_genesis_block_num(bsp->core.final_on_strong_qc_block_num)) {
                  actual_finality_mroot = bsp->get_validation_mroot(bsp->core.final_on_strong_qc_block_num);
               }

               EOS_ASSERT(bsp->finality_mroot() == actual_finality_mroot,
                  block_validate_exception,
                  "finality_mroot does not match, received finality_mroot: ${r} != actual_finality_mroot: ${a}",
                  ("r", bsp->finality_mroot())("a", actual_finality_mroot));
            } else {
               assemble_block(true, {}, nullptr);
            }
            auto& ab = std::get<assembled_block>(pending->_block_stage);

            if( producer_block_id != ab.id() ) {
               elog( "Validation block id does not match producer block id" );

               report_block_header_diff(*b, ab.header());

               // this implicitly asserts that all header fields (less the signature) are identical
               EOS_ASSERT(producer_block_id == ab.id(), block_validate_exception, "Block ID does not match, ${producer_block_id} != ${validator_block_id}",
                          ("producer_block_id", producer_block_id)("validator_block_id", ab.id()));
            }

            if( !use_bsp_cached ) {
               bsp->set_trxs_metas( ab.extract_trx_metas(), !skip_auth_checks );
            }
            // create completed_block with the existing block_state as we just verified it is the same as assembled_block
            pending->_block_stage = completed_block{ block_handle{bsp}, ab.get_action_receipt_digests_savanna() };

            br = pending->_block_report; // copy before commit block destroys pending
            commit_block(s);
            br.total_time = fc::time_point::now() - start;

         } catch ( const std::bad_alloc& ) {
            throw;
         } catch ( const boost::interprocess::bad_alloc& ) {
            throw;
         } catch ( const fc::exception& e ) {
            edump((e.to_detail_string()));
            abort_block();
            throw;
         } catch ( const std::exception& e ) {
            edump((e.what()));
            abort_block();
            throw;
         }
      } FC_CAPTURE_AND_RETHROW();
   } /// apply_block


   // called from net threads and controller's thread pool
   vote_status process_vote_message( const vote_message& vote ) {
      auto aggregate_vote = [&vote](auto& forkdb) -> vote_status {
          auto bsp = forkdb.get_block(vote.block_id);
          if (bsp) {
             return bsp->aggregate_vote(vote);
          }
          return vote_status::unknown_block;
      };
      // TODO: https://github.com/AntelopeIO/leap/issues/2057
      // TODO: Do not aggregate votes on block_state if in legacy block fork_db
      auto aggregate_vote_legacy = [](auto&) -> vote_status {
         return vote_status::unknown_block;
      };
      return fork_db.apply<vote_status>(aggregate_vote_legacy, aggregate_vote);
   }

   void create_and_send_vote_msg(const block_state_ptr& bsp) {
      auto finalizer_digest = bsp->compute_finality_digest();

      // Each finalizer configured on the node which is present in the active finalizer policy
      // may create and sign a vote
      // TODO: as a future optimization, we could run maybe_vote on a thread (it would need a
      // lock around the file access). We should document that the voted_block is emitted
      // off the main thread. net_plugin is fine for this to be emitted from any thread.
      // Just need to update the comment in net_plugin
      my_finalizers.maybe_vote(
          *bsp->active_finalizer_policy, bsp, finalizer_digest, [&](const vote_message& vote) {
              // net plugin subscribed to this signal. it will broadcast the vote message
              // on receiving the signal
              emit(voted_block, vote);

              // also aggregate our own vote into the pending_qc for this block.
              boost::asio::post(thread_pool.get_executor(),
                                [control = this, vote]() { control->process_vote_message(vote); });
          });
   }

   // Verify QC claim made by instant_finality_extension in header extension
   // and quorum_certificate_extension in block extension are valid.
   // Called from net-threads. It is thread safe as signed_block is never modified
   // after creation.
   // -----------------------------------------------------------------------------
   void verify_qc_claim( const block_id_type& id, const signed_block_ptr& b, const block_header_state& prev ) {
      auto qc_ext_id = quorum_certificate_extension::extension_id();
      auto if_ext_id = instant_finality_extension::extension_id();

      // extract current block extension and previous header extension
      auto block_exts = b->validate_and_extract_extensions();
      std::optional<block_header_extension> prev_header_ext = prev.header.extract_header_extension(if_ext_id);
      std::optional<block_header_extension> header_ext      = b->extract_header_extension(if_ext_id);

      bool qc_extension_present = block_exts.count(qc_ext_id) != 0;
      uint32_t block_num = b->block_num();

      if( !header_ext ) {
         // If there is no header extension, ensure the block does not have a QC and also the previous
         // block doesn't have a header extension either. Then return early.
         // ------------------------------------------------------------------------------------------
         EOS_ASSERT( !qc_extension_present,
                     invalid_qc_claim,
                     "Block #${b} includes a QC block extension, but doesn't have a finality header extension",
                     ("b", block_num) );

         EOS_ASSERT( !prev_header_ext,
                     invalid_qc_claim,
                     "Block #${b} doesn't have a finality header extension even though its predecessor does.",
                     ("b", block_num) );
         return;
      }

      assert(header_ext);
      const auto& if_ext   = std::get<instant_finality_extension>(*header_ext);
      const auto  new_qc_claim = if_ext.qc_claim;

      // If there is a header extension, but the previous block does not have a header extension,
      // ensure the block does not have a QC and the QC claim of the current block has a block_num
      // of the current block’s number and that it is a claim of a weak QC. Then return early.
      // -------------------------------------------------------------------------------------------------
      if (!prev_header_ext) {
         EOS_ASSERT( !qc_extension_present && new_qc_claim.block_num == block_num && new_qc_claim.is_strong_qc == false,
                     invalid_qc_claim,
                     "Block #${b}, which is the finality transition block, doesn't have the expected extensions",
                     ("b", block_num) );
         return;
      }

      // at this point both current block and its parent have IF extensions, and we are past the
      // IF transition block
      // ----------------------------------------------------------------------------------------
      assert(header_ext && prev_header_ext);

      const auto& prev_if_ext   = std::get<instant_finality_extension>(*prev_header_ext);
      const auto  prev_qc_claim = prev_if_ext.qc_claim;

      // validate QC claim against previous block QC info

      // new claimed QC block number cannot be smaller than previous block's
      EOS_ASSERT( new_qc_claim.block_num >= prev_qc_claim.block_num,
                  invalid_qc_claim,
                  "Block #${b} claims a block_num (${n1}) less than the previous block's (${n2})",
                  ("n1", new_qc_claim.block_num)("n2", prev_qc_claim.block_num)("b", block_num) );

      if( new_qc_claim.block_num == prev_qc_claim.block_num ) {
         if( new_qc_claim.is_strong_qc == prev_qc_claim.is_strong_qc ) {
            // QC block extension is redundant
            EOS_ASSERT( !qc_extension_present,
                        invalid_qc_claim,
                        "Block #${b} should not provide a QC block extension since its QC claim is the same as the previous block's",
                        ("b", block_num) );

            // if previous block's header extension has the same claim, just return
            // (previous block already validated the claim)
            return;
         }

         // new claimed QC must be stronger than previous if the claimed block number is the same
         EOS_ASSERT( new_qc_claim.is_strong_qc,
                     invalid_qc_claim,
                     "claimed QC (${s1}) must be stricter than previous block's (${s2}) if block number is the same. Block number: ${b}",
                     ("s1", new_qc_claim.is_strong_qc)("s2", prev_qc_claim.is_strong_qc)("b", block_num) );
      }

      // At this point, we are making a new claim in this block, so it better include a QC to justify this claim.
      EOS_ASSERT( qc_extension_present,
                  invalid_qc_claim,
                  "Block #${b} is making a new finality claim, but doesn't include a qc to justify this claim", ("b", block_num) );

      const auto& qc_ext   = std::get<quorum_certificate_extension>(block_exts.lower_bound(qc_ext_id)->second);
      const auto& qc_proof = qc_ext.qc;

      // Check QC information in header extension and block extension match
      EOS_ASSERT( qc_proof.block_num == new_qc_claim.block_num,
                  invalid_qc_claim,
                  "Block #${b}: Mismatch between qc.block_num (${n1}) in block extension and block_num (${n2}) in header extension",
                  ("n1", qc_proof.block_num)("n2", new_qc_claim.block_num)("b", block_num) );

      // Verify claimed strictness is the same as in proof
      EOS_ASSERT( qc_proof.qc.is_strong() == new_qc_claim.is_strong_qc,
                  invalid_qc_claim,
                  "QC is_strong (${s1}) in block extension does not match is_strong_qc (${s2}) in header extension. Block number: ${b}",
                  ("s1", qc_proof.qc.is_strong())("s2", new_qc_claim.is_strong_qc)("b", block_num) );

      // find the claimed block's block state on branch of id
      auto bsp = fetch_bsp_on_branch_by_num( prev.id(), new_qc_claim.block_num );
      EOS_ASSERT( bsp,
                  invalid_qc_claim,
                  "Block state was not found in forkdb for block_num ${q}. Block number: ${b}",
                  ("q", new_qc_claim.block_num)("b", block_num) );

      // verify the QC proof against the claimed block
      bsp->verify_qc(qc_proof.qc);
   }

   // thread safe, expected to be called from thread other than the main thread
   template<class BS>
   block_handle create_block_state_i( const block_id_type& id, const signed_block_ptr& b, const BS& prev ) {
      constexpr bool savanna_mode = std::is_same_v<typename std::decay_t<BS>, block_state>;
      if constexpr (savanna_mode) {
         // Verify claim made by instant_finality_extension in block header extension and
         // quorum_certificate_extension in block extension are valid.
         // This is the only place the evaluation is done.
         verify_qc_claim(id, b, prev);
      }

      auto trx_mroot = calculate_trx_merkle( b->transactions, savanna_mode );
      EOS_ASSERT( b->transaction_mroot == trx_mroot,
                  block_validate_exception,
                  "invalid block transaction merkle root ${b} != ${c}", ("b", b->transaction_mroot)("c", trx_mroot) );

      const bool skip_validate_signee = false;
      auto bsp = std::make_shared<BS>(
            prev,
            b,
            protocol_features.get_protocol_feature_set(),
            [this]( block_timestamp_type timestamp,
                    const flat_set<digest_type>& cur_features,
                    const vector<digest_type>& new_features )
            { check_protocol_features( timestamp, cur_features, new_features ); },
            skip_validate_signee
      );

      EOS_ASSERT( id == bsp->id(), block_validate_exception,
                  "provided id ${id} does not match block id ${bid}", ("id", id)("bid", bsp->id()) );
      return block_handle{bsp};
   }

   std::future<block_handle> create_block_handle_future( const block_id_type& id, const signed_block_ptr& b ) {
      EOS_ASSERT( b, block_validate_exception, "null block" );

      auto f = [&](auto& forkdb) -> std::future<block_handle> {
         return post_async_task( thread_pool.get_executor(), [b, id, &forkdb, control=this]() {
            // no reason for a block_state if fork_db already knows about block
            auto existing = forkdb.get_block( id );
            EOS_ASSERT( !existing, fork_database_exception, "we already know about this block: ${id}", ("id", id) );

            auto prev = forkdb.get_block( b->previous, include_root_t::yes );
            EOS_ASSERT( prev, unlinkable_block_exception,
                        "unlinkable block ${id} previous ${p}", ("id", id)("p", b->previous) );

            return control->create_block_state_i( id, b, *prev );
         } );
      };

      return fork_db.apply<std::future<block_handle>>(f);
   }

   // thread safe, expected to be called from thread other than the main thread
   std::optional<block_handle> create_block_handle( const block_id_type& id, const signed_block_ptr& b ) {
      EOS_ASSERT( b, block_validate_exception, "null block" );
      
      auto f = [&](auto& forkdb) -> std::optional<block_handle> {
         // no reason for a block_state if fork_db already knows about block
         auto existing = forkdb.get_block( id );
         EOS_ASSERT( !existing, fork_database_exception, "we already know about this block: ${id}", ("id", id) );

         // previous not found could mean that previous block not applied yet
         auto prev = forkdb.get_block( b->previous, include_root_t::yes );
         if( !prev ) return {};

         return create_block_state_i( id, b, *prev );
      };

      return fork_db.apply<std::optional<block_handle>>(f);
   }

   // expected to be called from application thread as it modifies bsp->valid_qc and if_irreversible_block_id
   void integrate_received_qc_to_block(const block_state_ptr& bsp_in) {
      // extract QC from block extension
      const auto& block_exts = bsp_in->block->validate_and_extract_extensions();
      auto qc_ext_id = quorum_certificate_extension::extension_id();

      if( block_exts.count(qc_ext_id) == 0 ) {
         return;
      }
      const auto& qc_ext = std::get<quorum_certificate_extension>(block_exts.lower_bound(qc_ext_id)->second);
      const auto& received_qc = qc_ext.qc.qc;

      const auto claimed = fetch_bsp_on_branch_by_num( bsp_in->previous(), qc_ext.qc.block_num );
      if( !claimed ) {
         dlog("qc not found in forkdb, qc: ${qc} for block ${bn} ${id}, previous ${p}",
              ("qc", qc_ext.qc.to_qc_claim())("bn", bsp_in->block_num())("id", bsp_in->id())("p", bsp_in->previous()));
         return;
      }

      // Don't save the QC from block extension if the claimed block has a better valid_qc.
      if (claimed->valid_qc && (claimed->valid_qc->is_strong() || received_qc.is_weak())) {
         dlog("qc not better, claimed->valid: ${qbn} ${qid}, strong=${s}, received: ${rqc}, for block ${bn} ${id}",
              ("qbn", claimed->block_num())("qid", claimed->id())("s", claimed->valid_qc->is_strong())
              ("rqc", qc_ext.qc.to_qc_claim())("bn", bsp_in->block_num())("id", bsp_in->id()));
         return;
      }

      // Save the QC. This is safe as the function is called by push_block & accept_block from application thread.
      dlog("setting valid qc: ${rqc} into claimed block ${bn} ${id}", ("rqc", qc_ext.qc.to_qc_claim())("bn", claimed->block_num())("id", claimed->id()));
      claimed->valid_qc = received_qc;

      // advance LIB if QC is strong
      if( received_qc.is_strong() ) {
         // We evaluate a block extension qc and advance lib if strong.
         // This is done before evaluating the block. It is possible the block
         // will not be valid or forked out. This is safe because the block is
         // just acting as a carrier of this info. It doesn't matter if the block
         // is actually valid as it simply is used as a network message for this data.
         const auto& final_on_strong_qc_block_ref = claimed->core.get_block_reference(claimed->core.final_on_strong_qc_block_num);
         set_if_irreversible_block_id(final_on_strong_qc_block_ref.block_id);
      }
   }

   void consider_voting(const block_state_ptr& bsp) {
      // 1. Get the `core.final_on_strong_qc_block_num` for the block you are considering to vote on and use that to find the actual block ID
      //    of the ancestor block that has that block number.
      // 2. If that block ID is for a non validated block, then do not vote for that block.
      // 3. Otherwise, consider voting for that block according to the decide_vote rules.

      if (bsp->core.final_on_strong_qc_block_num > 0) {
         const auto& final_on_strong_qc_block_ref = bsp->core.get_block_reference(bsp->core.final_on_strong_qc_block_num);
         auto final = fetch_bsp(final_on_strong_qc_block_ref.block_id);
         if (final && final->is_valid()) {
            create_and_send_vote_msg(bsp);
         }
      }
   }

   template <class BSP>
   void accept_block(const BSP& bsp) {
      assert(bsp && bsp->block);

      // Save the received QC as soon as possible, no matter whether the block itself is valid or not
      if constexpr (std::is_same_v<BSP, block_state_ptr>) {
         integrate_received_qc_to_block(bsp);
         consider_voting(bsp);
      }

      auto do_accept_block = [&](auto& forkdb) {
         if constexpr (std::is_same_v<BSP, typename std::decay_t<decltype(forkdb.head())>>)
            forkdb.add( bsp, mark_valid_t::no, ignore_duplicate_t::no );

         emit( accepted_block_header, std::tie(bsp->block, bsp->id()) );
      };

      fork_db.apply<void>(do_accept_block);
   }

   template <class BSP>
   void push_block( controller::block_report& br,
                    const BSP& bsp,
                    const forked_callback_t& forked_branch_cb,
                    const trx_meta_cache_lookup& trx_lookup )
   {
      assert(bsp && bsp->block);

      // Save the received QC as soon as possible, no matter whether the block itself is valid or not
      if constexpr (std::is_same_v<BSP, block_state_ptr>) {
         integrate_received_qc_to_block(bsp);
         consider_voting(bsp);
      }

      controller::block_status s = controller::block_status::complete;
      EOS_ASSERT(!pending, block_validate_exception, "it is not valid to push a block when there is a pending block");

      auto reset_prod_light_validation = fc::make_scoped_exit([old_value=trusted_producer_light_validation, this]() {
         trusted_producer_light_validation = old_value;
      });
      try {
         const auto& b = bsp->block;

         if( conf.terminate_at_block > 0 && conf.terminate_at_block <= chain_head.block_num()) {
            ilog("Reached configured maximum block ${num}; terminating", ("num", conf.terminate_at_block) );
            shutdown();
            return;
         }
         
         auto do_push = [&](auto& forkdb) {
            if constexpr (std::is_same_v<BSP, typename std::decay_t<decltype(forkdb.head())>>) {
               forkdb.add( bsp, mark_valid_t::no, ignore_duplicate_t::no );
            }

            if (is_trusted_producer(b->producer)) {
               trusted_producer_light_validation = true;
            };

            emit( accepted_block_header, std::tie(bsp->block, bsp->id()) );

            if( read_mode != db_read_mode::IRREVERSIBLE ) {
               if constexpr (std::is_same_v<BSP, typename std::decay_t<decltype(forkdb.head())>>)
                  maybe_switch_forks( br, forkdb.pending_head(), s, forked_branch_cb, trx_lookup );
            } else {
               log_irreversible();
            }
         };

         fork_db.apply<void>(do_push);

      } FC_LOG_AND_RETHROW( )
   }

   template <class BSP>
   void replay_push_block( const signed_block_ptr& b, controller::block_status s ) {
      validate_db_available_size();

      EOS_ASSERT(!pending, block_validate_exception, "it is not valid to push a block when there is a pending block");

      try {
         EOS_ASSERT( b, block_validate_exception, "trying to push empty block" );
         EOS_ASSERT( (s == controller::block_status::irreversible || s == controller::block_status::validated),
                     block_validate_exception, "invalid block status for replay" );

         if( conf.terminate_at_block > 0 && conf.terminate_at_block <= chain_head.block_num() ) {
            ilog("Reached configured maximum block ${num}; terminating", ("num", conf.terminate_at_block) );
            shutdown();
            return;
         }

         const bool skip_validate_signee = !conf.force_all_checks;
         validator_t validator = [this](block_timestamp_type timestamp, const flat_set<digest_type>& cur_features,
                                        const vector<digest_type>& new_features) {
            check_protocol_features(timestamp, cur_features, new_features);
         };

         auto do_push = [&](const auto& head) {
            if constexpr (std::is_same_v<BSP, typename std::decay_t<decltype(head)>>) {
               BSP bsp = std::make_shared<typename BSP::element_type>(*head, b, protocol_features.get_protocol_feature_set(),validator, skip_validate_signee);

               if (s != controller::block_status::irreversible) {
                  fork_db.apply<void>([&](auto& forkdb) {
                     if constexpr (std::is_same_v<std::decay_t<decltype(bsp)>, std::decay_t<decltype(forkdb.head())>>)
                        forkdb.add(bsp, mark_valid_t::no, ignore_duplicate_t::yes);
                  });
               }

               emit(accepted_block_header, std::tie(bsp->block, bsp->id()));

               controller::block_report br;
               if (s == controller::block_status::irreversible) {
                  apply_block(br, bsp, s, trx_meta_cache_lookup{});

                  // On replay, log_irreversible is not called and so no irreversible_block signal is emitted.
                  // So emit it explicitly here.
                  emit(irreversible_block, std::tie(bsp->block, bsp->id()));

                  if (!skip_db_sessions(s)) {
                     db.commit(bsp->block_num());
                  }
               } else {
                  EOS_ASSERT(read_mode != db_read_mode::IRREVERSIBLE, block_validate_exception,
                             "invariant failure: cannot replay reversible blocks while in irreversible mode");
                  maybe_switch_forks(br, bsp, s, {}, trx_meta_cache_lookup{});
               }
            }
         };
         apply<void>(chain_head, do_push);

      } FC_LOG_AND_RETHROW( )
   }

   void maybe_switch_forks(const forked_callback_t& cb, const trx_meta_cache_lookup& trx_lookup) {
      auto maybe_switch = [&](auto& forkdb) {
         if (read_mode != db_read_mode::IRREVERSIBLE) {
            auto pending_head = forkdb.pending_head();
            if (chain_head.id() != pending_head->id() && pending_head->id() != forkdb.head()->id()) {
               dlog("switching forks on controller->maybe_switch_forks call");
               controller::block_report br;
               maybe_switch_forks(br, pending_head, pending_head->is_valid() ? controller::block_status::validated : controller::block_status::complete,
                                  cb, trx_lookup);
            }
         }
      };

      fork_db.apply<void>(maybe_switch);
   }

   template<class BSP>
   void maybe_switch_forks( controller::block_report& br, const BSP& new_head, controller::block_status s,
                            const forked_callback_t& forked_cb, const trx_meta_cache_lookup& trx_lookup )
   {
      auto do_maybe_switch_forks = [&](auto& forkdb) {
         bool head_changed = true;
         if( new_head->header.previous == chain_head.id() ) {
            try {
               apply_block( br, new_head, s, trx_lookup );
            } catch ( const std::exception& e ) {
               forkdb.remove( new_head->id() );
               throw;
            }
         } else if( new_head->id() != chain_head.id() ) {
            ilog("switching forks from ${current_head_id} (block number ${current_head_num}) ${c} to ${new_head_id} (block number ${new_head_num}) ${n}",
                 ("current_head_id", chain_head.id())("current_head_num", chain_head.block_num())("new_head_id", new_head->id())("new_head_num", new_head->block_num())
                 ("c", log_fork_comparison(chain_head.internal()))("n", log_fork_comparison(*new_head)));

            // not possible to log transaction specific infor when switching forks
            if (auto dm_logger = get_deep_mind_logger(false)) {
               dm_logger->on_switch_forks(chain_head.id(), new_head->id());
            }

            auto branches = forkdb.fetch_branch_from( new_head->id(), chain_head.id() );

            if( branches.second.size() > 0 ) {
               for( auto itr = branches.second.begin(); itr != branches.second.end(); ++itr ) {
                  pop_block();
               }
               EOS_ASSERT( chain_head.id() == branches.second.back()->header.previous, fork_database_exception,
                           "loss of sync between fork_db and chainbase during fork switch" ); // _should_ never fail

               if( forked_cb ) {
                  // forked_branch is in reverse order, maintain execution order
                  for( auto ritr = branches.second.rbegin(), rend = branches.second.rend(); ritr != rend; ++ritr ) {
                     const auto& bsptr = *ritr;
                     for( auto itr = bsptr->trxs_metas().begin(), end = bsptr->trxs_metas().end(); itr != end; ++itr ) {
                        forked_cb(*itr);
                     }
                  }
               }
            }

            for( auto ritr = branches.first.rbegin(); ritr != branches.first.rend(); ++ritr ) {
               auto except = std::exception_ptr{};
               try {
                  br = controller::block_report{};
                  apply_block( br, *ritr, (*ritr)->is_valid() ? controller::block_status::validated
                               : controller::block_status::complete, trx_lookup );
               } catch ( const std::bad_alloc& ) {
                  throw;
               } catch ( const boost::interprocess::bad_alloc& ) {
                  throw;
               } catch (const fc::exception& e) {
                  elog("exception thrown while switching forks ${e}", ("e", e.to_detail_string()));
                  except = std::current_exception();
               } catch (const std::exception& e) {
                  elog("exception thrown while switching forks ${e}", ("e", e.what()));
                  except = std::current_exception();
               }

               if( except ) {
                  // ritr currently points to the block that threw
                  // Remove the block that threw and all forks built off it.
                  forkdb.remove( (*ritr)->id() );

                  // pop all blocks from the bad fork, discarding their transactions
                  // ritr base is a forward itr to the last block successfully applied
                  auto applied_itr = ritr.base();
                  for( auto itr = applied_itr; itr != branches.first.end(); ++itr ) {
                     pop_block();
                  }
                  EOS_ASSERT( chain_head.id() == branches.second.back()->header.previous, fork_database_exception,
                              "loss of sync between fork_db and chainbase during fork switch reversal" ); // _should_ never fail

                  // re-apply good blocks
                  for( auto ritr = branches.second.rbegin(); ritr != branches.second.rend(); ++ritr ) {
                     br = controller::block_report{};
                     apply_block( br, *ritr, controller::block_status::validated /* we previously validated these blocks*/, trx_lookup );
                  }
                  std::rethrow_exception(except);
               } // end if exception
            } /// end for each block in branch

            if (fc::logger::get(DEFAULT_LOGGER).is_enabled(fc::log_level::info)) {
               auto get_ids = [&](auto& container)->std::string {
                  std::string ids;
                  for(auto ritr = container.rbegin(), e = container.rend(); ritr != e; ++ritr) {
                     ids += std::to_string((*ritr)->block_num()) + ":" + (*ritr)->id().str() + ",";
                  }
                  if (!ids.empty()) ids.resize(ids.size()-1);
                  return ids;
               };
               ilog("successfully switched fork to new head ${new_head_id}, removed {${rm_ids}}, applied {${new_ids}}",
                    ("new_head_id", new_head->id())("rm_ids", get_ids(branches.second))("new_ids", get_ids(branches.first)));
            }
         } else {
            head_changed = false;
         }

         if( head_changed )
            log_irreversible();
      };

      fork_db.apply<void>(do_maybe_switch_forks);

   } /// push_block

   deque<transaction_metadata_ptr> abort_block() {
      deque<transaction_metadata_ptr> applied_trxs;
      if( pending ) {
         applied_trxs = pending->extract_trx_metas();
         pending.reset();
         protocol_features.popped_blocks_to( chain_head.block_num() );
      }
      return applied_trxs;
   }

   // @param if_active true if instant finality is active
   static checksum256_type calc_merkle( deque<digest_type>&& digests, bool if_active ) {
      if (if_active) {
         return calculate_merkle( std::move(digests) );
      } else {
         return legacy_merkle( std::move(digests) );
      }
   }

   static checksum256_type calculate_trx_merkle( const deque<transaction_receipt>& trxs, bool if_active ) {
      deque<digest_type> trx_digests;
      for( const auto& a : trxs )
         trx_digests.emplace_back( a.digest() );

      return calc_merkle(std::move(trx_digests), if_active);
   }

   void update_producers_authority() {
      // this is not called when hotstuff is activated
      auto& bb = std::get<building_block>(pending->_block_stage);
      bb.apply_l<void>([this](building_block::building_block_legacy& legacy_header) {
         pending_block_header_state_legacy& pbhs = legacy_header.pending_block_header_state;
         const auto& producers = pbhs.active_schedule.producers;

         auto update_permission = [&](auto& permission, auto threshold) {
            auto auth = authority(threshold, {}, {});
            for (auto& p : producers) {
               auth.accounts.push_back({
                  {p.producer_name, config::active_name},
                  1
               });
            }

            if (permission.auth != auth) {
               db.modify(permission, [&](auto& po) { po.auth = auth; });
            }
         };

         uint32_t num_producers       = producers.size();
         auto     calculate_threshold = [=](uint32_t numerator, uint32_t denominator) {
            return ((num_producers * numerator) / denominator) + 1;
         };

         update_permission(authorization.get_permission({config::producers_account_name, config::active_name}),
                           calculate_threshold(2, 3) /* more than two-thirds */);

         update_permission(
            authorization.get_permission({config::producers_account_name, config::majority_producers_permission_name}),
            calculate_threshold(1, 2) /* more than one-half */);

         update_permission(
            authorization.get_permission({config::producers_account_name, config::minority_producers_permission_name}),
            calculate_threshold(1, 3) /* more than one-third */);

         // TODO: Add tests
      });
   }

   void create_block_summary(const block_id_type& id) {
      auto block_num = block_header::num_from_id(id);
      auto sid = block_num & 0xffff;
      db.modify( db.get<block_summary_object,by_id>(sid), [&](block_summary_object& bso ) {
          bso.block_id = id;
      });
   }


   void clear_expired_input_transactions(const fc::time_point& deadline) {
      //Look for expired transactions in the deduplication list, and remove them.
      auto& transaction_idx = db.get_mutable_index<transaction_multi_index>();
      const auto& dedupe_index = transaction_idx.indices().get<by_expiration>();
      auto now = is_building_block() ? pending_block_time() : chain_head.block_time().to_time_point();
      const auto total = dedupe_index.size();
      uint32_t num_removed = 0;
      while( (!dedupe_index.empty()) && ( now > dedupe_index.begin()->expiration.to_time_point() ) ) {
         transaction_idx.remove(*dedupe_index.begin());
         ++num_removed;
         if( deadline <= fc::time_point::now() ) {
            break;
         }
      }
      dlog("removed ${n} expired transactions of the ${t} input dedup list, pending block time ${pt}",
           ("n", num_removed)("t", total)("pt", now));
   }

   bool sender_avoids_whitelist_blacklist_enforcement( account_name sender )const {
      if( conf.sender_bypass_whiteblacklist.size() > 0 &&
          ( conf.sender_bypass_whiteblacklist.find( sender ) != conf.sender_bypass_whiteblacklist.end() ) )
      {
         return true;
      }

      return false;
   }

   void check_actor_list( const flat_set<account_name>& actors )const {
      if( actors.size() == 0 ) return;

      if( conf.actor_whitelist.size() > 0 ) {
         // throw if actors is not a subset of whitelist
         const auto& whitelist = conf.actor_whitelist;
         bool is_subset = true;

         // quick extents check, then brute force the check actors
         if (*actors.cbegin() >= *whitelist.cbegin() && *actors.crbegin() <= *whitelist.crbegin() ) {
            auto lower_bound = whitelist.cbegin();
            for (const auto& actor: actors) {
               lower_bound = std::lower_bound(lower_bound, whitelist.cend(), actor);

               // if the actor is not found, this is not a subset
               if (lower_bound == whitelist.cend() || *lower_bound != actor ) {
                  is_subset = false;
                  break;
               }

               // if the actor was found, we are guaranteed that other actors are either not present in the whitelist
               // or will be present in the range defined as [next actor,end)
               lower_bound = std::next(lower_bound);
            }
         } else {
            is_subset = false;
         }

         // helper lambda to lazily calculate the actors for error messaging
         static auto generate_missing_actors = [](const flat_set<account_name>& actors, const flat_set<account_name>& whitelist) -> vector<account_name> {
            vector<account_name> excluded;
            excluded.reserve( actors.size() );
            set_difference( actors.begin(), actors.end(),
                            whitelist.begin(), whitelist.end(),
                            std::back_inserter(excluded) );
            return excluded;
         };

         EOS_ASSERT( is_subset,  actor_whitelist_exception,
                     "authorizing actor(s) in transaction are not on the actor whitelist: ${actors}",
                     ("actors", generate_missing_actors(actors, whitelist))
                   );
      } else if( conf.actor_blacklist.size() > 0 ) {
         // throw if actors intersects blacklist
         const auto& blacklist = conf.actor_blacklist;
         bool intersects = false;

         // quick extents check then brute force check actors
         if( *actors.cbegin() <= *blacklist.crbegin() && *actors.crbegin() >= *blacklist.cbegin() ) {
            auto lower_bound = blacklist.cbegin();
            for (const auto& actor: actors) {
               lower_bound = std::lower_bound(lower_bound, blacklist.cend(), actor);

               // if the lower bound in the blacklist is at the end, all other actors are guaranteed to
               // not exist in the blacklist
               if (lower_bound == blacklist.cend()) {
                  break;
               }

               // if the lower bound of an actor IS the actor, then we have an intersection
               if (*lower_bound == actor) {
                  intersects = true;
                  break;
               }
            }
         }

         // helper lambda to lazily calculate the actors for error messaging
         static auto generate_blacklisted_actors = [](const flat_set<account_name>& actors, const flat_set<account_name>& blacklist) -> vector<account_name> {
            vector<account_name> blacklisted;
            blacklisted.reserve( actors.size() );
            set_intersection( actors.begin(), actors.end(),
                              blacklist.begin(), blacklist.end(),
                              std::back_inserter(blacklisted)
                            );
            return blacklisted;
         };

         EOS_ASSERT( !intersects, actor_blacklist_exception,
                     "authorizing actor(s) in transaction are on the actor blacklist: ${actors}",
                     ("actors", generate_blacklisted_actors(actors, blacklist))
                   );
      }
   }

   void check_contract_list( account_name code )const {
      if( conf.contract_whitelist.size() > 0 ) {
         EOS_ASSERT( conf.contract_whitelist.find( code ) != conf.contract_whitelist.end(),
                     contract_whitelist_exception,
                     "account '${code}' is not on the contract whitelist", ("code", code)
                   );
      } else if( conf.contract_blacklist.size() > 0 ) {
         EOS_ASSERT( conf.contract_blacklist.find( code ) == conf.contract_blacklist.end(),
                     contract_blacklist_exception,
                     "account '${code}' is on the contract blacklist", ("code", code)
                   );
      }
   }

   void check_action_list( account_name code, action_name action )const {
      if( conf.action_blacklist.size() > 0 ) {
         EOS_ASSERT( conf.action_blacklist.find( std::make_pair(code, action) ) == conf.action_blacklist.end(),
                     action_blacklist_exception,
                     "action '${code}::${action}' is on the action blacklist",
                     ("code", code)("action", action)
                   );
      }
   }

   void check_key_list( const public_key_type& key )const {
      if( conf.key_blacklist.size() > 0 ) {
         EOS_ASSERT( conf.key_blacklist.find( key ) == conf.key_blacklist.end(),
                     key_blacklist_exception,
                     "public key '${key}' is on the key blacklist",
                     ("key", key)
                   );
      }
   }

   /**
    *  At the start of each block we notify the system contract with a transaction that passes in
    *  the block header of the prior block (which is currently our head block)
    */
   signed_transaction get_on_block_transaction()
   {
      action on_block_act;
      on_block_act.account = config::system_account_name;
      on_block_act.name = "onblock"_n;
      on_block_act.authorization = vector<permission_level>{{config::system_account_name, config::active_name}};
      on_block_act.data = fc::raw::pack(chain_head.header());

      signed_transaction trx;
      trx.actions.emplace_back(std::move(on_block_act));
      if( is_builtin_activated( builtin_protocol_feature_t::no_duplicate_deferred_id ) ) {
         trx.expiration = time_point_sec();
         trx.ref_block_num = 0;
         trx.ref_block_prefix = 0;
      } else {
         trx.expiration = time_point_sec{pending_block_time() + fc::microseconds(999'999)}; // Round up to nearest second to avoid appearing expired
         trx.set_reference_block( chain_head.id() );
      }

      return trx;
   }

   inline deep_mind_handler* get_deep_mind_logger(bool is_trx_transient) const {
      // do not perform deep mind logging for read-only and dry-run transactions
      return is_trx_transient ? nullptr : deep_mind_logger;
   }

   void set_if_irreversible_block_id(const block_id_type& id) {
      const block_num_type id_num = block_header::num_from_id(id);
      const block_num_type current_num = block_header::num_from_id(if_irreversible_block_id);
      if( id_num > current_num ) {
         dlog("set irreversible block ${bn}: ${id}, old ${obn}: ${oid}", ("bn", id_num)("id", id)("obn", current_num)("oid", if_irreversible_block_id));
         if_irreversible_block_id = id;
      }
   }

   uint32_t earliest_available_block_num() const {
      return (blog.first_block_num() != 0) ? blog.first_block_num() : fork_db_root_block_num();
   }

   void set_to_write_window() {
      app_window = app_window_type::write;
   }
   void set_to_read_window() {
      app_window = app_window_type::read;
   }
   bool is_write_window() const {
      return app_window == app_window_type::write;
   }

#ifdef EOSIO_EOS_VM_OC_RUNTIME_ENABLED
   bool is_eos_vm_oc_enabled() const {
      return wasmif.is_eos_vm_oc_enabled();
   }
#endif

   // Only called from read-only trx execution threads when producer_plugin
   // starts them. Only OC requires initialize thread specific data.
   void init_thread_local_data() {
#ifdef EOSIO_EOS_VM_OC_RUNTIME_ENABLED
      if ( is_eos_vm_oc_enabled() ) {
         wasmif.init_thread_local_data();
      }
#endif
   }

   wasm_interface& get_wasm_interface() {
      return wasmif;
   }

   void code_block_num_last_used(const digest_type& code_hash, uint8_t vm_type, uint8_t vm_version, uint32_t block_num) {
      wasmif.code_block_num_last_used(code_hash, vm_type, vm_version, block_num);
   }

   void set_node_finalizer_keys(const bls_pub_priv_key_map_t& finalizer_keys) {
      my_finalizers.set_keys(finalizer_keys);
   }

   bool irreversible_mode() const { return read_mode == db_read_mode::IRREVERSIBLE; }

   bool light_validation_allowed() const {
      if (!pending || in_trx_requiring_checks) {
         return false;
      }

      const auto pb_status = pending->_block_status;

      // in a pending irreversible or previously validated block and we have forcing all checks
      const bool consider_skipping_on_replay =
            (pb_status == controller::block_status::irreversible || pb_status == controller::block_status::validated) && !conf.force_all_checks;

      // OR in a signed block and in light validation mode
      const bool consider_skipping_on_validate = pb_status == controller::block_status::complete &&
                                                 (conf.block_validation_mode == validation_mode::LIGHT || trusted_producer_light_validation);

      return consider_skipping_on_replay || consider_skipping_on_validate;
   }


   bool skip_auth_check() const {
      return light_validation_allowed();
   }

   bool skip_trx_checks() const {
      return light_validation_allowed();
   }

   bool skip_db_sessions( controller::block_status bs ) const {
      bool consider_skipping = bs == controller::block_status::irreversible;
      return consider_skipping
         && !conf.disable_replay_opts
         && !in_trx_requiring_checks;
   }

   bool skip_db_sessions() const {
      if (pending) {
         return skip_db_sessions(pending->_block_status);
      } else {
         return false;
      }
   }

   bool is_trusted_producer( const account_name& producer) const {
      return conf.block_validation_mode == validation_mode::LIGHT || conf.trusted_producers.count(producer);
   }

   bool is_builtin_activated( builtin_protocol_feature_t f )const {
      uint32_t current_block_num = chain_head.block_num();

      if( pending ) {
         ++current_block_num;
      }

      return protocol_features.is_builtin_activated( f, current_block_num );
   }

   block_timestamp_type pending_block_timestamp()const {
      EOS_ASSERT( pending, block_validate_exception, "no pending block" );

      return pending->timestamp();
   }

   time_point pending_block_time()const {
      return pending_block_timestamp();
   }

   bool is_building_block()const {
      return pending.has_value() && !std::holds_alternative<completed_block>(pending->_block_stage);
   }

   bool is_speculative_block()const {
      if( !pending ) return false;

      return (pending->_block_status == controller::block_status::incomplete || pending->_block_status == controller::block_status::ephemeral );
   }

   std::optional<block_id_type> pending_producer_block_id()const {
      EOS_ASSERT( pending, block_validate_exception, "no pending block" );
      return pending->_producer_block_id;
   }

   void validate_db_available_size() const {
      const auto free = db.get_free_memory();
      const auto guard = conf.state_guard_size;
      EOS_ASSERT(free >= guard, database_guard_exception, "database free: ${f}, guard size: ${g}", ("f", free)("g",guard));
   }

   const producer_authority_schedule& active_producers()const {
      if( !(pending) )
         return head_active_schedule_auth();

      return pending->active_producers();
   }

   const producer_authority_schedule* pending_producers_legacy()const {
      if( !(pending) )
         return head_pending_schedule_auth_legacy();

      return pending->pending_producers_legacy();
   }

}; /// controller_impl

thread_local platform_timer controller_impl::timer;
#if defined(EOSIO_EOS_VM_RUNTIME_ENABLED) || defined(EOSIO_EOS_VM_JIT_RUNTIME_ENABLED)
thread_local eosio::vm::wasm_allocator controller_impl::wasm_alloc;
#endif

const resource_limits_manager&   controller::get_resource_limits_manager()const
{
   return my->resource_limits;
}
resource_limits_manager&         controller::get_mutable_resource_limits_manager()
{
   return my->resource_limits;
}

const authorization_manager&   controller::get_authorization_manager()const
{
   return my->authorization;
}
authorization_manager&         controller::get_mutable_authorization_manager()
{
   return my->authorization;
}

const protocol_feature_manager& controller::get_protocol_feature_manager()const
{
   return my->protocol_features;
}

const subjective_billing& controller::get_subjective_billing()const {
   return my->subjective_bill;
}

subjective_billing& controller::get_mutable_subjective_billing() {
   return my->subjective_bill;
}


controller::controller( const controller::config& cfg, const chain_id_type& chain_id )
:my( new controller_impl( cfg, *this, protocol_feature_set{}, chain_id ) )
{
}

controller::controller( const config& cfg, protocol_feature_set&& pfs, const chain_id_type& chain_id )
:my( new controller_impl( cfg, *this, std::move(pfs), chain_id ) )
{
}

controller::~controller() {
   my->abort_block();
   // controller_impl (my) holds a reference to controller (controller_impl.self).
   // The self is passed to transaction_context which passes it on to apply_context.
   // Currently nothing posted to the thread_pool accesses the `self` reference, but to make
   // sure it is safe in case something is added to the thread pool that does access self,
   // stop the thread pool before the unique_ptr (my) destructor runs.
   my->thread_pool.stop();
}

void controller::add_indices() {
   my->add_indices();
}

void controller::startup( std::function<void()> shutdown, std::function<bool()> check_shutdown, const snapshot_reader_ptr& snapshot ) {
   my->startup(shutdown, check_shutdown, snapshot);
}

void controller::startup( std::function<void()> shutdown, std::function<bool()> check_shutdown, const genesis_state& genesis ) {
   my->startup(shutdown, check_shutdown, genesis);
}

void controller::startup(std::function<void()> shutdown, std::function<bool()> check_shutdown) {
   my->startup(shutdown, check_shutdown);
}

const chainbase::database& controller::db()const { return my->db; }

chainbase::database& controller::mutable_db()const { return my->db; }

void controller::preactivate_feature( const digest_type& feature_digest, bool is_trx_transient ) {
   const auto& pfs = my->protocol_features.get_protocol_feature_set();
   auto cur_time = pending_block_time();

   auto status = pfs.is_recognized( feature_digest, cur_time );
   switch( status ) {
      case protocol_feature_set::recognized_t::unrecognized:
         if( is_speculative_block() ) {
            EOS_THROW( subjective_block_production_exception,
                       "protocol feature with digest '${digest}' is unrecognized", ("digest", feature_digest) );
         } else {
            EOS_THROW( protocol_feature_bad_block_exception,
                       "protocol feature with digest '${digest}' is unrecognized", ("digest", feature_digest) );
         }
      break;
      case protocol_feature_set::recognized_t::disabled:
         if( is_speculative_block() ) {
            EOS_THROW( subjective_block_production_exception,
                       "protocol feature with digest '${digest}' is disabled", ("digest", feature_digest) );
         } else {
            EOS_THROW( protocol_feature_bad_block_exception,
                       "protocol feature with digest '${digest}' is disabled", ("digest", feature_digest) );
         }
      break;
      case protocol_feature_set::recognized_t::too_early:
         if( is_speculative_block() ) {
            EOS_THROW( subjective_block_production_exception,
                       "${timestamp} is too early for the earliest allowed activation time of the protocol feature with digest '${digest}'", ("digest", feature_digest)("timestamp", cur_time) );
         } else {
            EOS_THROW( protocol_feature_bad_block_exception,
                       "${timestamp} is too early for the earliest allowed activation time of the protocol feature with digest '${digest}'", ("digest", feature_digest)("timestamp", cur_time) );
         }
      break;
      case protocol_feature_set::recognized_t::ready:
      break;
      default:
         if( is_speculative_block() ) {
            EOS_THROW( subjective_block_production_exception, "unexpected recognized_t status" );
         } else {
            EOS_THROW( protocol_feature_bad_block_exception, "unexpected recognized_t status" );
         }
      break;
   }

   // The above failures depend on subjective information.
   // Because of deferred transactions, this complicates things considerably.

   // If producing a block, we throw a subjective failure if the feature is not properly recognized in order
   // to try to avoid retiring into a block a deferred transacton driven by subjective information.

   // But it is still possible for a producer to retire a deferred transaction that deals with this subjective
   // information. If they recognized the feature, they would retire it successfully, but a validator that
   // does not recognize the feature should reject the entire block (not just fail the deferred transaction).
   // Even if they don't recognize the feature, the producer could change their nodeos code to treat it like an
   // objective failure thus leading the deferred transaction to retire with soft_fail or hard_fail.
   // In this case, validators that don't recognize the feature would reject the whole block immediately, and
   // validators that do recognize the feature would likely lead to a different retire status which would
   // ultimately cause a validation failure and thus rejection of the block.
   // In either case, it results in rejection of the block which is the desired behavior in this scenario.

   // If the feature is properly recognized by producer and validator, we have dealt with the subjectivity and
   // now only consider the remaining failure modes which are deterministic and objective.
   // Thus the exceptions that can be thrown below can be regular objective exceptions
   // that do not cause immediate rejection of the block.

   EOS_ASSERT( !is_protocol_feature_activated( feature_digest ),
               protocol_feature_exception,
               "protocol feature with digest '${digest}' is already activated",
               ("digest", feature_digest)
   );

   const auto& pso = my->db.get<protocol_state_object>();

   EOS_ASSERT( std::find( pso.preactivated_protocol_features.begin(),
                          pso.preactivated_protocol_features.end(),
                          feature_digest
               ) == pso.preactivated_protocol_features.end(),
               protocol_feature_exception,
               "protocol feature with digest '${digest}' is already pre-activated",
               ("digest", feature_digest)
   );

   auto dependency_checker = [&]( const digest_type& d ) -> bool
   {
      if( is_protocol_feature_activated( d ) ) return true;

      return ( std::find( pso.preactivated_protocol_features.begin(),
                          pso.preactivated_protocol_features.end(),
                          d ) != pso.preactivated_protocol_features.end() );
   };

   EOS_ASSERT( pfs.validate_dependencies( feature_digest, dependency_checker ),
               protocol_feature_exception,
               "not all dependencies of protocol feature with digest '${digest}' have been activated or pre-activated",
               ("digest", feature_digest)
   );

   if (auto dm_logger = get_deep_mind_logger(is_trx_transient)) {
      const auto feature = pfs.get_protocol_feature(feature_digest);

      dm_logger->on_preactivate_feature(feature);
   }

   my->db.modify( pso, [&]( auto& ps ) {
      ps.preactivated_protocol_features.emplace_back(feature_digest);
   } );
}

vector<digest_type> controller::get_preactivated_protocol_features()const {
   const auto& pso = my->db.get<protocol_state_object>();

   if( pso.preactivated_protocol_features.size() == 0 ) return {};

   vector<digest_type> preactivated_protocol_features;

   for( const auto& f : pso.preactivated_protocol_features ) {
      preactivated_protocol_features.emplace_back( f );
   }

   return preactivated_protocol_features;
}

void controller::validate_protocol_features( const vector<digest_type>& features_to_activate )const {
   my->check_protocol_features( my->chain_head.block_time(),
                                my->head_activated_protocol_features()->protocol_features,
                                features_to_activate );
}

void controller::start_block( block_timestamp_type when,
                              uint16_t confirm_block_count,
                              const vector<digest_type>& new_protocol_feature_activations,
                              block_status bs,
                              const fc::time_point& deadline )
{
   validate_db_available_size();

   if( new_protocol_feature_activations.size() > 0 ) {
      validate_protocol_features( new_protocol_feature_activations );
   }

   EOS_ASSERT( bs == block_status::incomplete || bs == block_status::ephemeral, block_validate_exception, "speculative block type required" );

   my->start_block( when, confirm_block_count, new_protocol_feature_activations,
                    bs, std::optional<block_id_type>(), deadline );
}

void controller::assemble_and_complete_block( block_report& br, const signer_callback_type& signer_callback ) {
   validate_db_available_size();

   my->assemble_block(false, {}, nullptr);

   auto& ab = std::get<assembled_block>(my->pending->_block_stage);
   const auto& valid_block_signing_authority = my->head_active_schedule_auth().get_scheduled_producer(ab.timestamp()).authority;
   my->pending->_block_stage = ab.complete_block(
      my->protocol_features.get_protocol_feature_set(),
      [](block_timestamp_type timestamp, const flat_set<digest_type>& cur_features, const vector<digest_type>& new_features) {},
      signer_callback,
      valid_block_signing_authority);

   br = my->pending->_block_report;
}

void controller::commit_block() {
   validate_db_available_size();
   my->commit_block(block_status::incomplete);
}

void controller::maybe_switch_forks(const forked_callback_t& cb, const trx_meta_cache_lookup& trx_lookup) {
   validate_db_available_size();
   my->maybe_switch_forks(cb, trx_lookup);
}


deque<transaction_metadata_ptr> controller::abort_block() {
   return my->abort_block();
}

boost::asio::io_context& controller::get_thread_pool() {
   return my->thread_pool.get_executor();
}

std::future<block_handle> controller::create_block_handle_future( const block_id_type& id, const signed_block_ptr& b ) {
   return my->create_block_handle_future( id, b );
}

std::optional<block_handle> controller::create_block_handle( const block_id_type& id, const signed_block_ptr& b ) const {
   return my->create_block_handle( id, b );
}

void controller::push_block( block_report& br,
                             const block_handle& bh,
                             const forked_callback_t& forked_cb,
                             const trx_meta_cache_lookup& trx_lookup )
{
   validate_db_available_size();
   apply<void>(bh, [&](const auto& bsp) { my->push_block( br, bsp, forked_cb, trx_lookup); });
}

void controller::accept_block(const block_handle& bh) {
   apply<void>(bh, [&](const auto& bsp) { my->accept_block(bsp); });
}

transaction_trace_ptr controller::push_transaction( const transaction_metadata_ptr& trx,
                                                    fc::time_point block_deadline, fc::microseconds max_transaction_time,
                                                    uint32_t billed_cpu_time_us, bool explicit_billed_cpu_time,
                                                    int64_t subjective_cpu_bill_us ) {
   validate_db_available_size();
   EOS_ASSERT( get_read_mode() != db_read_mode::IRREVERSIBLE, transaction_type_exception, "push transaction not allowed in irreversible mode" );
   EOS_ASSERT( trx && !trx->implicit() && !trx->scheduled(), transaction_type_exception, "Implicit/Scheduled transaction not allowed" );
   return my->push_transaction(trx, block_deadline, max_transaction_time, billed_cpu_time_us, explicit_billed_cpu_time, subjective_cpu_bill_us );
}

transaction_trace_ptr controller::push_scheduled_transaction( const transaction_id_type& trxid,
                                                              fc::time_point block_deadline, fc::microseconds max_transaction_time,
                                                              uint32_t billed_cpu_time_us, bool explicit_billed_cpu_time )
{
   EOS_ASSERT( get_read_mode() != db_read_mode::IRREVERSIBLE, transaction_type_exception, "push scheduled transaction not allowed in irreversible mode" );
   validate_db_available_size();
   return my->push_scheduled_transaction( trxid, block_deadline, max_transaction_time, billed_cpu_time_us, explicit_billed_cpu_time );
}

const flat_set<account_name>& controller::get_actor_whitelist() const {
   return my->conf.actor_whitelist;
}
const flat_set<account_name>& controller::get_actor_blacklist() const {
   return my->conf.actor_blacklist;
}
const flat_set<account_name>& controller::get_contract_whitelist() const {
   return my->conf.contract_whitelist;
}
const flat_set<account_name>& controller::get_contract_blacklist() const {
   return my->conf.contract_blacklist;
}
const flat_set< pair<account_name, action_name> >& controller::get_action_blacklist() const {
   return my->conf.action_blacklist;
}
const flat_set<public_key_type>& controller::get_key_blacklist() const {
   return my->conf.key_blacklist;
}

void controller::set_actor_whitelist( const flat_set<account_name>& new_actor_whitelist ) {
   my->conf.actor_whitelist = new_actor_whitelist;
}
void controller::set_actor_blacklist( const flat_set<account_name>& new_actor_blacklist ) {
   my->conf.actor_blacklist = new_actor_blacklist;
}
void controller::set_contract_whitelist( const flat_set<account_name>& new_contract_whitelist ) {
   my->conf.contract_whitelist = new_contract_whitelist;
}
void controller::set_contract_blacklist( const flat_set<account_name>& new_contract_blacklist ) {
   my->conf.contract_blacklist = new_contract_blacklist;
}
void controller::set_action_blacklist( const flat_set< pair<account_name, action_name> >& new_action_blacklist ) {
   for (auto& act: new_action_blacklist) {
      EOS_ASSERT(act.first != account_name(), name_type_exception, "Action blacklist - contract name should not be empty");
      EOS_ASSERT(act.second != action_name(), action_type_exception, "Action blacklist - action name should not be empty");
   }
   my->conf.action_blacklist = new_action_blacklist;
}
void controller::set_key_blacklist( const flat_set<public_key_type>& new_key_blacklist ) {
   my->conf.key_blacklist = new_key_blacklist;
}

void controller::set_disable_replay_opts( bool v ) {
   my->conf.disable_replay_opts = v;
}

uint32_t controller::head_block_num()const {
   return my->chain_head.block_num();
}
block_timestamp_type controller::head_block_timestamp()const {
   return my->chain_head.block_time();
}
time_point controller::head_block_time()const {
   return my->chain_head.block_time();
}
block_id_type controller::head_block_id()const {
   return my->chain_head.id();
}

account_name  controller::head_block_producer()const {
   return my->chain_head.producer();
}

const block_header& controller::head_block_header()const {
   return my->chain_head.header();
}

block_state_legacy_ptr controller::head_block_state_legacy()const {
   // returns null after instant finality activated
   return apply_l<block_state_legacy_ptr>(my->chain_head, [](const auto& head) {
      return head;
   });
}

const signed_block_ptr& controller::head_block()const {
   return my->chain_head.block();
}

uint32_t controller::fork_db_head_block_num()const {
   return my->fork_db_head_block_num();
}

block_id_type controller::fork_db_head_block_id()const {
   return my->fork_db_head_block_id();
}

block_timestamp_type controller::pending_block_timestamp()const {
   return my->pending_block_timestamp();
}

time_point controller::pending_block_time()const {
   return my->pending_block_time();
}

uint32_t controller::pending_block_num()const {
   EOS_ASSERT( my->pending, block_validate_exception, "no pending block" );
   return my->pending->block_num();
}

account_name controller::pending_block_producer()const {
   EOS_ASSERT( my->pending, block_validate_exception, "no pending block" );
   return my->pending->producer();
}

const block_signing_authority& controller::pending_block_signing_authority() const {
   EOS_ASSERT( my->pending, block_validate_exception, "no pending block" );
   return my->pending->pending_block_signing_authority();
}

std::optional<block_id_type> controller::pending_producer_block_id()const {
   return my->pending_producer_block_id();
}

void controller::set_if_irreversible_block_id(const block_id_type& id) {
   my->set_if_irreversible_block_id(id);
}

uint32_t controller::if_irreversible_block_num() const {
   return block_header::num_from_id(my->if_irreversible_block_id);
}

uint32_t controller::last_irreversible_block_num() const {
   return my->fork_db_root_block_num();
}

block_id_type controller::last_irreversible_block_id() const {
   return my->fork_db_root_block_id();
}

time_point controller::last_irreversible_block_time() const {
   return my->fork_db_root_timestamp().to_time_point();
}


const dynamic_global_property_object& controller::get_dynamic_global_properties()const {
  return my->db.get<dynamic_global_property_object>();
}
const global_property_object& controller::get_global_properties()const {
  return my->db.get<global_property_object>();
}

signed_block_ptr controller::fetch_block_by_id( const block_id_type& id )const {
   auto sb_ptr = my->fork_db_fetch_block_by_id(id);
   if( sb_ptr ) return sb_ptr;
   auto bptr = my->blog.read_block_by_num( block_header::num_from_id(id) );
   if( bptr && bptr->calculate_id() == id ) return bptr;
   return signed_block_ptr();
}

bool controller::block_exists(const block_id_type& id) const {
   bool exists = my->fork_db_block_exists(id);
   if( exists ) return true;
   std::optional<signed_block_header> sbh = my->blog.read_block_header_by_num( block_header::num_from_id(id) );
   if( sbh && sbh->calculate_id() == id ) return true;
   return false;
}

std::optional<signed_block_header> controller::fetch_block_header_by_id( const block_id_type& id )const {
   auto sb_ptr = my->fork_db_fetch_block_by_id(id);
   if( sb_ptr ) return *static_cast<signed_block_header*>(sb_ptr.get());
   auto result = my->blog.read_block_header_by_num( block_header::num_from_id(id) );
   if( result && result->calculate_id() == id ) return result;
   return {};
}

signed_block_ptr controller::fetch_block_by_number( uint32_t block_num )const  { try {
   auto b = my->fetch_block_on_head_branch_by_num( block_num );
   if (b)
      return b;

   return my->blog.read_block_by_num(block_num);
} FC_CAPTURE_AND_RETHROW( (block_num) ) }

std::optional<signed_block_header> controller::fetch_block_header_by_number( uint32_t block_num )const  { try {
   auto b = my->fetch_block_on_head_branch_by_num(block_num);
   if (b)
      return *b;

   return my->blog.read_block_header_by_num(block_num);
} FC_CAPTURE_AND_RETHROW( (block_num) ) }


block_id_type controller::get_block_id_for_num( uint32_t block_num )const { try {
   const auto& blog_head = my->blog.head();

   bool find_in_blog = (blog_head && block_num <= blog_head->block_num());

   if( !find_in_blog ) {
      std::optional<block_id_type> id = my->fetch_block_id_on_head_branch_by_num(block_num);
      if (id) return *id;
   }

   auto id = my->blog.read_block_id_by_num(block_num);

   EOS_ASSERT( BOOST_LIKELY( id != block_id_type() ), unknown_block_exception,
               "Could not find block: ${block}", ("block", block_num) );

   return id;
} FC_CAPTURE_AND_RETHROW( (block_num) ) }

digest_type controller::get_strong_digest_by_id( const block_id_type& id ) const {
   return my->get_strong_digest_by_id(id);
}

fc::sha256 controller::calculate_integrity_hash() { try {
   return my->calculate_integrity_hash();
} FC_LOG_AND_RETHROW() }

void controller::write_snapshot( const snapshot_writer_ptr& snapshot ) {
   EOS_ASSERT( !my->pending, block_validate_exception, "cannot take a consistent snapshot with a pending block" );
   my->writing_snapshot.store(true, std::memory_order_release);
   fc::scoped_exit<std::function<void()>> e = [&] {
      my->writing_snapshot.store(false, std::memory_order_release);
   };
   my->add_to_snapshot(snapshot);
}

bool controller::is_writing_snapshot() const {
   return my->writing_snapshot.load(std::memory_order_acquire);
}

int64_t controller::set_proposed_producers( vector<producer_authority> producers ) {
   assert(my->pending);
   if (my->pending->is_legacy()) {
      return my->set_proposed_producers_legacy(std::move(producers));
   } else {
      return my->set_proposed_producers(std::move(producers));
   }
}

int64_t controller_impl::set_proposed_producers( vector<producer_authority> producers ) {
   // Savanna sets the global_property_object.proposed_schedule similar to legacy, but it is only set during the building of the block.
   // global_property_object is used instead of building_block so that if the transaction fails it is rolledback.

   if (producers.empty())
      return -1; // regardless of disallow_empty_producer_schedule

   assert(pending);
   const auto& gpo = db.get<global_property_object>();
   auto cur_block_num = chain_head.block_num() + 1;

   producer_authority_schedule sch;

   sch.version = pending->get_next_proposer_schedule_version();
   sch.producers = std::move(producers);

   ilog( "proposed producer schedule with version ${v}", ("v", sch.version) );

   // overwrite any existing proposed_schedule set earlier in this block
   db.modify( gpo, [&]( auto& gp ) {
      gp.proposed_schedule_block_num = cur_block_num;
      gp.proposed_schedule = sch;
   });

   return sch.version;
}

int64_t controller_impl::set_proposed_producers_legacy( vector<producer_authority> producers ) {
   const auto& gpo = db.get<global_property_object>();
   auto cur_block_num = chain_head.block_num() + 1;

   if( producers.size() == 0 && is_builtin_activated( builtin_protocol_feature_t::disallow_empty_producer_schedule ) ) {
      return -1;
   }

   if( gpo.proposed_schedule_block_num ) {
      if( *gpo.proposed_schedule_block_num != cur_block_num )
         return -1; // there is already a proposed schedule set in a previous block, wait for it to become pending

      if( std::equal( producers.begin(), producers.end(),
                      gpo.proposed_schedule.producers.begin(), gpo.proposed_schedule.producers.end() ) )
         return -1; // the proposed producer schedule does not change
   }

   producer_authority_schedule sch;

   decltype(sch.producers.cend()) end;
   decltype(end)                  begin;

   const auto* pending_sch = pending_producers_legacy();
   assert(pending_sch); // can't be null during dpos

   if( pending_sch->producers.size() == 0 ) {
      const auto& active_sch = active_producers();
      begin = active_sch.producers.begin();
      end   = active_sch.producers.end();
      sch.version = active_sch.version + 1;
   } else {
      begin = pending_sch->producers.begin();
      end   = pending_sch->producers.end();
      sch.version = pending_sch->version + 1;
   }

   if( std::equal( producers.begin(), producers.end(), begin, end ) )
      return -1; // the producer schedule would not change

   sch.producers = std::move(producers);

   int64_t version = sch.version;

   ilog( "proposed producer schedule with version ${v}", ("v", version) );

   db.modify( gpo, [&]( auto& gp ) {
      gp.proposed_schedule_block_num = cur_block_num;
      gp.proposed_schedule = sch;
   });
   return version;
}

void controller::set_proposed_finalizers( const finalizer_policy& fin_pol ) {
   my->set_proposed_finalizers(fin_pol);
}

// called from net threads
vote_status controller::process_vote_message( const vote_message& vote ) {
   return my->process_vote_message( vote );
};

const producer_authority_schedule& controller::active_producers()const {
   return my->active_producers();
}

const producer_authority_schedule& controller::head_active_producers()const {
   return my->head_active_schedule_auth();
}

const producer_authority_schedule* controller::pending_producers_legacy()const {
   return my->pending_producers_legacy();
}

std::optional<producer_authority_schedule> controller::proposed_producers_legacy()const {
   const auto& gpo = get_global_properties();
   if( !gpo.proposed_schedule_block_num )
      return std::optional<producer_authority_schedule>();

   return producer_authority_schedule::from_shared(gpo.proposed_schedule);
}

const producer_authority_schedule* controller::next_producers()const {
   if( !(my->pending) )
      return my->next_producers();

   return my->pending->next_producers();
}

bool controller::light_validation_allowed() const {
   return my->light_validation_allowed();
}

bool controller::skip_auth_check() const {
   return my->skip_auth_check();
}

bool controller::skip_trx_checks() const {
   return my->skip_trx_checks();
}

bool controller::skip_db_sessions( block_status bs ) const {
   return my->skip_db_sessions( bs );
}

bool controller::skip_db_sessions() const {
   return my->skip_db_sessions();
}

bool controller::is_trusted_producer( const account_name& producer) const {
   return my->is_trusted_producer( producer );
}

bool controller::contracts_console()const {
   return my->conf.contracts_console;
}

bool controller::is_profiling(account_name account) const {
   return my->conf.profile_accounts.find(account) != my->conf.profile_accounts.end();
}

chain_id_type controller::get_chain_id()const {
   return my->chain_id;
}

db_read_mode controller::get_read_mode()const {
   return my->read_mode;
}

validation_mode controller::get_validation_mode()const {
   return my->conf.block_validation_mode;
}

uint32_t controller::get_terminate_at_block()const {
   return my->conf.terminate_at_block;
}

const apply_handler* controller::find_apply_handler( account_name receiver, account_name scope, action_name act ) const
{
   auto native_handler_scope = my->apply_handlers.find( receiver );
   if( native_handler_scope != my->apply_handlers.end() ) {
      auto handler = native_handler_scope->second.find( make_pair( scope, act ) );
      if( handler != native_handler_scope->second.end() )
         return &handler->second;
   }
   return nullptr;
}
wasm_interface& controller::get_wasm_interface() {
   return my->get_wasm_interface();
}

const account_object& controller::get_account( account_name name )const
{ try {
   return my->db.get<account_object, by_name>(name);
} FC_CAPTURE_AND_RETHROW( (name) ) }

bool controller::sender_avoids_whitelist_blacklist_enforcement( account_name sender )const {
   return my->sender_avoids_whitelist_blacklist_enforcement( sender );
}

void controller::check_actor_list( const flat_set<account_name>& actors )const {
   my->check_actor_list( actors );
}

void controller::check_contract_list( account_name code )const {
   my->check_contract_list( code );
}

void controller::check_action_list( account_name code, action_name action )const {
   my->check_action_list( code, action );
}

void controller::check_key_list( const public_key_type& key )const {
   my->check_key_list( key );
}

bool controller::is_building_block()const {
   return my->is_building_block();
}

bool controller::is_speculative_block()const {
   return my->is_speculative_block();
}

bool controller::is_ram_billing_in_notify_allowed()const {
   return my->conf.disable_all_subjective_mitigations || !is_speculative_block() || my->conf.allow_ram_billing_in_notify;
}

uint32_t controller::configured_subjective_signature_length_limit()const {
   return my->conf.maximum_variable_signature_length;
}

void controller::validate_expiration( const transaction& trx )const { try {
   const auto& chain_configuration = get_global_properties().configuration;

   EOS_ASSERT( trx.expiration.to_time_point() >= pending_block_time(),
               expired_tx_exception,
               "transaction has expired, "
               "expiration is ${trx.expiration} and pending block time is ${pending_block_time}",
               ("trx.expiration",trx.expiration)("pending_block_time",pending_block_time()));
   EOS_ASSERT( trx.expiration.to_time_point() <= pending_block_time() + fc::seconds(chain_configuration.max_transaction_lifetime),
               tx_exp_too_far_exception,
               "Transaction expiration is too far in the future relative to the reference time of ${reference_time}, "
               "expiration is ${trx.expiration} and the maximum transaction lifetime is ${max_til_exp} seconds",
               ("trx.expiration",trx.expiration)("reference_time",pending_block_time())
               ("max_til_exp",chain_configuration.max_transaction_lifetime) );
} FC_CAPTURE_AND_RETHROW((trx)) }

void controller::validate_tapos( const transaction& trx )const { try {
   const auto& tapos_block_summary = db().get<block_summary_object>((uint16_t)trx.ref_block_num);

   //Verify TaPoS block summary has correct ID prefix, and that this block's time is not past the expiration
   EOS_ASSERT(trx.verify_reference_block(tapos_block_summary.block_id), invalid_ref_block_exception,
              "Transaction's reference block did not match. Is this transaction from a different fork?",
              ("tapos_summary", tapos_block_summary));
} FC_CAPTURE_AND_RETHROW() }

void controller::validate_db_available_size() const {
   return my->validate_db_available_size();
}

bool controller::is_protocol_feature_activated( const digest_type& feature_digest )const {
   if( my->pending )
      return my->pending->is_protocol_feature_activated( feature_digest );

   const auto& activated_features = my->head_activated_protocol_features()->protocol_features;
   return (activated_features.find( feature_digest ) != activated_features.end());
}

bool controller::is_builtin_activated( builtin_protocol_feature_t f )const {
   return my->is_builtin_activated( f );
}

bool controller::is_known_unexpired_transaction( const transaction_id_type& id) const {
   return db().find<transaction_object, by_trx_id>(id);
}

void controller::set_subjective_cpu_leeway(fc::microseconds leeway) {
   my->subjective_cpu_leeway = leeway;
}

std::optional<fc::microseconds> controller::get_subjective_cpu_leeway() const {
    return my->subjective_cpu_leeway;
}

void controller::set_greylist_limit( uint32_t limit ) {
   EOS_ASSERT( 0 < limit && limit <= chain::config::maximum_elastic_resource_multiplier,
               misc_exception,
               "Invalid limit (${limit}) passed into set_greylist_limit. "
               "Must be between 1 and ${max}.",
               ("limit", limit)("max", chain::config::maximum_elastic_resource_multiplier)
   );
   my->conf.greylist_limit = limit;
}

uint32_t controller::get_greylist_limit()const {
   return my->conf.greylist_limit;
}

void controller::add_resource_greylist(const account_name &name) {
   my->conf.resource_greylist.insert(name);
}

void controller::remove_resource_greylist(const account_name &name) {
   my->conf.resource_greylist.erase(name);
}

bool controller::is_resource_greylisted(const account_name &name) const {
   return my->conf.resource_greylist.find(name) !=  my->conf.resource_greylist.end();
}

const flat_set<account_name> &controller::get_resource_greylist() const {
   return  my->conf.resource_greylist;
}


void controller::add_to_ram_correction( account_name account, uint64_t ram_bytes ) {
   auto ptr = my->db.find<account_ram_correction_object, by_name>( account );
   if( ptr ) {
      my->db.modify<account_ram_correction_object>( *ptr, [&]( auto& rco ) {
         rco.ram_correction += ram_bytes;
      } );
   } else {
      ptr = &my->db.create<account_ram_correction_object>( [&]( auto& rco ) {
         rco.name = account;
         rco.ram_correction = ram_bytes;
      } );
   }

   // on_add_ram_correction is only called for deferred transaction
   // (in apply_context::schedule_deferred_transaction)
   if (auto dm_logger = get_deep_mind_logger(false)) {
      dm_logger->on_add_ram_correction(*ptr, ram_bytes);
   }
}

bool controller::all_subjective_mitigations_disabled()const {
   return my->conf.disable_all_subjective_mitigations;
}

deep_mind_handler* controller::get_deep_mind_logger(bool is_trx_transient)const {
   return my->get_deep_mind_logger(is_trx_transient);
}

void controller::enable_deep_mind(deep_mind_handler* logger) {
   EOS_ASSERT( logger != nullptr, misc_exception, "Invalid logger passed into enable_deep_mind, must be set" );
   my->deep_mind_logger = logger;
}

uint32_t controller::earliest_available_block_num() const{
   return my->earliest_available_block_num();
}
#if defined(EOSIO_EOS_VM_RUNTIME_ENABLED) || defined(EOSIO_EOS_VM_JIT_RUNTIME_ENABLED)
vm::wasm_allocator& controller::get_wasm_allocator() {
   return my->wasm_alloc;
}
#endif
#ifdef EOSIO_EOS_VM_OC_RUNTIME_ENABLED
bool controller::is_eos_vm_oc_enabled() const {
   return my->is_eos_vm_oc_enabled();
}
#endif

std::optional<uint64_t> controller::convert_exception_to_error_code( const fc::exception& e ) {
   const chain_exception* e_ptr = dynamic_cast<const chain_exception*>( &e );

   if( e_ptr == nullptr ) return {};

   if( !e_ptr->error_code ) return static_cast<uint64_t>(system_error_code::generic_system_error);

   return e_ptr->error_code;
}

signal<void(uint32_t)>&                    controller::block_start() { return my->block_start; }
signal<void(const block_signal_params&)>&  controller::accepted_block_header() { return my->accepted_block_header; }
signal<void(const block_signal_params&)>&  controller::accepted_block() { return my->accepted_block; }
signal<void(const block_signal_params&)>&  controller::irreversible_block() { return my->irreversible_block; }
signal<void(std::tuple<const transaction_trace_ptr&, const packed_transaction_ptr&>)>& controller::applied_transaction() { return my->applied_transaction; }
signal<void(const vote_message&)>&         controller::voted_block() { return my->voted_block; }

chain_id_type controller::extract_chain_id(snapshot_reader& snapshot) {
   chain_snapshot_header header;
   snapshot.read_section<chain_snapshot_header>([&header]( auto &section ){
      section.read_row(header);
      header.validate();
   });

   // check if this is a legacy version of the snapshot, which has a genesis state instead of chain id
   std::optional<genesis_state> genesis = controller_impl::extract_legacy_genesis_state(snapshot, header.version);
   if (genesis) {
      return genesis->compute_chain_id();
   }

   chain_id_type chain_id;

   using v4 = legacy::snapshot_global_property_object_v4;
   if (header.version <= v4::maximum_version) {
      snapshot.read_section<global_property_object>([&chain_id]( auto &section ){
         v4 global_properties;
         section.read_row(global_properties);
         chain_id = global_properties.chain_id;
      });
   }
   else {
      snapshot.read_section<global_property_object>([&chain_id]( auto &section ){
         snapshot_global_property_object global_properties;
         section.read_row(global_properties);
         chain_id = global_properties.chain_id;
      });
   }

   return chain_id;
}

std::optional<chain_id_type> controller::extract_chain_id_from_db( const path& state_dir ) {
   try {
      chainbase::database db( state_dir, chainbase::database::read_only );

      db.add_index<database_header_multi_index>();
      db.add_index<global_property_multi_index>();

      controller_impl::validate_db_version( db );

      if( db.revision() < 1 ) return {};

      auto * gpo = db.find<global_property_object>();
      if (gpo==nullptr) return {};

      return gpo->chain_id;
   } catch( const std::system_error& e ) {
      // do not propagate db_error_code::not_found for absent db, so it will be created
      if( e.code().value() != chainbase::db_error_code::not_found )
         throw;
   }

   return {};
}

void controller::replace_producer_keys( const public_key_type& key ) {
   ilog("Replace producer keys with ${k}", ("k", key));
   // can be done even after instant-finality, will be no-op then
   mutable_db().modify( db().get<global_property_object>(), [&]( auto& gp ) {
      gp.proposed_schedule_block_num = {};
      gp.proposed_schedule.version = 0;
      gp.proposed_schedule.producers.clear();
   });

   my->replace_producer_keys(key);
}

void controller::replace_account_keys( name account, name permission, const public_key_type& key ) {
   auto& rlm = get_mutable_resource_limits_manager();
   auto* perm = db().find<permission_object, by_owner>(boost::make_tuple(account, permission));
   if (!perm)
      return;
   int64_t old_size = (int64_t)(chain::config::billable_size_v<permission_object> + perm->auth.get_billable_size());
   mutable_db().modify(*perm, [&](auto& p) {
      p.auth = authority(key);
   });
   int64_t new_size = (int64_t)(chain::config::billable_size_v<permission_object> + perm->auth.get_billable_size());
   rlm.add_pending_ram_usage(account, new_size - old_size, false); // false for doing dm logging
   rlm.verify_account_ram_usage(account);
}

void controller::set_producer_node(bool is_producer_node) {
   my->is_producer_node = is_producer_node;
}

bool controller::is_producer_node()const {
   return my->is_producer_node;
}

void controller::set_db_read_only_mode() {
   mutable_db().set_read_only_mode();
}

void controller::unset_db_read_only_mode() {
   mutable_db().unset_read_only_mode();
}

void controller::init_thread_local_data() {
   my->init_thread_local_data();
}

void controller::set_to_write_window() {
   my->set_to_write_window();
}
void controller::set_to_read_window() {
   my->set_to_read_window();
}
bool controller::is_write_window() const {
   return my->is_write_window();
}

void controller::code_block_num_last_used(const digest_type& code_hash, uint8_t vm_type, uint8_t vm_version, uint32_t block_num) {
   return my->code_block_num_last_used(code_hash, vm_type, vm_version, block_num);
}

void controller::set_node_finalizer_keys(const bls_pub_priv_key_map_t& finalizer_keys) {
   my->set_node_finalizer_keys(finalizer_keys);
}

/// Protocol feature activation handlers:

template<>
void controller_impl::on_activation<builtin_protocol_feature_t::preactivate_feature>() {
   db.modify( db.get<protocol_state_object>(), [&]( auto& ps ) {
      add_intrinsic_to_whitelist( ps.whitelisted_intrinsics, "preactivate_feature" );
      add_intrinsic_to_whitelist( ps.whitelisted_intrinsics, "is_feature_activated" );
   } );
}

template<>
void controller_impl::on_activation<builtin_protocol_feature_t::get_sender>() {
   db.modify( db.get<protocol_state_object>(), [&]( auto& ps ) {
      add_intrinsic_to_whitelist( ps.whitelisted_intrinsics, "get_sender" );
   } );
}

template<>
void controller_impl::on_activation<builtin_protocol_feature_t::replace_deferred>() {
   const auto& indx = db.get_index<account_ram_correction_index, by_id>();
   for( auto itr = indx.begin(); itr != indx.end(); itr = indx.begin() ) {
      int64_t current_ram_usage = resource_limits.get_account_ram_usage( itr->name );
      int64_t ram_delta = -static_cast<int64_t>(itr->ram_correction);
      if( itr->ram_correction > static_cast<uint64_t>(current_ram_usage) ) {
         ram_delta = -current_ram_usage;
         elog( "account ${name} was to be reduced by ${adjust} bytes of RAM despite only using ${current} bytes of RAM",
               ("name", itr->name)("adjust", itr->ram_correction)("current", current_ram_usage) );
      }

      // This method is only called for deferred transaction
      if (auto dm_logger = get_deep_mind_logger(false)) {
         dm_logger->on_ram_trace(RAM_EVENT_ID("${id}", ("id", itr->id._id)), "deferred_trx", "correction", "deferred_trx_ram_correction");
      }

      resource_limits.add_pending_ram_usage( itr->name, ram_delta, false ); // false for doing dm logging
      db.remove( *itr );
   }
}

template<>
void controller_impl::on_activation<builtin_protocol_feature_t::webauthn_key>() {
   db.modify( db.get<protocol_state_object>(), [&]( auto& ps ) {
      ps.num_supported_key_types = 3;
   } );
}

template<>
void controller_impl::on_activation<builtin_protocol_feature_t::wtmsig_block_signatures>() {
   db.modify( db.get<protocol_state_object>(), [&]( auto& ps ) {
      add_intrinsic_to_whitelist( ps.whitelisted_intrinsics, "set_proposed_producers_ex" );
   } );
}

template<>
void controller_impl::on_activation<builtin_protocol_feature_t::action_return_value>() {
   db.modify( db.get<protocol_state_object>(), [&]( auto& ps ) {
      add_intrinsic_to_whitelist( ps.whitelisted_intrinsics, "set_action_return_value" );
   } );
}

template<>
void controller_impl::on_activation<builtin_protocol_feature_t::configurable_wasm_limits>() {
   db.modify( db.get<protocol_state_object>(), [&]( auto& ps ) {
      add_intrinsic_to_whitelist( ps.whitelisted_intrinsics, "set_wasm_parameters_packed" );
      add_intrinsic_to_whitelist( ps.whitelisted_intrinsics, "get_wasm_parameters_packed" );
   } );
}

template<>
void controller_impl::on_activation<builtin_protocol_feature_t::blockchain_parameters>() {
   db.modify( db.get<protocol_state_object>(), [&]( auto& ps ) {
      add_intrinsic_to_whitelist( ps.whitelisted_intrinsics, "get_parameters_packed" );
      add_intrinsic_to_whitelist( ps.whitelisted_intrinsics, "set_parameters_packed" );
   } );
}

template<>
void controller_impl::on_activation<builtin_protocol_feature_t::get_code_hash>() {
   db.modify( db.get<protocol_state_object>(), [&]( auto& ps ) {
      add_intrinsic_to_whitelist( ps.whitelisted_intrinsics, "get_code_hash" );
   } );
}

template<>
void controller_impl::on_activation<builtin_protocol_feature_t::get_block_num>() {
   db.modify( db.get<protocol_state_object>(), [&]( auto& ps ) {
      add_intrinsic_to_whitelist( ps.whitelisted_intrinsics, "get_block_num" );
   } );
}

template<>
void controller_impl::on_activation<builtin_protocol_feature_t::crypto_primitives>() {
   db.modify( db.get<protocol_state_object>(), [&]( auto& ps ) {
      add_intrinsic_to_whitelist( ps.whitelisted_intrinsics, "alt_bn128_add" );
      add_intrinsic_to_whitelist( ps.whitelisted_intrinsics, "alt_bn128_mul" );
      add_intrinsic_to_whitelist( ps.whitelisted_intrinsics, "alt_bn128_pair" );
      add_intrinsic_to_whitelist( ps.whitelisted_intrinsics, "mod_exp" );
      add_intrinsic_to_whitelist( ps.whitelisted_intrinsics, "blake2_f" );
      add_intrinsic_to_whitelist( ps.whitelisted_intrinsics, "sha3" );
      add_intrinsic_to_whitelist( ps.whitelisted_intrinsics, "k1_recover" );
   } );
}

template<>
void controller_impl::on_activation<builtin_protocol_feature_t::bls_primitives>() {
   db.modify( db.get<protocol_state_object>(), [&]( auto& ps ) {
      add_intrinsic_to_whitelist( ps.whitelisted_intrinsics, "bls_g1_add" );
      add_intrinsic_to_whitelist( ps.whitelisted_intrinsics, "bls_g2_add" );
      add_intrinsic_to_whitelist( ps.whitelisted_intrinsics, "bls_g1_weighted_sum" );
      add_intrinsic_to_whitelist( ps.whitelisted_intrinsics, "bls_g2_weighted_sum" );
      add_intrinsic_to_whitelist( ps.whitelisted_intrinsics, "bls_pairing" );
      add_intrinsic_to_whitelist( ps.whitelisted_intrinsics, "bls_g1_map" );
      add_intrinsic_to_whitelist( ps.whitelisted_intrinsics, "bls_g2_map" );
      add_intrinsic_to_whitelist( ps.whitelisted_intrinsics, "bls_fp_mod" );
      add_intrinsic_to_whitelist( ps.whitelisted_intrinsics, "bls_fp_mul" );
      add_intrinsic_to_whitelist( ps.whitelisted_intrinsics, "bls_fp_exp" );
   } );
}

template<>
void controller_impl::on_activation<builtin_protocol_feature_t::disable_deferred_trxs_stage_2>() {
   const auto& idx = db.get_index<generated_transaction_multi_index, by_trx_id>();
   // remove all deferred trxs and refund their payers
   for( auto itr = idx.begin(); itr != idx.end(); itr = idx.begin() ) {
      remove_scheduled_transaction(*itr);
   }
}

template<>
void controller_impl::on_activation<builtin_protocol_feature_t::instant_finality>() {
   db.modify( db.get<protocol_state_object>(), [&]( auto& ps ) {
      add_intrinsic_to_whitelist( ps.whitelisted_intrinsics, "set_finalizers" );
   } );
}

/// End of protocol feature activation handlers

} /// eosio::chain

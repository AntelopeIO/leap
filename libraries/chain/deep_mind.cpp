#include <eosio/chain/deep_mind.hpp>
#include <eosio/chain/block_state.hpp>
#include <eosio/chain/generated_transaction_object.hpp>
#include <eosio/chain/contract_table_objects.hpp>
#include <eosio/chain/resource_limits_private.hpp>
#include <eosio/chain/permission_object.hpp>
#include <eosio/chain/global_property_object.hpp>
#include <eosio/chain/account_object.hpp>
#include <eosio/chain/protocol_feature_manager.hpp>
#include <fc/crypto/hex.hpp>

namespace {

   void set_trace_elapsed_to_zero(eosio::chain::action_trace& trace) {
      trace.elapsed = fc::microseconds{};
   }

   void set_trace_elapsed_to_zero(eosio::chain::transaction_trace& trace) {
      trace.elapsed = fc::microseconds{};
      for (auto& act_trace : trace.action_traces) {
         set_trace_elapsed_to_zero(act_trace);
      }
   }

}

namespace eosio::chain {

   void deep_mind_handler::update_config(deep_mind_config config)
   {
      _config = std::move(config);
   }

   void deep_mind_handler::update_logger(const std::string& logger_name)
   {
      fc::logger::update( logger_name, _logger );
   }

   static const char* prefix(deep_mind_handler::operation_qualifier q) {
      switch(q)
      {
      case deep_mind_handler::operation_qualifier::none: return "";
      case deep_mind_handler::operation_qualifier::modify: return "MODIFY_";
      case deep_mind_handler::operation_qualifier::push: return "PUSH_";
      default: elog("Unknown operation_qualifier"); return "";
      }
   }

   void deep_mind_handler::on_startup(chainbase::database& db, uint32_t head_block_num)
   {
      // FIXME: We should probably feed that from CMake directly somehow ...
      fc_dlog(_logger, "DEEP_MIND_VERSION leap 13 0");

      fc_dlog(_logger, "ABIDUMP START ${block_num} ${global_sequence_num}",
         ("block_num", head_block_num)
         ("global_sequence_num", db.get<dynamic_global_property_object>().global_action_sequence)
      );
      const auto& idx = db.get_index<account_index>();
      for (auto& row : idx.indices()) {
         if (row.abi.size() != 0) {
            fc_dlog(_logger, "ABIDUMP ABI ${contract} ${abi}",
               ("contract", row.name)
               ("abi", row.abi)
            );
         }
      }
      fc_dlog(_logger, "ABIDUMP END");
   }

   void deep_mind_handler::on_start_block(uint32_t block_num)
   {
      fc_dlog(_logger, "START_BLOCK ${block_num}", ("block_num", block_num));
   }

   void deep_mind_handler::on_accepted_block(const std::shared_ptr<block_state>& bsp)
   {
      auto packed_blk = fc::raw::pack(*bsp);

      fc_dlog(_logger, "ACCEPTED_BLOCK ${num} ${blk}",
         ("num", bsp->block_num)
         ("blk", fc::to_hex(packed_blk))
      );
   }

   void deep_mind_handler::on_switch_forks(const block_id_type& old_head, const block_id_type& new_head)
   {
      fc_dlog(_logger, "SWITCH_FORK ${from_id} ${to_id}",
         ("from_id", old_head)
         ("to_id", new_head)
      );
   }

   void deep_mind_handler::on_onerror(const signed_transaction& etrx)
   {
      auto packed_trx = fc::raw::pack(etrx);

      fc_dlog(_logger, "TRX_OP CREATE onerror ${id} ${trx}",
         ("id", etrx.id())
         ("trx", fc::to_hex(packed_trx))
      );
   }

   void deep_mind_handler::on_onblock(const signed_transaction& trx)
   {
      auto packed_trx = fc::raw::pack(trx);

      fc_dlog(_logger, "TRX_OP CREATE onblock ${id} ${trx}",
         ("id", trx.id())
         ("trx", fc::to_hex(packed_trx))
      );
   }

   void deep_mind_handler::on_start_transaction()
   {
      _action_id = 0;
   }

   void deep_mind_handler::on_end_transaction()
   {
      _action_id = 0;
   }

   void deep_mind_handler::on_applied_transaction(uint32_t block_num, const transaction_trace_ptr& trace)
   {
      std::vector<char> packed_trace;
      
      if (_config.zero_elapsed) {
         transaction_trace trace_copy = *trace;
         set_trace_elapsed_to_zero(trace_copy);
         packed_trace = fc::raw::pack(trace_copy);

      } else {
         packed_trace = fc::raw::pack(*trace);
      }

      fc_dlog(_logger, "APPLIED_TRANSACTION ${block} ${traces}",
         ("block", block_num)
         ("traces", fc::to_hex(packed_trace))
      );
   }

   void deep_mind_handler::on_add_ram_correction(const account_ram_correction_object& rco, uint64_t delta)
   {
      fc_dlog(_logger, "RAM_CORRECTION_OP ${action_id} ${correction_id} ${event_id} ${payer} ${delta}",
         ("action_id", _action_id)
         ("correction_id", rco.id._id)
         ("event_id", _ram_trace.event_id)
         ("payer", rco.name)
         ("delta", delta)
      );
      _ram_trace = ram_trace();
   }

   void deep_mind_handler::on_preactivate_feature(const protocol_feature& feature)
   {
      fc_dlog(_logger, "FEATURE_OP PRE_ACTIVATE ${action_id} ${feature_digest} ${feature}",
         ("action_id", _action_id)
         ("feature_digest", feature.feature_digest)
         ("feature", feature.to_variant())
      );
   }

   void deep_mind_handler::on_activate_feature(const protocol_feature& feature)
   {
      fc_dlog(_logger, "FEATURE_OP ACTIVATE ${feature_digest} ${feature}",
         ("feature_digest", feature.feature_digest)
         ("feature", feature.to_variant())
      );
   }

   void deep_mind_handler::on_input_action()
   {
      fc_dlog(_logger, "CREATION_OP ROOT ${action_id}",
         ("action_id", _action_id)
      );
   }
   void deep_mind_handler::on_end_action()
   {
      ++_action_id;
   }
   void deep_mind_handler::on_require_recipient()
   {
      fc_dlog(_logger, "CREATION_OP NOTIFY ${action_id}",
         ("action_id", _action_id)
      );
   }
   void deep_mind_handler::on_send_inline()
   {
      fc_dlog(_logger, "CREATION_OP INLINE ${action_id}",
         ("action_id", _action_id)
      );
   }
   void deep_mind_handler::on_send_context_free_inline()
   {
      fc_dlog(_logger, "CREATION_OP CFA_INLINE ${action_id}",
         ("action_id", _action_id)
      );
   }
   void deep_mind_handler::on_cancel_deferred(operation_qualifier qual, const generated_transaction_object& gto)
   {
      fc_dlog(_logger, "DTRX_OP ${qual}CANCEL ${action_id} ${sender} ${sender_id} ${payer} ${published} ${delay} ${expiration} ${trx_id} ${trx}",
         ("qual", prefix(qual))
         ("action_id", _action_id)
         ("sender", gto.sender)
         ("sender_id", gto.sender_id)
         ("payer", gto.payer)
         ("published", gto.published)
         ("delay", gto.delay_until)
         ("expiration", gto.expiration)
         ("trx_id", gto.trx_id)
         ("trx", fc::to_hex(gto.packed_trx.data(), gto.packed_trx.size()))
      );
   }
   void deep_mind_handler::on_send_deferred(operation_qualifier qual, const generated_transaction_object& gto)
   {
      fc_dlog(_logger, "DTRX_OP ${qual}CREATE ${action_id} ${sender} ${sender_id} ${payer} ${published} ${delay} ${expiration} ${trx_id} ${trx}",
         ("qual", prefix(qual))
         ("action_id", _action_id)
         ("sender", gto.sender)
         ("sender_id", gto.sender_id)
         ("payer", gto.payer)
         ("published", gto.published)
         ("delay", gto.delay_until)
         ("expiration", gto.expiration)
         ("trx_id", gto.trx_id)
         ("trx", fc::to_hex(gto.packed_trx.data(), gto.packed_trx.size()))
      );
   }
   void deep_mind_handler::on_create_deferred(operation_qualifier qual, const generated_transaction_object& gto, const packed_transaction& packed_trx)
   {
      auto packed_signed_trx = fc::raw::pack(packed_trx.get_signed_transaction());

      fc_dlog(_logger, "DTRX_OP ${qual}CREATE ${action_id} ${sender} ${sender_id} ${payer} ${published} ${delay} ${expiration} ${trx_id} ${trx}",
         ("qual", prefix(qual))
         ("action_id", _action_id)
         ("sender", gto.sender)
         ("sender_id", gto.sender_id)
         ("payer", gto.payer)
         ("published", gto.published)
         ("delay", gto.delay_until)
         ("expiration", gto.expiration)
         ("trx_id", gto.trx_id)
         ("trx", fc::to_hex(packed_signed_trx.data(), packed_signed_trx.size()))
      );
   }
   void deep_mind_handler::on_fail_deferred()
   {
      fc_dlog(_logger, "DTRX_OP FAILED ${action_id}",
         ("action_id", _action_id)
      );
   }
   void deep_mind_handler::on_create_table(const table_id_object& tid)
   {
      fc_dlog(_logger, "TBL_OP INS ${action_id} ${code} ${scope} ${table} ${payer}",
         ("action_id", _action_id)
         ("code", tid.code)
         ("scope", tid.scope)
         ("table", tid.table)
         ("payer", tid.payer)
      );
   }
   void deep_mind_handler::on_remove_table(const table_id_object& tid)
   {
      fc_dlog(_logger, "TBL_OP REM ${action_id} ${code} ${scope} ${table} ${payer}",
         ("action_id", _action_id)
         ("code", tid.code)
         ("scope", tid.scope)
         ("table", tid.table)
         ("payer", tid.payer)
      );
   }
   void deep_mind_handler::on_db_store_i64(const table_id_object& tid, const key_value_object& kvo)
   {
      fc_dlog(_logger, "DB_OP INS ${action_id} ${payer} ${table_code} ${scope} ${table_name} ${primkey} ${ndata}",
         ("action_id", _action_id)
         ("payer", kvo.payer)
         ("table_code", tid.code)
         ("scope", tid.scope)
         ("table_name", tid.table)
         ("primkey", name(kvo.primary_key))
         ("ndata", fc::to_hex(kvo.value.data(), kvo.value.size()))
      );
   }
   void deep_mind_handler::on_db_update_i64(const table_id_object& tid, const key_value_object& kvo, account_name payer, const char* buffer, std::size_t buffer_size)
   {
      fc_dlog(_logger, "DB_OP UPD ${action_id} ${opayer}:${npayer} ${table_code} ${scope} ${table_name} ${primkey} ${odata}:${ndata}",
         ("action_id", _action_id)
         ("opayer", kvo.payer)
         ("npayer", payer)
         ("table_code", tid.code)
         ("scope", tid.scope)
         ("table_name", tid.table)
         ("primkey", name(kvo.primary_key))
         ("odata", fc::to_hex(kvo.value.data(),kvo.value.size()))
         ("ndata", fc::to_hex(buffer, buffer_size))
      );
   }
   void deep_mind_handler::on_db_remove_i64(const table_id_object& tid, const key_value_object& kvo)
   {
      fc_dlog(_logger, "DB_OP REM ${action_id} ${payer} ${table_code} ${scope} ${table_name} ${primkey} ${odata}",
         ("action_id", _action_id)
         ("payer", kvo.payer)
         ("table_code", tid.code)
         ("scope", tid.scope)
         ("table_name", tid.table)
         ("primkey", name(kvo.primary_key))
         ("odata", fc::to_hex(kvo.value.data(), kvo.value.size()))
      );
   }
   void deep_mind_handler::on_init_resource_limits(const resource_limits::resource_limits_config_object& config, const resource_limits::resource_limits_state_object& state)
   {
      fc_dlog(_logger, "RLIMIT_OP CONFIG INS ${data}",
         ("data", config)
      );
      fc_dlog(_logger, "RLIMIT_OP STATE INS ${data}",
         ("data", state)
      );
   }
   void deep_mind_handler::on_init_fee_params(const resource_limits::fee_params_object& fee_params)
   {
      fc_dlog(_logger, "RLIMIT_OP FEE_PARAMS INS ${data}",
         ("data", fee_params)
      );
   }
   void deep_mind_handler::on_update_resource_limits_config(const resource_limits::resource_limits_config_object& config)
   {
      fc_dlog(_logger, "RLIMIT_OP CONFIG UPD ${data}",
         ("data", config)
      );
   }
   void deep_mind_handler::on_update_resource_limits_state(const resource_limits::resource_limits_state_object& state)
   {
      fc_dlog(_logger, "RLIMIT_OP STATE UPD ${data}",
         ("data", state)
      );
   }
   void deep_mind_handler::on_update_fee_params(const resource_limits::fee_params_object& fee_params)
   {
      fc_dlog(_logger, "RLIMIT_OP FEE_PARAMS UPD ${data}",
         ("data", fee_params)
      );
   }
   void deep_mind_handler::on_newaccount_resource_limits(const resource_limits::resource_limits_object& limits, const resource_limits::resource_usage_object& usage)
   {
      fc_dlog(_logger, "RLIMIT_OP ACCOUNT_LIMITS INS ${data}",
         ("data", limits)
      );
      fc_dlog(_logger, "RLIMIT_OP ACCOUNT_USAGE INS ${data}",
         ("data", usage)
      );
   }
   void deep_mind_handler::on_init_account_fee_limits(const resource_limits::fee_limits_object& fee_limits)
   {
      fc_dlog(_logger, "RLIMIT_OP ACCOUNT_FEE_LIMITS INS ${data}",
         ("data", fee_limits)
      );
   }
   void deep_mind_handler::on_update_account_fee_limits(const resource_limits::fee_limits_object& fee_limits)
   {
      fc_dlog(_logger, "RLIMIT_OP ACCOUNT_FEE_LIMITS UPD ${data}",
         ("data", fee_limits)
      );
   }
   void deep_mind_handler::on_update_account_usage(const resource_limits::resource_usage_object& usage)
   {
      fc_dlog(_logger, "RLIMIT_OP ACCOUNT_USAGE UPD ${data}",
         ("data", usage)
      );
   }
   void deep_mind_handler::on_set_account_limits(const resource_limits::resource_limits_object& limits)
   {
      fc_dlog(_logger, "RLIMIT_OP ACCOUNT_LIMITS UPD ${data}",
         ("data", limits)
      );
   }
   void deep_mind_handler::on_ram_trace(std::string&& event_id, const char* family, const char* operation, const char* legacy_tag)
   {
      _ram_trace = ram_trace(std::move(event_id), family, operation, legacy_tag);
   }
   void deep_mind_handler::on_ram_event(account_name account, uint64_t new_usage, int64_t delta)
   {
      fc_dlog(_logger, "RAM_OP ${action_id} ${event_id} ${family} ${operation} ${legacy_tag} ${payer} ${new_usage} ${delta}",
         ("action_id", _action_id)
         ("event_id", _ram_trace.event_id)
         ("family", _ram_trace.family)
         ("operation", _ram_trace.operation)
         ("legacy_tag", _ram_trace.legacy_tag)
         ("payer", account)
         ("new_usage", new_usage)
         ("delta", delta)
      );
      _ram_trace = ram_trace();
   }

   void deep_mind_handler::on_create_permission(const permission_object& p)
   {
      fc_dlog(_logger, "PERM_OP INS ${action_id} ${permission_id} ${data}",
         ("action_id", _action_id)
         ("permission_id", p.id)
         ("data", p)
      );
   }
   void deep_mind_handler::on_modify_permission(const permission_object& old_permission, const permission_object& new_permission)
   {
      fc_dlog(_logger, "PERM_OP UPD ${action_id} ${permission_id} ${data}",
         ("action_id", _action_id)
         ("permission_id", new_permission.id)
         ("data", fc::mutable_variant_object()
            ("old", old_permission)
            ("new", new_permission)
         )
      );
   }
   void deep_mind_handler::on_remove_permission(const permission_object& permission)
   {
      fc_dlog(_logger, "PERM_OP REM ${action_id} ${permission_id} ${data}",
        ("action_id", _action_id)
        ("permission_id", permission.id)
        ("data", permission)
      );
   }

}

#pragma once

#include <fc/log/logger.hpp>

#include <eosio/chain/types.hpp>

namespace eosio::chain {

class account_ram_correction_object;
class generated_transaction_object;
class table_id_object;
struct key_value_object;
class permission_object;
struct block_state;
struct protocol_feature;
struct signed_transaction;
struct packed_transaction;
struct transaction_trace;
struct ram_trace;
namespace resource_limits {
   class resource_limits_config_object;
   class resource_limits_state_object;
   struct resource_limits_object;
   struct resource_usage_object;
}

#define RAM_EVENT_ID( FORMAT, ... ) \
   fc::format_string( FORMAT, fc::mutable_variant_object()__VA_ARGS__ )

struct ram_trace {
public:
   ram_trace(std::string&& event_id, const char* family, const char* operation, const char* legacy_tag)
      :event_id(std::move(event_id)),family(family),operation(operation),legacy_tag(legacy_tag)
   {}

   std::string  event_id   = "generic";
   const char*  family     = "generic";
   const char*  operation  = "generic";
   const char*  legacy_tag = "generic";

   ram_trace()
   {}
};

class deep_mind_handler
{
public:
   struct deep_mind_config {
      bool zero_elapsed = false; // if true, the elapsed field of transaction and action traces is always set to 0 (for reproducibility)
   };

   void update_config(deep_mind_config config);

   void update_logger(const std::string& logger_name);
   enum class operation_qualifier { none, modify, push };

   void on_startup(chainbase::database& db, uint32_t head_block_num);
   void on_start_block(uint32_t block_num);
   void on_accepted_block(const std::shared_ptr<block_state>& bsp);
   void on_switch_forks(const block_id_type& old_head, const block_id_type& new_head);
   void on_onerror(const signed_transaction& etrx);
   void on_onblock(const signed_transaction& trx);
   void on_start_transaction();
   void on_end_transaction();
   void on_applied_transaction(uint32_t block_num, const std::shared_ptr<transaction_trace>& trace);
   void on_add_ram_correction(const account_ram_correction_object& rco, uint64_t delta);
   void on_preactivate_feature(const protocol_feature& feature);
   void on_activate_feature(const protocol_feature& feature);
   void on_input_action();
   void on_end_action();
   void on_require_recipient();
   void on_send_inline();
   void on_send_context_free_inline();
   void on_cancel_deferred(operation_qualifier qual, const generated_transaction_object& gto);
   void on_send_deferred(operation_qualifier qual, const generated_transaction_object& gto);
   void on_create_deferred(operation_qualifier qual, const generated_transaction_object& gto, const packed_transaction& packed_trx);
   void on_fail_deferred();
   void on_create_table(const table_id_object& tid);
   void on_remove_table(const table_id_object& tid);
   void on_db_store_i64(const table_id_object& tid, const key_value_object& kvo);
   void on_db_update_i64(const table_id_object& tid, const key_value_object& kvo, account_name payer, const char* buffer, std::size_t buffer_size);
   void on_db_remove_i64(const table_id_object& tid, const key_value_object& kvo);
   void on_init_resource_limits(const resource_limits::resource_limits_config_object& config, const resource_limits::resource_limits_state_object& state);
   void on_update_resource_limits_config(const resource_limits::resource_limits_config_object& config);
   void on_update_resource_limits_state(const resource_limits::resource_limits_state_object& state);
   void on_newaccount_resource_limits(const resource_limits::resource_limits_object& limits, const resource_limits::resource_usage_object& usage);
   void on_update_account_usage(const resource_limits::resource_usage_object& usage);
   void on_set_account_limits(const resource_limits::resource_limits_object& limits);
   // The trace is consumed by the next ram_event or ram_correction
   void on_ram_trace(std::string&& event_id, const char* family, const char* operation, const char* legacy_tag);
   void on_ram_event(account_name account, uint64_t new_usage, int64_t delta);
   void on_create_permission(const permission_object& p);
   void on_modify_permission(const permission_object& old_permission, const permission_object& new_permission);
   void on_remove_permission(const permission_object& permission);
private:
   uint32_t         _action_id = 0;
   ram_trace        _ram_trace;
   deep_mind_config _config;
   fc::logger       _logger;
};

}

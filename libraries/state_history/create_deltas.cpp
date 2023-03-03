#include <eosio/state_history/create_deltas.hpp>
#include <eosio/state_history/serialization.hpp>

namespace eosio {
namespace state_history {

template <typename T>
bool include_delta(const T& old, const T& curr) {
   return true;
}

bool include_delta(const chain::table_id_object& old, const chain::table_id_object& curr) {
   return old.payer != curr.payer;
}

bool include_delta(const chain::resource_limits::resource_limits_object& old,
                   const chain::resource_limits::resource_limits_object& curr) {
   return                                   //
       old.net_weight != curr.net_weight || //
       old.cpu_weight != curr.cpu_weight || //
       old.ram_bytes != curr.ram_bytes;
}

bool include_delta(const chain::resource_limits::resource_limits_state_object& old,
                   const chain::resource_limits::resource_limits_state_object& curr) {
   return                                                                                       //
       old.average_block_net_usage.last_ordinal != curr.average_block_net_usage.last_ordinal || //
       old.average_block_net_usage.value_ex != curr.average_block_net_usage.value_ex ||         //
       old.average_block_net_usage.consumed != curr.average_block_net_usage.consumed ||         //
       old.average_block_cpu_usage.last_ordinal != curr.average_block_cpu_usage.last_ordinal || //
       old.average_block_cpu_usage.value_ex != curr.average_block_cpu_usage.value_ex ||         //
       old.average_block_cpu_usage.consumed != curr.average_block_cpu_usage.consumed ||         //
       old.total_net_weight != curr.total_net_weight ||                                         //
       old.total_cpu_weight != curr.total_cpu_weight ||                                         //
       old.total_ram_bytes != curr.total_ram_bytes ||                                           //
       old.virtual_net_limit != curr.virtual_net_limit ||                                       //
       old.virtual_cpu_limit != curr.virtual_cpu_limit;
}

bool include_delta(const chain::account_metadata_object& old, const chain::account_metadata_object& curr) {
   return                                               //
       old.name != curr.name ||                         //
       old.is_privileged() != curr.is_privileged() ||   //
       old.last_code_update != curr.last_code_update || //
       old.vm_type != curr.vm_type ||                   //
       old.vm_version != curr.vm_version ||             //
       old.code_hash != curr.code_hash;
}

bool include_delta(const chain::code_object& old, const chain::code_object& curr) { //
   // code_object data that is exported by SHiP is never modified they are only deleted or created,
   // see serialization of history_serial_wrapper<eosio::chain::code_object>
   return false;
}

bool include_delta(const chain::protocol_state_object& old, const chain::protocol_state_object& curr) {
   return old.activated_protocol_features != curr.activated_protocol_features;
}

void pack_deltas(boost::iostreams::filtering_ostreambuf& obuf, const chainbase::database& db, bool full_snapshot) {

   fc::datastream<boost::iostreams::filtering_ostreambuf&> ds{obuf};

   const auto&                                       table_id_index = db.get_index<chain::table_id_multi_index>();
   std::map<uint64_t, const chain::table_id_object*> removed_table_id;
   for (auto& rem : table_id_index.last_undo_session().removed_values)
      removed_table_id[rem.id._id] = &rem;

   auto get_table_id = [&](uint64_t tid) -> const chain::table_id_object& {
      auto obj = table_id_index.find(tid);
      if (obj)
         return *obj;
      auto it = removed_table_id.find(tid);
      EOS_ASSERT(it != removed_table_id.end(), chain::plugin_exception, "can not found table id ${tid}", ("tid", tid));
      return *it->second;
   };

   auto pack_row          = [&](auto& ds, auto& row) { fc::raw::pack(ds, make_history_serial_wrapper(db, row)); };
   auto pack_contract_row = [&](auto& ds, auto& row) {
      fc::raw::pack(ds, make_history_context_wrapper(db, get_table_id(row.t_id._id), row));
   };

   auto process_table = [&](auto& ds, auto* name, auto& index, auto& pack_row) {

      auto pack_row_v0 = [&](auto& ds, bool present, auto& row) {
         fc::raw::pack(ds, present);
         fc::datastream<size_t> ps;
         pack_row(ps, row);
         fc::raw::pack(ds, fc::unsigned_int(ps.tellp()));
         pack_row(ds, row);
      };

      if (full_snapshot) {
         if (index.indices().empty())
            return;

         fc::raw::pack(ds, fc::unsigned_int(0)); // table_delta = std::variant<table_delta_v0> and fc::unsigned_int struct_version
         fc::raw::pack(ds, name);
         fc::raw::pack(ds, fc::unsigned_int(index.indices().size()));
         for (auto& row : index.indices()) {
            pack_row_v0(ds, true, row);
         }
      } else {
         auto undo = index.last_undo_session();

         size_t num_entries =
             std::count_if(undo.old_values.begin(), undo.old_values.end(),
                           [&index](const auto& old) { return include_delta(old, index.get(old.id)); }) +
             std::distance(undo.removed_values.begin(), undo.removed_values.end()) +
             std::distance(undo.new_values.begin(), undo.new_values.end());

         if (num_entries) {
            fc::raw::pack(ds, fc::unsigned_int(0)); // table_delta = std::variant<table_delta_v0> and fc::unsigned_int struct_version
            fc::raw::pack(ds, name);
            fc::raw::pack(ds, fc::unsigned_int((uint32_t)num_entries));

            for (auto& old : undo.old_values) {
               auto& row = index.get(old.id);
               if (include_delta(old, row)) {
                  pack_row_v0(ds, true, row);
               }
            }

            for (auto& old : undo.removed_values) {
               pack_row_v0(ds, false, old);
            }

            for (auto& row : undo.new_values) {
               pack_row_v0(ds, true, row);
            }
         }
      }
   };

   auto has_table = [&](auto x) -> int {
      auto& index = db.get_index<std::remove_pointer_t<decltype(x)>>();
      if (full_snapshot) {
         return !index.indices().empty();
      } else {
         auto undo = index.last_undo_session();
         return std::find_if(undo.old_values.begin(), undo.old_values.end(),
                           [&index](const auto& old) { return include_delta(old, index.get(old.id)); }) != undo.old_values.end() ||
             !undo.removed_values.empty() || !undo.new_values.empty();
      }
   };

   int num_tables = std::apply(
       [&has_table](auto... args) { return (has_table(args) + ... ); },
       std::tuple<chain::account_index*, chain::account_metadata_index*, chain::code_index*,
                  chain::table_id_multi_index*, chain::key_value_index*, chain::index64_index*, chain::index128_index*,
                  chain::index256_index*, chain::index_double_index*, chain::index_long_double_index*,
                  chain::global_property_multi_index*, chain::generated_transaction_multi_index*,
                  chain::protocol_state_multi_index*, chain::permission_index*, chain::permission_link_index*,
                  chain::resource_limits::resource_limits_index*, chain::resource_limits::resource_usage_index*,
                  chain::resource_limits::resource_limits_state_index*,
                  chain::resource_limits::resource_limits_config_index*>());

   fc::raw::pack(ds, fc::unsigned_int(num_tables));

   process_table(ds, "account", db.get_index<chain::account_index>(), pack_row);
   process_table(ds, "account_metadata", db.get_index<chain::account_metadata_index>(), pack_row);
   process_table(ds, "code", db.get_index<chain::code_index>(), pack_row);

   process_table(ds, "contract_table", db.get_index<chain::table_id_multi_index>(), pack_row);
   process_table(ds, "contract_row", db.get_index<chain::key_value_index>(), pack_contract_row);
   process_table(ds, "contract_index64", db.get_index<chain::index64_index>(), pack_contract_row);
   process_table(ds, "contract_index128", db.get_index<chain::index128_index>(), pack_contract_row);
   process_table(ds, "contract_index256", db.get_index<chain::index256_index>(), pack_contract_row);
   process_table(ds, "contract_index_double", db.get_index<chain::index_double_index>(), pack_contract_row);
   process_table(ds, "contract_index_long_double", db.get_index<chain::index_long_double_index>(), pack_contract_row);

   process_table(ds, "global_property", db.get_index<chain::global_property_multi_index>(), pack_row);
   process_table(ds, "generated_transaction", db.get_index<chain::generated_transaction_multi_index>(), pack_row);
   process_table(ds, "protocol_state", db.get_index<chain::protocol_state_multi_index>(), pack_row);

   process_table(ds, "permission", db.get_index<chain::permission_index>(), pack_row);
   process_table(ds, "permission_link", db.get_index<chain::permission_link_index>(), pack_row);

   process_table(ds, "resource_limits", db.get_index<chain::resource_limits::resource_limits_index>(), pack_row);
   process_table(ds, "resource_usage", db.get_index<chain::resource_limits::resource_usage_index>(), pack_row);
   process_table(ds, "resource_limits_state", db.get_index<chain::resource_limits::resource_limits_state_index>(),
                 pack_row);
   process_table(ds, "resource_limits_config", db.get_index<chain::resource_limits::resource_limits_config_index>(),
                 pack_row);

   obuf.pubsync();

}


} // namespace state_history
} // namespace eosio

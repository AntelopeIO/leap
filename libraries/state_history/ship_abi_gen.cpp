#include <eosio/abi.hpp>
#include <iostream>
#include <regex>
#include <iterator>
#include <eosio/state_history/ship_protocol.hpp>


namespace eosio {
namespace ship_protocol {

using types = std::tuple<request, result, account, account_metadata, code, contract_index_double, contract_index_long_double, contract_index128,
               contract_index256, contract_index64, contract_row, contract_table, generated_transaction,
               global_property, permission, permission_link, protocol_state, resource_limits,
               resource_limits_config, resource_limits_state, resource_usage, transaction>;
} // namespace ship_protocol

inline abi_type* add_type(abi& a, ship_protocol::transaction_status*) {
    return &a.abi_types.find("uint8")->second;
}

inline abi_type* add_type(abi& a, std::vector<ship_protocol::recurse_transaction_trace>*) {
    return a.add_type<std::optional<ship_protocol::transaction_trace>>();
}
} // namespace eosio

int main() {

   eosio::abi abi;
   eosio::abi_def empty_def;
   eosio::convert(empty_def, abi);

   std::apply([&abi](auto... x) {
      (abi.add_type<std::decay_t<decltype(x)>>(),...);
   }, eosio::ship_protocol::types{});

   eosio::abi_def ship_abi_def;
   eosio::convert(abi, ship_abi_def);
   ship_abi_def.version="eosio::abi/1.1";

   using namespace eosio::literals;

   ship_abi_def.tables = {
  {.name = "account"_n, .key_names = {"name"}, .type = "account"},
       {.name = "actmetadata"_n, .key_names = {"name"}, .type = "account_metadata"},
       {.name = "code"_n, .key_names = {"vm_type", "vm_version", "code_hash"}, .type = "code"},
       {.name = "contracttbl"_n, .key_names = {"code", "scope", "table"}, .type = "contract_table"},
       {.name = "contractrow"_n, .key_names = {"code", "scope", "table", "primary_key"}, .type = "contract_row"},
       {.name = "cntrctidx1"_n, .key_names = {"code", "scope", "table", "primary_key"}, .type = "contract_index64"},
       {.name = "cntrctidx2"_n, .key_names = {"code", "scope", "table", "primary_key"}, .type = "contract_index128"},
       {.name = "cntrctidx3"_n, .key_names = {"code", "scope", "table", "primary_key"}, .type = "contract_index256"},
       {.name = "cntrctidx4"_n,
        .key_names = {"code", "scope", "table", "primary_key"},
        .type = "contract_index_double"},
       {.name = "cntrctidx5"_n,
        .key_names = {"code", "scope", "table", "primary_key"},
        .type = "contract_index_long_double"},
       {.name = "global.pty"_n, .key_names = {}, .type = "global_property"},
       {.name = "generatedtrx"_n, .key_names = {"sender", "sender_id"}, .type = "generated_transaction"},
       {.name = "protocolst"_n, .key_names = {}, .type = "protocol_state"},
       {.name = "permission"_n, .key_names = {"owner", "name"}, .type = "permission"},
       {.name = "permlink"_n, .key_names = {"account", "code", "message_type"}, .type = "permission_link"},
       {.name = "rsclimits"_n, .key_names = {"owner"}, .type = "resource_limits"},
       {.name = "rscusage"_n, .key_names = {"owner"}, .type = "resource_usage"},
       {.name = "rsclimitsst"_n, .key_names = {}, .type = "resource_limits_state"},
       {.name = "rsclimitscfg"_n, .key_names = {}, .type = "resource_limits_config"}};

   std::vector<char> data;
   eosio::vector_stream strm{data};
   const char* preamble = "extern const char* const state_history_plugin_abi = R\"(";
   strm.write(preamble, strlen(preamble));
   to_json(ship_abi_def, strm);
   strm.write(")\";", 3);

   // remove the empty value elements in the json, like {"name": "myname","type":""}  ==> {"name":"myname"}
   std::regex empty_value_re(R"===(,"[^"]+":(""|\[\]|\{\}))===");
   std::regex_replace(std::ostreambuf_iterator<char>(std::cout),
                      data.begin(), data.end(), empty_value_re, "");


   return 0;
}
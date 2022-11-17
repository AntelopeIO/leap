#include <eosio/abi.hpp>
#include <eosio/ship_protocol.hpp>
#include <iostream>
#include <iterator>
#include <regex>

namespace eosio {
namespace ship_protocol {

using types =
    std::tuple<request, result, account, account_metadata, code, contract_index_double, contract_index_long_double,
               contract_index128, contract_index256, contract_index64, contract_row, contract_table,
               generated_transaction, global_property, permission, permission_link, protocol_state, resource_limits,
               resource_limits_config, resource_limits_state, resource_usage, transaction>;
} // namespace ship_protocol

inline abi_type* add_type(abi& a, ship_protocol::transaction_status*) { return &a.abi_types.find("uint8")->second; }

inline abi_type* add_type(abi& a, std::vector<ship_protocol::recurse_transaction_trace>*) {
   return a.add_type<std::optional<ship_protocol::transaction_trace>>();
}
} // namespace eosio

int main() {

   eosio::abi     abi;
   eosio::abi_def empty_def;
   eosio::convert(empty_def, abi);

   std::apply([&abi](auto... x) { (abi.add_type<std::decay_t<decltype(x)>>(), ...); }, eosio::ship_protocol::types{});

   eosio::abi_def ship_abi_def;
   eosio::convert(abi, ship_abi_def);
   ship_abi_def.version = "eosio::abi/1.1";

   using namespace eosio::literals;

// clang-format off
   ship_abi_def.tables = {
       {.name = "account"_n,     .index_type = "", .key_names = {"name"},                                  .key_types = {}, .type = "account"},
       {.name = "actmetadata"_n, .index_type = "", .key_names = {"name"},                                  .key_types = {}, .type = "account_metadata"},
       {.name = "code"_n,        .index_type = "", .key_names = {"vm_type", "vm_version", "code_hash"},    .key_types = {}, .type = "code"},
       {.name = "contracttbl"_n, .index_type = "", .key_names = {"code", "scope", "table"},                .key_types = {}, .type = "contract_table"},
       {.name = "contractrow"_n, .index_type = "", .key_names = {"code", "scope", "table", "primary_key"}, .key_types = {}, .type = "contract_row"},
       {.name = "cntrctidx1"_n,  .index_type = "", .key_names = {"code", "scope", "table", "primary_key"}, .key_types = {}, .type = "contract_index64"},
       {.name = "cntrctidx2"_n,  .index_type = "", .key_names = {"code", "scope", "table", "primary_key"}, .key_types = {}, .type = "contract_index128"},
       {.name = "cntrctidx3"_n,  .index_type = "", .key_names = {"code", "scope", "table", "primary_key"}, .key_types = {}, .type = "contract_index256"},
       {.name = "cntrctidx4"_n,  .index_type = "", .key_names = {"code", "scope", "table", "primary_key"}, .key_types = {}, .type = "contract_index_double"},
       {.name = "cntrctidx5"_n,  .index_type = "", .key_names = {"code", "scope", "table", "primary_key"}, .key_types = {}, .type = "contract_index_long_double"},
       {.name = "global.pty"_n,  .index_type = "", .key_names = {},                                        .key_types = {}, .type = "global_property"},
       {.name = "generatedtrx"_n,.index_type = "", .key_names  = {"sender", "sender_id"},                  .key_types = {}, .type = "generated_transaction"},
       {.name = "protocolst"_n,  .index_type = "", .key_names = {},                                        .key_types = {}, .type = "protocol_state"},
       {.name = "permission"_n,  .index_type = "", .key_names  = {"owner", "name"},                        .key_types = {}, .type = "permission"},
       {.name = "permlink"_n,    .index_type = "", .key_names  = {"account", "code", "message_type"},      .key_types = {}, .type = "permission_link"},
       {.name = "rsclimits"_n,   .index_type = "", .key_names = {"owner"},                                 .key_types = {}, .type = "resource_limits"},
       {.name = "rscusage"_n,    .index_type = "", .key_names = {"owner"},                                 .key_types = {}, .type = "resource_usage"},
       {.name = "rsclimitsst"_n, .index_type = "", .key_names = {},                                        .key_types = {}, .type = "resource_limits_state"},
       {.name = "rsclimitscfg"_n,.index_type = "", .key_names  = {},                                       .key_types = {}, .type = "resource_limits_config"}};
// clang-format on

   std::vector<char>    data;
   eosio::vector_stream strm{data};
   const char*          preamble = "// This file is generated from ship_abi_gen using ship_protocol.hpp as input, please do NOT modify it directly.\n"
                                   "// Any modification to ship protocol should be done through ship_protocol.hpp and the file can be regenerated via\n"
                                   "// the build system.\n"
                                   "extern const char* const state_history_plugin_abi = R\"(";
   strm.write(preamble, strlen(preamble));
   to_json(ship_abi_def, strm);
   strm.write(")\";", 3);

   // remove the empty value elements in the json, like {"name": "myname","type":""}  ==> {"name":"myname"}
   std::regex empty_value_re(R"===(,"[^"]+":(""|\[\]|\{\}))===");
   std::regex_replace(std::ostreambuf_iterator<char>(std::cout), data.begin(), data.end(), empty_value_re, "");

   return 0;
}
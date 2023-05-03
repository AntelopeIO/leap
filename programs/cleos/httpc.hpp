#pragma once
#include "config.hpp"
#include <fc/variant.hpp>
#include <string>
#include <vector>
#include "do_http_post.hpp"
namespace eosio { namespace client { namespace http {
   using std::string;

   struct config_t {
      std::vector<std::string> headers;
      bool                     no_verify_cert = false;
      bool                     verbose        = false;
      bool                     trace          = false;
      bool                     print_request  = false;
      bool                     print_response = false;
   };

   fc::variant do_http_call(const config_t& config, const std::string& base_uri, const std::string& path,
                            const fc::variant& postdata);

   const string chain_func_base = "/v1/chain";
   const string get_info_func = chain_func_base + "/get_info";
   const string get_finalizer_state_func = chain_func_base + "/get_finalizer_state";
   const string get_transaction_status_func = chain_func_base + "/get_transaction_status";
   const string get_consensus_parameters_func = chain_func_base + "/get_consensus_parameters";
   const string send_txn_func = chain_func_base + "/send_transaction";
   const string push_txn_func = chain_func_base + "/push_transaction";
   const string send2_txn_func = chain_func_base + "/send_transaction2";
   const string send_read_only_txn_func = chain_func_base + "/send_read_only_transaction";
   const string compute_txn_func = chain_func_base + "/compute_transaction";
   const string push_txns_func = chain_func_base + "/push_transactions";
   const string get_block_func = chain_func_base + "/get_block";
   const string get_raw_block_func = chain_func_base + "/get_raw_block";
   const string get_block_header_func = chain_func_base + "/get_block_header";
   const string get_block_info_func = chain_func_base + "/get_block_info";
   const string get_block_header_state_func = chain_func_base + "/get_block_header_state";
   const string get_account_func = chain_func_base + "/get_account";
   const string get_table_func = chain_func_base + "/get_table_rows";
   const string get_table_by_scope_func = chain_func_base + "/get_table_by_scope";
   const string get_code_func = chain_func_base + "/get_code";
   const string get_code_hash_func = chain_func_base + "/get_code_hash";
   const string get_abi_func = chain_func_base + "/get_abi";
   const string get_raw_abi_func = chain_func_base + "/get_raw_abi";
   const string get_raw_code_and_abi_func = chain_func_base + "/get_raw_code_and_abi";
   const string get_currency_balance_func = chain_func_base + "/get_currency_balance";
   const string get_currency_stats_func = chain_func_base + "/get_currency_stats";
   const string get_producers_func = chain_func_base + "/get_producers";
   const string get_schedule_func = chain_func_base + "/get_producer_schedule";
   const string get_required_keys = chain_func_base + "/get_required_keys";

   const string history_func_base = "/v1/history";
   const string trace_api_func_base = "/v1/trace_api";
   const string get_actions_func = history_func_base + "/get_actions";
   const string get_transaction_trace_func = trace_api_func_base + "/get_transaction_trace";
   const string get_block_trace_func = trace_api_func_base + "/get_block";
   const string get_transaction_func = history_func_base + "/get_transaction";
   const string get_key_accounts_func = history_func_base + "/get_key_accounts";
   const string get_controlled_accounts_func = history_func_base + "/get_controlled_accounts";

   const string net_func_base = "/v1/net";
   const string net_connect = net_func_base + "/connect";
   const string net_disconnect = net_func_base + "/disconnect";
   const string net_status = net_func_base + "/status";
   const string net_connections = net_func_base + "/connections";


   const string wallet_func_base = "/v1/wallet";
   const string wallet_create = wallet_func_base + "/create";
   const string wallet_open = wallet_func_base + "/open";
   const string wallet_list = wallet_func_base + "/list_wallets";
   const string wallet_list_keys = wallet_func_base + "/list_keys";
   const string wallet_public_keys = wallet_func_base + "/get_public_keys";
   const string wallet_lock = wallet_func_base + "/lock";
   const string wallet_lock_all = wallet_func_base + "/lock_all";
   const string wallet_unlock = wallet_func_base + "/unlock";
   const string wallet_import_key = wallet_func_base + "/import_key";
   const string wallet_remove_key = wallet_func_base + "/remove_key";
   const string wallet_create_key = wallet_func_base + "/create_key";
   const string wallet_sign_trx = wallet_func_base + "/sign_transaction";
   const string keosd_stop = "/v1/" + string(client::config::key_store_executable_name) + "/stop";

   const string producer_func_base = "/v1/producer";
   const string producer_get_supported_protocol_features_func = producer_func_base + "/get_supported_protocol_features";

 }}}

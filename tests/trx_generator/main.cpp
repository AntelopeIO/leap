#include <eosio/chain_plugin/chain_plugin.hpp>
#include <trx_provider.hpp>
#include <trx_generator.hpp>
#include <boost/algorithm/string.hpp>
#include <fc/bitutil.hpp>
#include <fc/io/raw.hpp>
#include <contracts.hpp>
#include <iostream>

using namespace eosio::testing;
using namespace eosio::chain;
using namespace eosio;

enum return_codes {
   TERMINATED_EARLY = -3,
   OTHER_FAIL = -2,
   INITIALIZE_FAIL = -1,
   SUCCESS = 0,
   BAD_ALLOC = 1,
   DATABASE_DIRTY = 2,
   FIXED_REVERSIBLE = SUCCESS,
   EXTRACTED_GENESIS = SUCCESS,
   NODE_MANAGEMENT_SUCCESS = 5
};

int main(int argc, char** argv) {
   const int64_t trx_expiration_max = 3600;
   const uint16_t generator_id_max = 960;
   variables_map vmap;
   options_description cli("Transaction Generator command line options.");
   uint16_t gen_id = 0;
   string chain_id_in;
   string contract_owner_acct;
   string accts;
   string p_keys;
   int64_t trx_expr = 3600;
   uint32_t gen_duration = 60;
   uint32_t target_tps = 1;
   string lib_id_str;
   int64_t spinup_time_us = 1000000;
   uint32_t max_lag_per = 5;
   int64_t max_lag_duration_us = 1000000;
   string log_dir_in;
   bool stop_on_trx_failed;
   std::string peer_endpoint = "127.0.0.1";
   unsigned short port = 9876;

   bool transaction_specified = false;
   std::string abi_file_path_in;
   std::string actions_data_json_file_or_str;
   std::string actions_auths_json_file_or_str;

   vector<string> account_str_vector;
   vector<string> private_keys_str_vector;


   cli.add_options()
         ("generator-id", bpo::value<uint16_t>(&gen_id)->default_value(0), "Id for the transaction generator. Allowed range (0-960). Defaults to 0.")
         ("chain-id", bpo::value<string>(&chain_id_in), "set the chain id")
         ("contract-owner-account", bpo::value<string>(&contract_owner_acct), "Account name of the contract account for the transaction actions")
         ("accounts", bpo::value<string>(&accts), "comma-separated list of accounts that will be used for transfers. Minimum required accounts: 2.")
         ("priv-keys", bpo::value<string>(&p_keys), "comma-separated list of private keys in same order of accounts list that will be used to sign transactions. Minimum required: 2.")
         ("trx-expiration", bpo::value<int64_t>(&trx_expr)->default_value(3600), "transaction expiration time in seconds. Defaults to 3,600. Maximum allowed: 3,600")
         ("trx-gen-duration", bpo::value<uint32_t>(&gen_duration)->default_value(60), "Transaction generation duration (seconds). Defaults to 60 seconds.")
         ("target-tps", bpo::value<uint32_t>(&target_tps)->default_value(1), "Target transactions per second to generate/send. Defaults to 1 transaction per second.")
         ("last-irreversible-block-id", bpo::value<string>(&lib_id_str), "Current last-irreversible-block-id (LIB ID) to use for transactions.")
         ("monitor-spinup-time-us", bpo::value<int64_t>(&spinup_time_us)->default_value(1000000), "Number of microseconds to wait before monitoring TPS. Defaults to 1000000 (1s).")
         ("monitor-max-lag-percent", bpo::value<uint32_t>(&max_lag_per)->default_value(5), "Max percentage off from expected transactions sent before being in violation. Defaults to 5.")
         ("monitor-max-lag-duration-us", bpo::value<int64_t>(&max_lag_duration_us)->default_value(1000000), "Max microseconds that transaction generation can be in violation before quitting. Defaults to 1000000 (1s).")
         ("log-dir", bpo::value<string>(&log_dir_in), "set the logs directory")
         ("abi-file", bpo::value<string>(&abi_file_path_in), "The path to the contract abi file to use for the supplied transaction action data")
         ("actions-data", bpo::value<string>(&actions_data_json_file_or_str), "The json actions data file or json actions data description string to use")
         ("actions-auths", bpo::value<string>(&actions_auths_json_file_or_str), "The json actions auth file or json actions auths description string to use, containting authAcctName to activePrivateKey pairs.")
         ("peer-endpoint", bpo::value<string>(&peer_endpoint)->default_value("127.0.0.1"), "set the peer endpoint to send transactions to")
         ("port", bpo::value<uint16_t>(&port)->default_value(9876), "set the peer endpoint port to send transactions to")
         ("help,h", "print this list")
         ;

   try {
      bpo::store(bpo::parse_command_line(argc, argv, cli), vmap);
      bpo::notify(vmap);

      if(vmap.count("help") > 0) {
         cli.print(std::cerr);
         return SUCCESS;
      }

      if((vmap.count("abi-file") || vmap.count("actions-data") || vmap.count("actions-auths")) &&
        !(vmap.count("abi-file") && vmap.count("actions-data") && vmap.count("actions-auths"))) {
         ilog("Initialization error: If using abi-file, actions-data, and actions-auths to specify a transaction type to generate, must provide all inputs.");
         cli.print(std::cerr);
         return INITIALIZE_FAIL;
      }

      if(vmap.count("abi-file") && vmap.count("actions-data") && vmap.count("actions-auths")) {
         ilog("Specifying transaction to generate directly using abi-file, actions-data, and actions-auths.");
         transaction_specified = true;
      }

      if(chain_id_in.empty()) {
         ilog("Initialization error: missing chain-id");
         cli.print(std::cerr);
         return INITIALIZE_FAIL;
      }

      if(log_dir_in.empty()) {
         ilog("Initialization error: missing log-dir");
         cli.print(std::cerr);
         return INITIALIZE_FAIL;
      }

      if(lib_id_str.empty()) {
         ilog("Initialization error: missing last-irreversible-block-id");
         cli.print(std::cerr);
         return INITIALIZE_FAIL;
      }

      if(contract_owner_acct.empty()) {
         ilog("Initialization error: missing contract-owner-account");
         cli.print(std::cerr);
         return INITIALIZE_FAIL;
      }

      boost::split(account_str_vector, accts, boost::is_any_of(","));
      if(account_str_vector.size() < 1 || (!transaction_specified && account_str_vector.size() < 2)) {
         ilog("Initialization error: did not specify transfer accounts. Auto transfer transaction generation requires at minimum 2 transfer accounts, while providing transaction action data requires at least one.");
         cli.print(std::cerr);
         return INITIALIZE_FAIL;
      }

      boost::split(private_keys_str_vector, p_keys, boost::is_any_of(","));
      if(private_keys_str_vector.size() < 1 || (!transaction_specified && private_keys_str_vector.size() < 2)) {
         ilog("Initialization error: did not specify accounts' private keys. Auto transfer transaction generation requires at minimum 2 private keys, while providing transaction action data requires at least one.");
         cli.print(std::cerr);
         return INITIALIZE_FAIL;
      }

      if(gen_id > generator_id_max) {
         ilog("Initialization error: Exceeded max value for generator id. Value must be less than ${max}.", ("max", generator_id_max));
         cli.print(std::cerr);
         return INITIALIZE_FAIL;
      }

      if(trx_expr > trx_expiration_max) {
         ilog("Initialization error: Exceeded max value for transaction expiration. Value must be less than ${max}.", ("max", trx_expiration_max));
         cli.print(std::cerr);
         return INITIALIZE_FAIL;
      }

      if(spinup_time_us < 0) {
         ilog("Initialization error: spinup-time-us cannot be negative");
         cli.print(std::cerr);
         return INITIALIZE_FAIL;
      }

      if(max_lag_duration_us < 0) {
         ilog("Initialization error: max-lag-duration-us cannot be negative");
         cli.print(std::cerr);
         return INITIALIZE_FAIL;
      }

      if(max_lag_per > 100) {
         ilog("Initialization error: max-lag-percent must be between 0 and 100");
         cli.print(std::cerr);
         return INITIALIZE_FAIL;
      }
   } catch(bpo::unknown_option& ex) {
      std::cerr << ex.what() << std::endl;
      cli.print(std::cerr);
      return INITIALIZE_FAIL;
   }

   ilog("Initial generator id ${id}", ("id", gen_id));
   ilog("Initial chain id ${chainId}", ("chainId", chain_id_in));
   ilog("Contract owner account ${acct}", ("acct", contract_owner_acct));
   ilog("Transfer accounts ${accts}", ("accts", accts));
   ilog("Account private keys ${priv_keys}", ("priv_keys", p_keys));
   ilog("Transaction expiration seconds ${expr}", ("expr", trx_expr));
   ilog("Reference LIB block id ${LIB}", ("LIB", lib_id_str));
   ilog("Transaction Generation Duration (sec) ${dur}", ("dur", gen_duration));
   ilog("Target generation Transaction Per Second (TPS) ${tps}", ("tps", target_tps));
   ilog("Logs directory ${logDir}", ("logDir", log_dir_in));
   ilog("Peer Endpoint ${peer-endpoint}:${peer-port}", ("peer-endpoint", peer_endpoint)("peer-port", port));

   if (transaction_specified) {
      ilog("User Transaction Specified: Abi File ${abi}", ("abi", abi_file_path_in));
      ilog("User Transaction Specified: Actions Data ${acts}", ("acts", actions_data_json_file_or_str));
      ilog("User Transaction Specified: Actions Auths ${auths}", ("auths", actions_auths_json_file_or_str));
   }

   fc::microseconds trx_expr_ms = fc::seconds(trx_expr);

   std::shared_ptr<tps_performance_monitor> monitor;
   if (transaction_specified) {
      auto generator = std::make_shared<trx_generator>(gen_id, chain_id_in, abi_file_path_in, contract_owner_acct,
                                                       actions_data_json_file_or_str, actions_auths_json_file_or_str,
                                                       trx_expr_ms,  lib_id_str, log_dir_in, stop_on_trx_failed, peer_endpoint, port);
      monitor = std::make_shared<tps_performance_monitor>(spinup_time_us, max_lag_per, max_lag_duration_us);
      trx_tps_tester<trx_generator, tps_performance_monitor> tester{generator, monitor, gen_duration, target_tps};

      if (!tester.run()) {
         return OTHER_FAIL;
      }
   } else {
      auto generator = std::make_shared<transfer_trx_generator>(gen_id, chain_id_in, contract_owner_acct, account_str_vector, trx_expr_ms, private_keys_str_vector,
                                                                lib_id_str, log_dir_in, stop_on_trx_failed, peer_endpoint, port);

      monitor = std::make_shared<tps_performance_monitor>(spinup_time_us, max_lag_per, max_lag_duration_us);
      trx_tps_tester<transfer_trx_generator, tps_performance_monitor> tester{generator, monitor, gen_duration, target_tps};

      if (!tester.run()) {
         return OTHER_FAIL;
      }
   }

   if (monitor->terminated_early()) {
      return TERMINATED_EARLY;
   }
   
   return SUCCESS;

}

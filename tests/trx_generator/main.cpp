#include <eosio/chain_plugin/chain_plugin.hpp>
#include <trx_provider.hpp>
#include <trx_generator.hpp>
#include <boost/algorithm/string.hpp>
#include <fc/bitutil.hpp>
#include <fc/io/raw.hpp>
#include <contracts.hpp>
#include <iostream>

namespace bpo = boost::program_options;
namespace et = eosio::testing;

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
   et::provider_base_config provider_config;
   et::trx_generator_base_config trx_gen_base_config;
   et::user_specified_trx_config user_trx_config;
   et::accounts_config accts_config;
   et::trx_tps_tester_config tester_config;

   const int64_t trx_expiration_max = 3600;
   const uint16_t generator_id_max = 960;
   bpo::variables_map vmap;
   bpo::options_description cli("Transaction Generator command line options.");
   std::string chain_id_in;
   std::string contract_owner_account_in;
   std::string lib_id_str;
   std::string accts;
   std::string p_keys;
   int64_t spinup_time_us = 1000000;
   uint32_t max_lag_per = 5;
   int64_t max_lag_duration_us = 1000000;
   int64_t trx_expr = 3600;

   bool transaction_specified = false;

   cli.add_options()
         ("generator-id", bpo::value<uint16_t>(&trx_gen_base_config._generator_id)->default_value(0), "Id for the transaction generator. Allowed range (0-960). Defaults to 0.")
         ("chain-id", bpo::value<std::string>(&chain_id_in), "set the chain id")
         ("contract-owner-account", bpo::value<std::string>(&contract_owner_account_in), "Account name of the contract account for the transaction actions")
         ("accounts", bpo::value<std::string>(&accts), "comma-separated list of accounts that will be used for transfers. Minimum required accounts: 2.")
         ("priv-keys", bpo::value<std::string>(&p_keys), "comma-separated list of private keys in same order of accounts list that will be used to sign transactions. Minimum required: 2.")
         ("trx-expiration", bpo::value<int64_t>(&trx_expr)->default_value(3600), "transaction expiration time in seconds. Defaults to 3,600. Maximum allowed: 3,600")
         ("trx-gen-duration", bpo::value<uint32_t>(&tester_config._gen_duration_seconds)->default_value(60), "Transaction generation duration (seconds). Defaults to 60 seconds.")
         ("target-tps", bpo::value<uint32_t>(&tester_config._target_tps)->default_value(1), "Target transactions per second to generate/send. Defaults to 1 transaction per second.")
         ("last-irreversible-block-id", bpo::value<std::string>(&lib_id_str), "Current last-irreversible-block-id (LIB ID) to use for transactions.")
         ("monitor-spinup-time-us", bpo::value<int64_t>(&spinup_time_us)->default_value(1000000), "Number of microseconds to wait before monitoring TPS. Defaults to 1000000 (1s).")
         ("monitor-max-lag-percent", bpo::value<uint32_t>(&max_lag_per)->default_value(5), "Max percentage off from expected transactions sent before being in violation. Defaults to 5.")
         ("monitor-max-lag-duration-us", bpo::value<int64_t>(&max_lag_duration_us)->default_value(1000000), "Max microseconds that transaction generation can be in violation before quitting. Defaults to 1000000 (1s).")
         ("log-dir", bpo::value<std::string>(&trx_gen_base_config._log_dir), "set the logs directory")
         ("stop-on-trx-failed", bpo::value<bool>(&trx_gen_base_config._stop_on_trx_failed)->default_value(true), "stop transaction generation if sending fails.")
         ("abi-file", bpo::value<std::string>(&user_trx_config._abi_data_file_path), "The path to the contract abi file to use for the supplied transaction action data")
         ("actions-data", bpo::value<std::string>(&user_trx_config._actions_data_json_file_or_str), "The json actions data file or json actions data description string to use")
         ("actions-auths", bpo::value<std::string>(&user_trx_config._actions_auths_json_file_or_str), "The json actions auth file or json actions auths description string to use, containting authAcctName to activePrivateKey pairs.")
         ("peer-endpoint", bpo::value<std::string>(&provider_config._peer_endpoint)->default_value("127.0.0.1"), "set the peer endpoint to send transactions to")
         ("port", bpo::value<uint16_t>(&provider_config._port)->default_value(9876), "set the peer endpoint port to send transactions to")
         ("help,h", "print this list")
         ;

   try {
      bpo::store(bpo::parse_command_line(argc, argv, cli), vmap);
      bpo::notify(vmap);

      if(vmap.count("help") > 0) {
         cli.print(std::cerr);
         return SUCCESS;
      }

      if(user_trx_config.fully_configured()) {
         ilog("Specifying transaction to generate directly using abi-file, actions-data, and actions-auths.");
         transaction_specified = true;
      } else if(user_trx_config.partially_configured()) {
         ilog("Initialization error: If using abi-file, actions-data, and actions-auths to specify a transaction type to generate, must provide all inputs.");
         cli.print(std::cerr);
         return INITIALIZE_FAIL;
      }

      if(chain_id_in.empty()) {
         ilog("Initialization error: missing chain-id");
         cli.print(std::cerr);
         return INITIALIZE_FAIL;
      } else {
         trx_gen_base_config._chain_id = eosio::chain::chain_id_type(chain_id_in);
      }

      if(trx_gen_base_config._log_dir.empty()) {
         ilog("Initialization error: missing log-dir");
         cli.print(std::cerr);
         return INITIALIZE_FAIL;
      }

      if(lib_id_str.empty()) {
         ilog("Initialization error: missing last-irreversible-block-id");
         cli.print(std::cerr);
         return INITIALIZE_FAIL;
      } else {
         trx_gen_base_config._last_irr_block_id = fc::variant(lib_id_str).as<eosio::chain::block_id_type>();
      }

      if(contract_owner_account_in.empty()) {
         ilog("Initialization error: missing contract-owner-account");
         cli.print(std::cerr);
         return INITIALIZE_FAIL;
      } else {
         trx_gen_base_config._contract_owner_account = eosio::chain::name(contract_owner_account_in);
      }

      std::vector<std::string> account_str_vector;
      boost::split(account_str_vector, accts, boost::is_any_of(","));
      if(!transaction_specified && account_str_vector.size() < 2) {
         ilog("Initialization error: did not specify transfer accounts. Auto transfer transaction generation requires at minimum 2 transfer accounts.");
         cli.print(std::cerr);
         return INITIALIZE_FAIL;
      } else if (!accts.empty() && !account_str_vector.empty()) {
         for(const std::string& account_name: account_str_vector) {
            ilog("Initializing accounts. Attempt to create name for ${acct}", ("acct", account_name));
            accts_config._acct_name_vec.emplace_back(account_name);
         }
      }

      std::vector<std::string> private_keys_str_vector;
      boost::split(private_keys_str_vector, p_keys, boost::is_any_of(","));
      if(!transaction_specified && private_keys_str_vector.size() < 2) {
         ilog("Initialization error: did not specify accounts' private keys. Auto transfer transaction generation requires at minimum 2 private keys.");
         cli.print(std::cerr);
         return INITIALIZE_FAIL;
      } else if (!p_keys.empty() && !private_keys_str_vector.empty()) {
         for(const std::string& private_key: private_keys_str_vector) {
            ilog("Initializing private keys. Attempt to create private_key for ${key} : gen key ${newKey}", ("key", private_key)("newKey", fc::crypto::private_key(private_key)));
            accts_config._priv_keys_vec.emplace_back(private_key);
         }
      }

      if(trx_gen_base_config._generator_id > generator_id_max) {
         ilog("Initialization error: Exceeded max value for generator id. Value must be less than ${max}.", ("max", generator_id_max));
         cli.print(std::cerr);
         return INITIALIZE_FAIL;
      }

      if(trx_expr > trx_expiration_max) {
         ilog("Initialization error: Exceeded max value for transaction expiration. Value must be less than ${max}.", ("max", trx_expiration_max));
         cli.print(std::cerr);
         return INITIALIZE_FAIL;
      } else {
         trx_gen_base_config._trx_expiration_us = fc::seconds(trx_expr);
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

   ilog("Initial Trx Generator config: ${config}", ("config", trx_gen_base_config.to_string()));
   ilog("Initial Provider config: ${config}", ("config", provider_config.to_string()));
   ilog("Initial Accounts config: ${config}", ("config", accts_config.to_string()));
   ilog("Transaction TPS Tester config: ${config}", ("config", tester_config.to_string()));

   if (transaction_specified) {
      ilog("User Transaction Specified: ${config}", ("config", user_trx_config.to_string()));
   }

   std::shared_ptr<et::tps_performance_monitor> monitor;
   if (transaction_specified) {
      auto generator = std::make_shared<et::trx_generator>(trx_gen_base_config, provider_config, user_trx_config);

      monitor = std::make_shared<et::tps_performance_monitor>(spinup_time_us, max_lag_per, max_lag_duration_us);
      et::trx_tps_tester<et::trx_generator, et::tps_performance_monitor> tester{generator, monitor, tester_config};

      if (!tester.run()) {
         return OTHER_FAIL;
      }
   } else {
      auto generator = std::make_shared<et::transfer_trx_generator>(trx_gen_base_config, provider_config, accts_config);

      monitor = std::make_shared<et::tps_performance_monitor>(spinup_time_us, max_lag_per, max_lag_duration_us);
      et::trx_tps_tester<et::transfer_trx_generator, et::tps_performance_monitor> tester{generator, monitor, tester_config};

      if (!tester.run()) {
         return OTHER_FAIL;
      }
   }

   if (monitor->terminated_early()) {
      return TERMINATED_EARLY;
   }
   
   return SUCCESS;

}

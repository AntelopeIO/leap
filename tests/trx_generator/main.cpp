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
   const int64_t TRX_EXPIRATION_MAX = 3600;
   variables_map vmap;
   options_description cli("Transaction Generator command line options.");
   string chain_id_in;
   string h_acct;
   string accts;
   string p_keys;
   int64_t trx_expr;
   uint32_t gen_duration;
   uint32_t target_tps;
   string lib_id_str;
   int64_t spinup_time_us;
   uint32_t max_lag_per;
   int64_t max_lag_duration_us;
   string log_dir_in;
   bool stop_on_trx_failed;
   std::string peer_endpoint;
   unsigned short port;


   vector<string> account_str_vector;
   vector<string> private_keys_str_vector;


   cli.add_options()
         ("chain-id", bpo::value<string>(&chain_id_in), "set the chain id")
         ("handler-account", bpo::value<string>(&h_acct), "Account name of the handler account for the transfer actions")
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
         ("stop-on-trx-failed", bpo::value<bool>(&stop_on_trx_failed)->default_value(true), "stop transaction generation if sending fails.")
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

      if(!vmap.count("chain-id")) {
         ilog("Initialization error: missing chain-id");
         cli.print(std::cerr);
         return INITIALIZE_FAIL;
      }

      if(!vmap.count("log-dir")) {
         ilog("Initialization error: missing log-dir");
         cli.print(std::cerr);
         return INITIALIZE_FAIL;
      }

      if(!vmap.count("last-irreversible-block-id")) {
         ilog("Initialization error: missing last-irreversible-block-id");
         cli.print(std::cerr);
         return INITIALIZE_FAIL;
      }

      if(vmap.count("handler-account")) {
      } else {
         ilog("Initialization error: missing handler-account");
         cli.print(std::cerr);
         return INITIALIZE_FAIL;
      }

      if(vmap.count("accounts")) {
         boost::split(account_str_vector, accts, boost::is_any_of(","));
         if(account_str_vector.size() < 2) {
            ilog("Initialization error: requires at minimum 2 transfer accounts");
            cli.print(std::cerr);
            return INITIALIZE_FAIL;
         }
      } else {
         ilog("Initialization error: did not specify transfer accounts. requires at minimum 2 transfer accounts");
         cli.print(std::cerr);
         return INITIALIZE_FAIL;
      }

      if(vmap.count("priv-keys")) {
         boost::split(private_keys_str_vector, p_keys, boost::is_any_of(","));
         if(private_keys_str_vector.size() < 2) {
            ilog("Initialization error: requires at minimum 2 private keys");
            cli.print(std::cerr);
            return INITIALIZE_FAIL;
         }
      } else {
         ilog("Initialization error: did not specify accounts' private keys. requires at minimum 2 private keys");
         cli.print(std::cerr);
         return INITIALIZE_FAIL;
      }

      if(vmap.count("trx-expiration")) {
         if(trx_expr > TRX_EXPIRATION_MAX) {
            ilog("Initialization error: Exceeded max value for transaction expiration. Value must be less than ${max}.", ("max", TRX_EXPIRATION_MAX));
            cli.print(std::cerr);
            return INITIALIZE_FAIL;
         }
      }

      if(vmap.count("spinup-time-us")) {
         if(spinup_time_us < 0) {
            ilog("Initialization error: spinup-time-us cannot be negative");
            cli.print(std::cerr);
            return INITIALIZE_FAIL;
         }
      }

      if(vmap.count("max-lag-duration-us")) {
         if(max_lag_duration_us < 0) {
            ilog("Initialization error: max-lag-duration-us cannot be negative");
            cli.print(std::cerr);
            return INITIALIZE_FAIL;
         }
      }

      if(vmap.count("max-lag-percent")) {
         if(max_lag_per > 100) {
            ilog("Initialization error: max-lag-percent must be between 0 and 100");
            cli.print(std::cerr);
            return INITIALIZE_FAIL;
         }
      }
   } catch(bpo::unknown_option& ex) {
      std::cerr << ex.what() << std::endl;
      cli.print(std::cerr);
      return INITIALIZE_FAIL;
   }

   ilog("Initial chain id ${chainId}", ("chainId", chain_id_in));
   ilog("Handler account ${acct}", ("acct", h_acct));
   ilog("Transfer accounts ${accts}", ("accts", accts));
   ilog("Account private keys ${priv_keys}", ("priv_keys", p_keys));
   ilog("Transaction expiration microsections ${expr}", ("expr", trx_expr));
   ilog("Reference LIB block id ${LIB}", ("LIB", lib_id_str));
   ilog("Transaction Generation Duration (sec) ${dur}", ("dur", gen_duration));
   ilog("Target generation Transaction Per Second (TPS) ${tps}", ("tps", target_tps));
   ilog("Logs directory ${logDir}", ("logDir", log_dir_in));
   ilog("Peer Endpoint ${peer-endpoint}:${peer-port}", ("peer-endpoint", peer_endpoint)("peer-port", port));

   auto generator = std::make_shared<transfer_trx_generator>(chain_id_in, h_acct, account_str_vector, trx_expr, private_keys_str_vector,
                                                             lib_id_str, log_dir_in, stop_on_trx_failed, peer_endpoint, port);
   std::shared_ptr<tps_performance_monitor> monitor = std::make_shared<tps_performance_monitor>(spinup_time_us, max_lag_per, max_lag_duration_us);

   trx_tps_tester<transfer_trx_generator, tps_performance_monitor> tester{generator, monitor, gen_duration, target_tps};

   if (!tester.run()) {
      return OTHER_FAIL;
   }

   if (monitor->terminated_early()) {
      return TERMINATED_EARLY;
   }
   
   return SUCCESS;

}

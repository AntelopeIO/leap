#include <eosio/wallet_plugin/wallet_plugin.hpp>
#include <eosio/wallet_plugin/wallet_manager.hpp>
#include <eosio/chain/exceptions.hpp>
#include <chrono>

#include <fc/io/json.hpp>

namespace fc { class variant; }

namespace eosio {

static auto _wallet_plugin = application::register_plugin<wallet_plugin>();

wallet_plugin::wallet_plugin() {}

wallet_manager& wallet_plugin::get_wallet_manager() {
   return *wallet_manager_ptr;
}

void wallet_plugin::set_program_options(options_description& cli, options_description& cfg) {
   cfg.add_options()
         ("wallet-dir", bpo::value<std::filesystem::path>()->default_value("."),
          "The path of the wallet files (absolute path or relative to application data dir)")
         ("unlock-timeout", bpo::value<int64_t>()->default_value(900),
          "Timeout for unlocked wallet in seconds (default 900 (15 minutes)). "
          "Wallets will automatically lock after specified number of seconds of inactivity. "
          "Activity is defined as any wallet command e.g. list-wallets.")
         ;
}

void wallet_plugin::plugin_initialize(const variables_map& options) {
   ilog("initializing wallet plugin");
   try {
      wallet_manager_ptr = std::make_unique<wallet_manager>();

      if (options.count("wallet-dir")) {
         auto dir = options.at("wallet-dir").as<std::filesystem::path>();
         if (dir.is_relative())
            dir = app().data_dir() / dir;
         if( !std::filesystem::exists(dir) )
            std::filesystem::create_directories(dir);
         wallet_manager_ptr->set_dir(dir);
      }
      if (options.count("unlock-timeout")) {
         auto timeout = options.at("unlock-timeout").as<int64_t>();
         EOS_ASSERT(timeout > 0, chain::invalid_lock_timeout_exception, "Please specify a positive timeout ${t}", ("t", timeout));
         std::chrono::seconds t(timeout);
         wallet_manager_ptr->set_timeout(t);
      }
   } FC_LOG_AND_RETHROW()
}

} // namespace eosio

#include "chain.hpp"
#include <memory>

#include <fc/bitutil.hpp>
#include <fc/filesystem.hpp>
#include <fc/io/json.hpp>
#include <fc/variant.hpp>

#include <boost/exception/diagnostic_information.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/path.hpp>

#include <eosio/chain/authorization_manager.hpp>
#include <eosio/chain/block_log.hpp>
#include <eosio/chain/code_object.hpp>
#include <eosio/chain/config.hpp>
#include <eosio/chain/controller.hpp>
#include <eosio/chain/deep_mind.hpp>
#include <eosio/chain/eosio_contract.hpp>
#include <eosio/chain/exceptions.hpp>
#include <eosio/chain/fork_database.hpp>
#include <eosio/chain/generated_transaction_object.hpp>
#include <eosio/chain/global_property_object.hpp>
#include <eosio/chain/permission_link_object.hpp>
#include <eosio/chain/resource_limits.hpp>
#include <eosio/chain/snapshot.hpp>
#include <eosio/chain/wasm_interface.hpp>
#include <eosio/chain_plugin/chain_plugin.hpp>
#include <eosio/chain_plugin/trx_finality_status_processing.hpp>
#include <eosio/chain_plugin/trx_retry_db.hpp>
#include <eosio/producer_plugin/producer_plugin.hpp>
#include <fc/io/json.hpp>

#include <eosio/resource_monitor_plugin/resource_monitor_plugin.hpp>

#include <chainbase/environment.hpp>
#include <eosio/chain/exceptions.hpp>


#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/signals2/connection.hpp>

namespace bfs = boost::filesystem;
using namespace eosio;
using namespace eosio::chain;


// reflect chainbase::environment for --print-build-info option
FC_REFLECT_ENUM(chainbase::environment::os_t,
                (OS_LINUX) (OS_MACOS) (OS_WINDOWS) (OS_OTHER))
FC_REFLECT_ENUM(chainbase::environment::arch_t,
                (ARCH_X86_64) (ARCH_ARM) (ARCH_RISCV) (ARCH_OTHER))
FC_REFLECT(chainbase::environment, (debug) (os) (arch) (boost_version) (compiler))


void chain_actions::setup(CLI::App& app) {
   auto* sub = app.add_subcommand("chain-state", "chain utility");

   auto* genesis = sub->add_subcommand("genesis-json", "extract genesis_state from blocks.log as JSON");
   genesis->add_option("--output-file,-o", opt->genesis_output_file, "write into specified file")->capture_default_str();
   genesis->add_flag("--print,-p", opt->genesis_just_print, "print to console");

   auto* build = sub->add_subcommand("build-info", "extract build environment information as JSON");
   build->add_option("--output-file,-o", opt->build_output_file, "write into specified file")->capture_default_str();
   build->add_flag("--print,-p", opt->build_just_print, "print to console");

   // callbacks
   genesis->callback([&]() {
      int rc = run_subcommand_genesis();
      // properly return err code in main
      if(rc) throw(CLI::RuntimeError(rc));
   });

   build->callback([&]() {
      int rc = run_subcommand_build();
      // properly return err code in main
      if(rc) throw(CLI::RuntimeError(rc));
   });
}

int chain_actions::run_subcommand_build() {
   if(!opt->build_output_file.empty()) {
      bfs::path p = opt->build_output_file;
      if(p.is_relative()) {
         p = bfs::current_path() / p;
      }
      fc::json::save_to_file(chainbase::environment(), p, true);
      ilog("Saved build info JSON to '${path}'", ("path", p.generic_string()));
   }
   if(opt->build_just_print) {
      ilog("\nBuild environment JSON:\n${e}", ("e", fc::json::to_pretty_string(chainbase::environment())));
   }

   return 0;
}

int chain_actions::run_subcommand_genesis() {
   return 0;
}
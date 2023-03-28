#include "chain.hpp"
#include <memory>

#include <fc/bitutil.hpp>
#include <fc/filesystem.hpp>
#include <fc/io/json.hpp>
#include <fc/variant.hpp>

#include <boost/exception/diagnostic_information.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/path.hpp>

#include <eosio/chain/block_log.hpp>
#include <eosio/chain/exceptions.hpp>
#include <chainbase/environment.hpp>

#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/lexical_cast.hpp>

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
   sub->require_subcommand();
   sub->fallthrough();

   auto* build = sub->add_subcommand("build-info", "extract build environment information as JSON");
   build->add_option("--output-file,-o", opt->build_output_file, "write into specified file")->capture_default_str();
   build->add_flag("--print,-p", opt->build_just_print, "print to console");
   build->require_option(1);

   build->callback([&]() {
      int rc = run_subcommand_build();
      // properly return err code in main
      if(rc) throw(CLI::RuntimeError(rc));
   });
}

int chain_actions::run_subcommand_build() {
   if(!opt->build_output_file.empty()) {
      std::filesystem::path p = opt->build_output_file;
      if(p.is_relative()) {
         p = std::filesystem::current_path() / p;
      }
      fc::json::save_to_file(chainbase::environment(), p, true);
      std::cout << "Saved build info JSON to '" <<  p.generic_string() << "'" << std::endl;
   }
   if(opt->build_just_print) {
      std::cout << fc::json::to_pretty_string(chainbase::environment()) << std::endl;
   }

   return 0;
}
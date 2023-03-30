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
   sub->add_option("--state-dir", opt->sstate_state_dir, "The location of the state directory (absolute path or relative to the current directory)")->capture_default_str();
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

  sub->add_subcommand("last-shutdown-state", "indicate whether last shutdown was clean or not")->callback([&]() {
      int rc = run_subcommand_sstate();
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
      std::cout << "Saved build info JSON to '" <<  p.generic_string() << "'" << std::endl;
   }
   if(opt->build_just_print) {
      std::cout << fc::json::to_pretty_string(chainbase::environment()) << std::endl;
   }

   return 0;
}

int chain_actions::run_subcommand_sstate() {
   bfs::path state_dir = "";

   // default state dir, if none specified
   if(opt->sstate_state_dir.empty()) {
      auto root = fc::app_path();
      auto default_data_dir = root / "eosio" / "nodeos" / "data" ;
      state_dir  = default_data_dir / config::default_state_dir_name;
   }
   else {
      // adjust if path relative
      state_dir = opt->sstate_state_dir;
      if(state_dir.is_relative()) {
         state_dir = bfs::current_path() / state_dir;
      }
   }
   
   auto shared_mem_path = state_dir / "shared_memory.bin";

   if(!bfs::exists(shared_mem_path)) {
      std::cerr << "Unable to read database status: file not found: " << shared_mem_path << std::endl;
      return -1;
   }

   char header[chainbase::header_size];
   std::ifstream hs(shared_mem_path.generic_string(), std::ifstream::binary);
   hs.read(header, chainbase::header_size);
   if(hs.fail()) {
      std::cerr << "Unable to read database status: file invalid or corrupt" << shared_mem_path <<  std::endl;
      return -1;
   }

   chainbase::db_header* dbheader = reinterpret_cast<chainbase::db_header*>(header);
   if(dbheader->id != chainbase::header_id) {
      std::string what_str("\"" + state_dir.generic_string() + "\" database format not compatible with this version of chainbase.");
      std::cerr << what_str << std::endl;
      return -1;
   }
   if(dbheader->dirty) {
      std::cout << "Database dirty flag is set, shutdown was not clean" << std::endl;
      return -1;
   }

   std::cout << "Database state is clean" << std::endl;
   return 0;
}
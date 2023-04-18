#include "snapshot.hpp"
#include <eosio/chain/abi_serializer.hpp>
#include <eosio/chain/block_log.hpp>
#include <eosio/chain/config.hpp>
#include <eosio/chain/controller.hpp>
#include <eosio/chain/fork_database.hpp>

#include <memory>

#include <fc/bitutil.hpp>
#include <fc/filesystem.hpp>
#include <fc/io/json.hpp>
#include <fc/variant.hpp>

#include <boost/exception/diagnostic_information.hpp>

using namespace eosio;
using namespace eosio::chain;


void snapshot_actions::setup(CLI::App& app) {
   auto* sub = app.add_subcommand("snapshot", "Snapshot utility");
   sub->require_subcommand();
   sub->fallthrough();

   // subcommand -convert snapshot to json
   auto to_json = sub->add_subcommand("to-json", "Convert snapshot file to json format");
   to_json->add_option("--input-file,-i", opt->input_file, "Snapshot file to convert to json format, writes to <file>.json if output file not specified (tmp state dir used).")->required();
   to_json->add_option("--output-file,-o", opt->output_file, "The file to write the output to (absolute or relative path).  If not specified then output is to <input-file>.json.");
   to_json->add_option("--chain-id", opt->chain_id, "Specify a chain id in case it is not included in a snapshot or you want to override it.");
   to_json->add_option("--db-size", opt->db_size, "Maximum size (in MiB) of the chain state database")->capture_default_str();

   to_json->callback([this]() {
      try {
         int rc = run_subcommand();
         if(rc) throw(CLI::RuntimeError(rc));
      } catch(...) {
         print_exception();
         throw(CLI::RuntimeError(-1));
      }
   });
}

int snapshot_actions::run_subcommand() {
   if(!opt->input_file.empty()) {
      if(!std::filesystem::exists(opt->input_file)) {
         std::cerr << "cannot load snapshot, " << opt->input_file
                   << " does not exist" << std::endl;
         return -1;
      }
   }

   std::filesystem::path snapshot_path = opt->input_file;
   std::filesystem::path json_path = opt->output_file.empty()
                               ? snapshot_path.generic_string() + ".json"
                               : opt->output_file;
   // determine chain id
   auto chain_id = chain_id_type("");
   if(!opt->chain_id.empty()) { // override it
      chain_id = chain_id_type(opt->chain_id);
   }
   else { // try to retrieve it
      auto infile = std::ifstream(snapshot_path.generic_string(),
                               (std::ios::in | std::ios::binary));
      istream_snapshot_reader reader(infile);
      reader.validate();
      chain_id = controller::extract_chain_id(reader);
      infile.close();
   }

   // setup controller
   fc::temp_directory dir;
   const auto& temp_dir = dir.path();
   std::filesystem::path state_dir = temp_dir / "state";
   std::filesystem::path blocks_dir = temp_dir / "blocks";
   std::unique_ptr<controller> control;
   controller::config cfg;
   cfg.blocks_dir = blocks_dir;
   cfg.state_dir = state_dir;
   cfg.state_size = opt->db_size * 1024 * 1024;
   cfg.state_guard_size = opt->guard_size * 1024 * 1024;
   protocol_feature_set pfs = initialize_protocol_features( std::filesystem::path("protocol_features"), false );

   try {
      auto infile = std::ifstream(snapshot_path.generic_string(),
                                  (std::ios::in | std::ios::binary));
      auto reader = std::make_shared<istream_snapshot_reader>(infile);

      auto check_shutdown = []() { return false; };
      auto shutdown = []() { throw; };

      control.reset(new controller(cfg, std::move(pfs), chain_id));
      control->add_indices();
      control->startup(shutdown, check_shutdown, reader);
      infile.close();

      ilog("Writing snapshot: ${s}", ("s", json_path));
      auto snap_out = std::ofstream(json_path.generic_string(), (std::ios::out));
      auto writer = std::make_shared<ostream_json_snapshot_writer>(snap_out);
      control->write_snapshot(writer);
      writer->finalize();
      snap_out.flush();
      snap_out.close();
   } catch(const database_guard_exception& e) {
      std::cerr << "Database is not configured to have enough storage to handle provided snapshot, please increase storage and try aagain" << std::endl;
      control.reset();
      throw;
   }

   ilog("Completed writing snapshot: ${s}", ("s", json_path));
   return 0;
}

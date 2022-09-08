#include "actions_snapshot.hpp"
#include <memory>

#include <fc/bitutil.hpp>
#include <fc/filesystem.hpp>
#include <fc/io/json.hpp>
#include <fc/variant.hpp>

#include <boost/exception/diagnostic_information.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/path.hpp>

void SnapshotActions::setup(CLI::App& app) {
   auto* sub = app.add_subcommand("snapshot", "Snapshot utility");
   sub->add_subcommand("to-json", "Convert snapshot file to convert to JSON format");

   // options
   sub->add_option("--input-file,-i", opt->input_file, "Snapshot file to convert to JSON format, writes to <file>.json if output file not specified (tmp state dir used), and exit.")->required();
   sub->add_option("--output-file,-o", opt->output_file, "The file to write the output to (absolute or relative path).  If not specified then output is to stdout.");

   sub->callback([this]() { run_subcommand(); });
}


int SnapshotActions::run_subcommand() {
   if(!opt->input_file.empty()) {
      if(!fc::exists(opt->input_file)) {
         std::cerr << "Cannot load snapshot, " << opt->input_file << " does not exist" << std::endl;
         return -1;
      }
   }

   // recover genesis information from the snapshot
   // used for validation code below
   /*
         auto infile = std::ifstream(my->snapshot_path->generic_string(), (std::ios::in | std::ios::binary));
         istream_snapshot_reader reader(infile);
         reader.validate();
         chain_id = controller::extract_chain_id(reader);
         infile.close();

         boost::filesystem::path temp_dir = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();
         my->chain_config->state_dir = temp_dir / "state";
         my->blocks_dir = temp_dir / "blocks";
         my->chain_config->blocks_dir = my->blocks_dir;
         try {
            auto shutdown = [](){ return app().quit(); };
            auto check_shutdown = [](){ return app().is_quiting(); };
            auto infile = std::ifstream(my->snapshot_path->generic_string(), (std::ios::in | std::ios::binary));
            auto reader = std::make_shared<istream_snapshot_reader>(infile);
            my->chain.emplace( *my->chain_config, std::move(pfs), *chain_id );
            my->chain->add_indices();
            my->chain->startup(shutdown, check_shutdown, reader);
            infile.close();
            EOS_ASSERT( !app().is_quiting(), snapshot_exception, "Loading of snapshot failed" );
            app().quit(); // shutdown as we will be finished after writing the snapshot

            ilog("Writing snapshot: ${s}", ("s", my->snapshot_path->generic_string() + ".json"));
            auto snap_out = std::ofstream( my->snapshot_path->generic_string() + ".json", (std::ios::out) );
            auto writer = std::make_shared<ostream_json_snapshot_writer>( snap_out );
            my->chain->write_snapshot( writer );
            writer->finalize();
            snap_out.flush();
            snap_out.close();
         } catch (const database_guard_exception& e) {
            log_guard_exception(e);
            // make sure to properly close the db
            my->chain.reset();
            fc::remove_all(temp_dir);
            throw;
         }
         my->chain.reset();
         fc::remove_all(temp_dir);
         ilog("Completed writing snapshot: ${s}", ("s", my->snapshot_path->generic_string() + ".json"));
         ilog("==== Ignore any additional log messages. ====");

         EOS_THROW( node_management_success, "extracted json from snapshot" );
         */
   return 0;
}
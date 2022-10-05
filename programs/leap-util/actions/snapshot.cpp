#include "snapshot.hpp"
#include <memory>

#include <fc/bitutil.hpp>
#include <fc/filesystem.hpp>
#include <fc/io/json.hpp>
#include <fc/variant.hpp>

#include <boost/exception/diagnostic_information.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/path.hpp>

void snapshot_actions::setup(CLI::App& app) {
   auto* sub = app.add_subcommand("snapshot", "snapshot utility");
   sub->add_subcommand("to-json", "convert snapshot file to convert to json format");

   // options
   sub->add_option("--input-file,-i", opt->input_file, "snapshot file to convert to json format, writes to <file>.json if output file not specified (tmp state dir used), and exit.")->required();
   sub->add_option("--output-file,-o", opt->output_file, "the file to write the output to (absolute or relative path).  if not specified then output is to stdout.");

   sub->callback([this]() {
      int rc = run_subcommand();
      // properly return err code in main
      if(rc) throw(CLI::RuntimeError(rc));
   });
}

int snapshot_actions::run_subcommand() {
   if(!opt->input_file.empty()) {
      if(!fc::exists(opt->input_file)) {
         std::cerr << "cannot load snapshot, " << opt->input_file << " does not exist" << std::endl;
         return -1;
      }
   }
   // todo: implementation here

   return 0;
}
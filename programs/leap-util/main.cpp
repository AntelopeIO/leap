#include <fc/bitutil.hpp>
#include <fc/filesystem.hpp>
#include <fc/io/json.hpp>
#include <fc/variant.hpp>

#include "CLI11.hpp"

#include "actions_blocklog.hpp"
#include "actions_generic.hpp"
#include "actions_snapshot.hpp"

int main(int argc, char** argv) {
   fc::logger::get(DEFAULT_LOGGER).set_log_level(fc::log_level::debug);

   CLI::App app{"Command Line Leap Utility"};
   app.set_help_all_flag("--help-all", "Show all help");
   app.require_subcommand(1, 2);

   // register actions
   GenericActions().setup(app);
   BlocklogActions().setup(app);
   SnapshotActions().setup(app);

   // parse
   CLI11_PARSE(app, argc, argv);
}

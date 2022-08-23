#include <fc/io/json.hpp>
#include <fc/filesystem.hpp>
#include <fc/variant.hpp>
#include <fc/bitutil.hpp>

#include "CLI11.hpp"

#include "actions_generic.hpp"

int main(int argc, char** argv) {
   fc::logger::get(DEFAULT_LOGGER).set_log_level(fc::log_level::debug);

   CLI::App app{"Command Line Leap Utility"};
   app.require_subcommand(1, 2);

   // version, etc
   GenericActions().setup(app);
   // SomeOtherActions1().setup(app);
   // SomeOtherActions2().setup(app);

   // parse
   CLI11_PARSE(app, argc, argv);
}

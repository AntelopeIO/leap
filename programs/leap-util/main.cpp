#include <fc/bitutil.hpp>
#include <fc/filesystem.hpp>
#include <fc/io/json.hpp>
#include <fc/variant.hpp>

#include <cli11/CLI11.hpp>

#include "actions/blocklog.hpp"
#include "actions/chain.hpp"
#include "actions/generic.hpp"
#include "actions/snapshot.hpp"

#include <memory>

int main(int argc, char** argv) {
   fc::logger::get(DEFAULT_LOGGER).set_log_level(fc::log_level::debug);

   CLI::App app{"Leap Command Line Utility"};

   // custom leap formatter
   auto fmt = std::make_shared<CLI::LeapFormatter>();
   app.formatter(fmt);

   app.set_help_all_flag("--help-all", "Show all help");
   app.failure_message(CLI::FailureMessage::help);
   app.require_subcommand(1, 2);

   // generics sc tree
   auto generic_subcommand = std::make_shared<generic_actions>();
   generic_subcommand->setup(app);

   // blocklog sc tree from eosio-blocklog
   auto blocklog_subcommand = std::make_shared<blocklog_actions>();
   blocklog_subcommand->setup(app);

   // snapshot sc tree, reserved
   // auto snapshot_subcommand = std::make_shared<snapshot_actions>();
   // snapshot_subcommand->setup(app);

   // chain subcommand from nodeos chain_plugin
   auto chain_subcommand = std::make_shared<chain_actions>();
   chain_subcommand->setup(app);

   // parse
   CLI11_PARSE(app, argc, argv);
}

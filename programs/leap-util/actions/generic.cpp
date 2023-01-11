#include "generic.hpp"
#include <eosio/version/version.hpp>
#include <iostream>

void generic_actions::setup(CLI::App& app) {
   auto* sub = app.add_subcommand("version", "retrieve version information");

   /// makes  subcommand "version" non-actionable, actions expected in child subcommands
   sub->require_subcommand(); 
   
   /// this will make options defined on parent subcommand to be actionable for child subcommands and to be displayed in --help call for them
   /// it's not required in this handler, but set here for documentation purposes to showcase that in most cases for custom action handlers it is a desirable behavior
   sub->fallthrough();     

   sub->add_subcommand("client", "retrieve basic version information of the client")->callback([this]() { cb_version(false); });
   sub->add_subcommand("full", "retrieve full version information of the client")->callback([this]() { cb_version(true); });
}

void generic_actions::cb_version(bool full) {
   std::cout << (full ? eosio::version::version_full() : eosio::version::version_client()) << '\n';
}
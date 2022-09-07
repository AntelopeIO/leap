#include "actions_generic.hpp"
#include <eosio/version/version.hpp>
#include <iostream>
#include <memory>


void GenericActions::setup(CLI::App& app) {
   auto* sub = app.add_subcommand("version", "Retrieve version information");
   // sub->require_subcommand();
   sub->add_subcommand("client", "Retrieve basic version information of the client")->callback([this]() { cb_version(false); });
   sub->add_subcommand("full", "Retrieve full version information of the client")->callback([this]() { cb_version(true); });
}

void GenericActions::cb_version(bool full) {
   std::cout << (full ? eosio::version::version_full() : eosio::version::version_client()) << '\n';
}
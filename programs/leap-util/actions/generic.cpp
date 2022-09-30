#include "generic.hpp"
#include <eosio/version/version.hpp>
#include <iostream>

void generic_actions::setup(CLI::App& app) {
   auto* sub = app.add_subcommand("version", "retrieve version information");
   sub->require_subcommand(1);
   sub->add_subcommand("client", "retrieve basic version information of the client")->callback([this]() { cb_version(false); });
   sub->add_subcommand("full", "retrieve full version information of the client")->callback([this]() { cb_version(true); });
}

void generic_actions::cb_version(bool full) {
   std::cout << (full ? eosio::version::version_full() : eosio::version::version_client()) << '\n';
}
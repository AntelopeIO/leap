#pragma once

#include <cli11/CLI11.hpp>

#include <memory>

template<class TSubcommandOptions>
class ISubCommand {
protected:
   std::shared_ptr<TSubcommandOptions> opt;
   ISubCommand() : opt(std::make_shared<TSubcommandOptions>()) {}

public:
   virtual void setup(CLI::App& app) = 0;
};
#pragma once

#include <cli11/CLI11.hpp>

#include <memory>

template<class subcommand_options>
class sub_command {
protected:
   std::shared_ptr<subcommand_options> opt;
   sub_command() : opt(std::make_shared<subcommand_options>()) {}

public:
   virtual ~sub_command(){}
   virtual void setup(CLI::App& app) = 0;
};
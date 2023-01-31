#pragma once

#include <CLI/CLI.hpp>
#include <memory>

class leap_util_exception_handler {
public:
   leap_util_exception_handler() {}
   ~leap_util_exception_handler() {}
   void print_exception() noexcept;
};

template<class subcommand_options, class exception_handler = leap_util_exception_handler>
class sub_command {
protected:
   std::shared_ptr<subcommand_options> opt;
   std::unique_ptr<exception_handler> exh;

   sub_command() : opt(std::make_shared<subcommand_options>()), exh(std::make_unique<exception_handler>()) {}
   void print_exception() noexcept { exh->print_exception(); };

public:
   virtual ~sub_command() {}
   virtual void setup(CLI::App& app) = 0;
};
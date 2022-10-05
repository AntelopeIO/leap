#include "subcommand.hpp"
#include <boost/filesystem.hpp>
#include <boost/filesystem/path.hpp>

struct chain_options {
   bool build_just_print = false;
   std::string build_output_file = "";
};

class chain_actions : public sub_command<chain_options> {
public:
   chain_actions() : sub_command() {}
   void setup(CLI::App& app);

   // callbacks
   int run_subcommand_build();
};
#include "subcommand.hpp"

struct chain_options {
   bool build_just_print = false;
   std::string build_output_file = "";
   std::string sstate_state_dir = "";
};

class chain_actions : public sub_command<chain_options> {
public:
   chain_actions() : sub_command() {}
   void setup(CLI::App& app);

   // callbacks
   int run_subcommand_build();
   int run_subcommand_sstate();
};
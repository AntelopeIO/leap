#include "subcommand.hpp"

struct snapshot_options {
   std::string input_file = "";
   std::string output_file = "";
   uint64_t db_size = 65536ull;
   uint64_t guard_size = 1;
   std::string chain_id = "";
};

class snapshot_actions : public sub_command<snapshot_options> {
public:
   snapshot_actions() : sub_command() {}
   void setup(CLI::App& app);

   // callbacks
   int run_subcommand();
};
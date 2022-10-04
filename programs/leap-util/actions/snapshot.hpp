#include "subcommand.hpp"
#include <boost/filesystem.hpp>
#include <boost/filesystem/path.hpp>

struct snapshot_options {
   std::string input_file = "";
   std::string output_file = "";
};

class snapshot_actions : public sub_command<snapshot_options> {
public:
   snapshot_actions() : sub_command() {}
   void setup(CLI::App& app);

   // callbacks
   int run_subcommand();
};
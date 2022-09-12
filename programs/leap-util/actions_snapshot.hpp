#include "subcommand.hpp"
#include <boost/filesystem.hpp>
#include <boost/filesystem/path.hpp>

struct SnapshotOptions {
   std::string input_file  = "";
   std::string output_file = "";
};

class SnapshotActions : public ISubCommand<SnapshotOptions> {
public:
   SnapshotActions() : ISubCommand(){}
   virtual void setup(CLI::App& app);

   // callbacks
   int run_subcommand();
};
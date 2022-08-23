#include "subcommand.hpp"

struct ScGenericOptions {
  std::string file;
  bool with_foo;
};

class GenericActions : public ISubCommand<ScGenericOptions> {
public:
  GenericActions() : ISubCommand() {}
  virtual void setup(CLI::App &app);

  // callbacks
  void cb_version(bool full);
};
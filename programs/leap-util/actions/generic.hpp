#include "subcommand.hpp"

struct sc_generic_options {
};

class generic_actions : public sub_command<sc_generic_options> {
public:
   generic_actions() : sub_command() {}
   void setup(CLI::App& app);

   // callbacks
   void cb_version(bool full);
};
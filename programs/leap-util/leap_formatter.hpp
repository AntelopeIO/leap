
#include <cli11/CLI11.hpp>
#include <iostream>
#include <memory>

class leap_formatter : public CLI::Formatter {
  public:
    leap_formatter() : Formatter() {
        // 30 is default for CLI11, but seems like overkill
        column_width(20);
    }
};
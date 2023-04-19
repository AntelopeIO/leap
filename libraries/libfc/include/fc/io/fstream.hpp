#include <fc/filesystem.hpp>
#include <string>

namespace fc {
  /**
   * Grab the full contents of a file into a string object.
   * NB reading a full file into memory is a poor choice
   * if the file may be very large.
   */
  void read_file_contents( const std::filesystem::path& filename, std::string& result );
}

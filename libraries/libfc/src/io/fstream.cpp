#include <fstream>
#include <sstream>

#include <fc/exception/exception.hpp>
#include <fc/log/logger.hpp>
#include <filesystem>

namespace fc {

   void read_file_contents( const std::filesystem::path& filename, std::string& result )
   {
      std::ifstream f( filename.string(), std::ios::in | std::ios::binary );
      FC_ASSERT(f, "Failed to open ${filename}", ("filename", filename.string()));
      // don't use fc::stringstream here as we need something with override for << rdbuf()
      std::stringstream ss;
      ss << f.rdbuf();
      FC_ASSERT(f, "Failed reading ${filename}", ("filename", filename.string()));
      result = ss.str();
   }

} // namespace fc

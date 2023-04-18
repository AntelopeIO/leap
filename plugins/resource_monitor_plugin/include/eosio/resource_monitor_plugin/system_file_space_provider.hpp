#pragma once

#include <sys/stat.h>
#include <filesystem>


namespace eosio::resource_monitor {
   class system_file_space_provider {
   public:
      system_file_space_provider()
      {
      }

      // Wrapper for Linux stat
      int get_stat(const char *path, struct stat *buf) const;

      // Wrapper for boost file system space
      std::filesystem::space_info get_space(const std::filesystem::path& p, std::error_code& ec) const;
   };
}

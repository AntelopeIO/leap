#include <eosio/resource_monitor_plugin/system_file_space_provider.hpp>


namespace eosio::resource_monitor {
   int system_file_space_provider::get_stat(const char *path, struct stat *buf) const {
      return stat(path, buf);
   }

   std::filesystem::space_info system_file_space_provider::get_space(const std::filesystem::path& p, std::error_code& ec) const {
      return std::filesystem::space(p, ec);
   }

   using std::filesystem::directory_iterator;
}

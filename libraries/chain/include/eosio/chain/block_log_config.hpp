#pragma once
#include <boost/filesystem/path.hpp>
#include <variant>

namespace eosio { namespace chain {


   struct basic_blocklog_config {};

   struct empty_blocklog_config {};

   struct partitioned_blocklog_config {
      std::filesystem::path retained_dir;
      std::filesystem::path archive_dir;
      uint32_t  stride                  = UINT32_MAX;
      uint32_t  max_retained_files      = UINT32_MAX;
   };

   struct prune_blocklog_config {
      uint32_t prune_blocks; // number of blocks to prune to when doing a prune
      size_t   prune_threshold =
            4 * 1024 * 1024; //(approximately) how many bytes need to be added before a prune is performed
      std::optional<size_t>
            vacuum_on_close; // when set, a vacuum is performed on dtor if log contains less than this many live bytes
   };

   using block_log_config =
         std::variant<basic_blocklog_config, empty_blocklog_config, partitioned_blocklog_config, prune_blocklog_config>;

}} // namespace eosio::chain

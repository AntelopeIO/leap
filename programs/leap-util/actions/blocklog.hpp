#include "subcommand.hpp"
#include <boost/filesystem.hpp>
#include <boost/filesystem/path.hpp>
#include <eosio/chain/block_log.hpp>
#include <eosio/chain/config.hpp>

using namespace eosio::chain;

struct blocklog_options {
   std::string blocks_dir = "blocks";
   std::string output_file = "";
   uint32_t first_block = 0;
   uint32_t last_block = std::numeric_limits<uint32_t>::max();
   std::string output_dir = "";
   uint32_t stride = 100000;

   // flags
   bool no_pretty_print = false;
   bool as_json_array = false;

   block_log_config blog_conf;
};

class blocklog_actions : public sub_command<blocklog_options> {
public:
   blocklog_actions() : sub_command() {}
   void setup(CLI::App& app);

protected:
   void initialize();
   int trim_blocklog_end(std::filesystem::path block_dir, uint32_t n);
   bool trim_blocklog_front(std::filesystem::path block_dir, uint32_t n);
   void extract_block_range(std::filesystem::path block_dir, std::filesystem::path output_dir, uint32_t start, uint32_t last);

   int make_index();
   int trim_blocklog();
   int extract_blocks();
   int smoke_test();
   int do_vacuum();
   int do_genesis();
   int read_log();

   int split_blocks();
   int merge_blocks();
};
#include "subcommand.hpp"
#include <boost/filesystem.hpp>
#include <boost/filesystem/path.hpp>
#include <eosio/chain/block_log.hpp>
#include <eosio/chain/config.hpp>

namespace bfs = boost::filesystem;
using namespace eosio::chain;

struct BlocklogOptions {
   std::string blocks_dir = "blocks";
   std::string output_file = "";
   int first_block = 0;
   int last_block = std::numeric_limits<uint32_t>::max();
   std::string output_dir;

   bool no_pretty_print;
   bool as_json_array;
   bool make_index;
   bool trim_blocklog;
   bool extract_blocks;
   bool smoke_test;
   bool vacuum;

   std::optional<block_log_prune_config> blog_keep_prune_conf;
};

class BlocklogActions : public ISubCommand<BlocklogOptions> {
public:
   BlocklogActions() : ISubCommand() {}
   virtual void setup(CLI::App& app);

   // callbacks
   int run_subcommand();
protected:
   void initialize();
   int  trim_blocklog_end(bfs::path block_dir, uint32_t n);
   bool trim_blocklog_front(bfs::path block_dir, uint32_t n);
   bool extract_block_range(bfs::path block_dir, bfs::path output_dir, uint32_t start, uint32_t end);
   void smoke_test(bfs::path block_dir);
   void do_vacuum();
   void read_log();
};
#include <memory>
#include <eosio/chain/abi_serializer.hpp>
#include <eosio/chain/block_log.hpp>
#include <eosio/chain/fork_database.hpp>
#include <eosio/chain/config.hpp>

#include <fc/io/json.hpp>
#include <fc/filesystem.hpp>
#include <fc/variant.hpp>
#include <fc/bitutil.hpp>

#include <boost/exception/diagnostic_information.hpp>
#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/path.hpp>

#include <chrono>

#ifndef _WIN32
#define FOPEN(p, m) fopen(p, m)
#else
#define CAT(s1, s2) s1##s2
#define PREL(s) CAT(L, s)
#define FOPEN(p, m) _wfopen(p, PREL(m))
#endif

using namespace eosio::chain;
namespace bfs = boost::filesystem;
namespace bpo = boost::program_options;
using bpo::options_description;
using bpo::variables_map;

struct blocklog
{
   blocklog() {}

   void read_log();
   void set_program_options(options_description& cli);
   void initialize(const variables_map& options);
   void do_vacuum();

   bfs::path blocks_dir;
   bfs::path output_file;
   uint32_t  first_block     = 0;
   uint32_t  last_block      = std::numeric_limits<uint32_t>::max();
   bool      no_pretty_print = false;
   bool      as_json_array   = false;
   bool      make_index      = false;
   bool      trim_log        = false;
   bool      extract_blocks  = false;
   bool      smoke_test      = false;
   bool      vacuum          = false;
   bool      help            = false;

   std::optional<block_log_prune_config> blog_keep_prune_conf;
};

struct report_time
{
   report_time(std::string desc)
      : _start(std::chrono::high_resolution_clock::now())
      , _desc(desc)
   {
   }

   void report()
   {
      const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
                               std::chrono::high_resolution_clock::now() - _start)
                               .count() /
                            1000;
      ilog("eosio-blocklog - ${desc} took ${t} msec", ("desc", _desc)("t", duration));
   }

   const std::chrono::high_resolution_clock::time_point _start;
   const std::string                                    _desc;
};

void blocklog::do_vacuum()
{
   EOS_ASSERT(blog_keep_prune_conf, block_log_exception, "blocks.log is not a pruned log; nothing to vacuum");
   block_log blocks(blocks_dir,
                    std::optional<block_log_prune_config>()); // passing an unset block_log_prune_config turns
                                                              // off pruning this performs a vacuum
   ilog("Successfully vacuumed block log");
}

void blocklog::read_log()
{
   report_time rt("reading log");
   block_log   block_logger(blocks_dir, blog_keep_prune_conf);
   const auto  end = block_logger.read_head();
   EOS_ASSERT(end, block_log_exception, "No blocks found in block log");
   EOS_ASSERT(end->block_num() > 1, block_log_exception, "Only one block found in block log");

   // fix message below, first block might not be 1, first_block_num is not set yet
   ilog("existing block log contains block num ${first} through block num ${n}",
        ("first", block_logger.first_block_num())("n", end->block_num()));
   if (first_block < block_logger.first_block_num()) {
      first_block = block_logger.first_block_num();
   }

   eosio::chain::branch_type fork_db_branch;
   if (fc::exists(blocks_dir / config::reversible_blocks_dir_name / config::forkdb_filename)) {
      ilog("opening fork_db");
      fork_database fork_db(blocks_dir / config::reversible_blocks_dir_name);

      fork_db.open([](block_timestamp_type         timestamp,
                      const flat_set<digest_type>& cur_features,
                      const vector<digest_type>&   new_features) {});

      fork_db_branch = fork_db.fetch_branch(fork_db.head()->id);
      if (fork_db_branch.empty()) {
         elog("no blocks available in reversible block database: only block_log blocks are available");
      } else {
         auto first = fork_db_branch.rbegin();
         auto last  = fork_db_branch.rend() - 1;
         ilog("existing reversible fork_db block num ${first} through block num ${last} ",
              ("first", (*first)->block_num)("last", (*last)->block_num));
         EOS_ASSERT(end->block_num() + 1 == (*first)->block_num,
                    block_log_exception,
                    "fork_db does not start at end of block log");
      }
   }

   std::ofstream output_blocks;
   std::ostream* out;
   if (!output_file.empty()) {
      output_blocks.open(output_file.generic_string().c_str());
      if (output_blocks.fail()) {
         std::ostringstream ss;
         ss << "Unable to open file '" << output_file.string() << "'";
         throw std::runtime_error(ss.str());
      }
      out = &output_blocks;
   } else
      out = &std::cout;

   if (as_json_array)
      *out << "[";
   uint32_t               block_num = (first_block < 1) ? 1 : first_block;
   signed_block_ptr       next;
   fc::variant            pretty_output;
   const fc::microseconds deadline    = fc::seconds(10);
   auto                   print_block = [&](signed_block_ptr& next) {
      abi_serializer::to_variant(
         *next,
         pretty_output,
         [](account_name n) { return std::optional<abi_serializer>(); },
         abi_serializer::create_yield_function(deadline));
      const auto     block_id         = next->calculate_id();
      const uint32_t ref_block_prefix = block_id._hash[1];
      const auto enhanced_object = fc::mutable_variant_object("block_num", next->block_num())("id", block_id)(
         "ref_block_prefix", ref_block_prefix)(pretty_output.get_object());
      fc::variant v(std::move(enhanced_object));
      if (no_pretty_print)
         *out << fc::json::to_string(v, fc::time_point::maximum());
      else
         *out << fc::json::to_pretty_string(v) << "\n";
   };
   bool contains_obj = false;
   while ((block_num <= last_block) && (next = block_logger.read_block_by_num(block_num))) {
      if (as_json_array && contains_obj)
         *out << ",";
      print_block(next);
      ++block_num;
      contains_obj = true;
   }

   if (!fork_db_branch.empty()) {
      for (auto bitr = fork_db_branch.rbegin(); bitr != fork_db_branch.rend() && block_num <= last_block;
           ++bitr) {
         if (as_json_array && contains_obj)
            *out << ",";
         auto next = (*bitr)->block;
         print_block(next);
         ++block_num;
         contains_obj = true;
      }
   }

   if (as_json_array)
      *out << "]";
   rt.report();
}

void blocklog::set_program_options(options_description& cli)
{
   cli.add_options()(
      "blocks-dir",
      bpo::value<bfs::path>()->default_value("blocks"),
      "the location of the blocks directory (absolute path or relative to the current directory)")(
      "output-file,o",
      bpo::value<bfs::path>(),
      "the file to write the output to (absolute or relative path).  If not specified then output is to "
      "stdout.")("first,f",
                 bpo::value<uint32_t>(&first_block)->default_value(0),
                 "the first block number to log or the first to keep if trim-blocklog")(
      "last,l",
      bpo::value<uint32_t>(&last_block)->default_value(std::numeric_limits<uint32_t>::max()),
      "the last block number to log or the last to keep if trim-blocklog")(
      "no-pretty-print",
      bpo::bool_switch(&no_pretty_print)->default_value(false),
      "Do not pretty print the output.  Useful if piping to jq to improve performance.")(
      "as-json-array",
      bpo::bool_switch(&as_json_array)->default_value(false),
      "Print out json blocks wrapped in json array (otherwise the output is free-standing json objects).")(
      "make-index",
      bpo::bool_switch(&make_index)->default_value(false),
      "Create blocks.index from blocks.log. Must give 'blocks-dir'. Give 'output-file' relative to current "
      "directory or absolute path (default is <blocks-dir>/blocks.index).")(
      "trim-blocklog",
      bpo::bool_switch(&trim_log)->default_value(false),
      "Trim blocks.log and blocks.index. Must give 'blocks-dir' and 'first' and/or 'last'.")(
      "extract-blocks",
      bpo::bool_switch(&extract_blocks)->default_value(false),
      "Extract range of blocks from blocks.log and write to output-dir.  Must give 'first' and/or 'last'.")(
      "output-dir",
      bpo::value<bfs::path>(),
      "the output directory for the block log extracted from blocks-dir")(
      "smoke-test",
      bpo::bool_switch(&smoke_test)->default_value(false),
      "Quick test that blocks.log and blocks.index are well formed and agree with each other.")(
      "vacuum",
      bpo::bool_switch(&vacuum)->default_value(false),
      "Vacuum a pruned blocks.log in to an un-pruned blocks.log")(
      "help,h", bpo::bool_switch(&help)->default_value(false), "Print this help message and exit.");
}

void blocklog::initialize(const variables_map& options)
{
   try {
      auto bld = options.at("blocks-dir").as<bfs::path>();
      if (bld.is_relative())
         blocks_dir = bfs::current_path() / bld;
      else
         blocks_dir = bld;

      if (options.count("output-file")) {
         bld = options.at("output-file").as<bfs::path>();
         if (bld.is_relative())
            output_file = bfs::current_path() / bld;
         else
            output_file = bld;
      }

      // if the log is pruned, keep it that way by passing in a config with a large block pruning value. There
      // is otherwise no
      //  way to tell block_log "keep the current non/pruneness of the log"
      if (block_log::is_pruned_log(blocks_dir)) {
         blog_keep_prune_conf.emplace();
         blog_keep_prune_conf->prune_blocks = UINT32_MAX;
      }
   }
   FC_LOG_AND_RETHROW()
}

int trim_blocklog_end(bfs::path block_dir, uint32_t n)
{ // n is last block to keep (remove later blocks)
   report_time rt("trimming blocklog end");
   using namespace std;
   trim_data td(block_dir);
   cout << "\nIn directory " << block_dir << " will trim all blocks after block " << n << " from "
        << td.block_file_name.generic_string() << " and " << td.index_file_name.generic_string() << ".\n";
   if (n < td.first_block) {
      cerr << "All blocks are after block " << n
           << " so do nothing (trim_end would delete entire blocks.log)\n";
      return 1;
   }
   if (n >= td.last_block) {
      cerr << "There are no blocks after block " << n << " so do nothing\n";
      return 2;
   }
   const uint64_t end_of_new_file = td.block_pos(n + 1);
   bfs::resize_file(td.block_file_name, end_of_new_file);
   const uint64_t index_end = td.block_index(n) + sizeof(uint64_t); // advance past record for block n
   bfs::resize_file(td.index_file_name, index_end);
   cout << "blocks.index has been trimmed to " << index_end << " bytes\n";
   rt.report();
   return 0;
}

bool trim_blocklog_front(bfs::path block_dir, uint32_t n)
{ // n is first block to keep (remove prior blocks)
   report_time    rt("trimming blocklog start");
   block_num_type end    = std::numeric_limits<block_num_type>::max();
   const bool     status = block_log::extract_block_range(block_dir, block_dir / "old", n, end, true);
   rt.report();
   return status;
}

bool extract_block_range(bfs::path block_dir, bfs::path output_dir, uint32_t start, uint32_t end)
{
   report_time rt("extracting block range");
   EOS_ASSERT(end > start, block_log_exception, "extract range end must be greater than start");
   const bool status = block_log::extract_block_range(block_dir, output_dir, start, end, false);
   rt.report();
   return status;
}

void smoke_test(bfs::path block_dir)
{
   using namespace std;
   cout << "\nSmoke test of blocks.log and blocks.index in directory " << block_dir << '\n';
   trim_data td(block_dir);
   auto      status = fseek(
      td.blk_in, -sizeof(uint64_t), SEEK_END); // get last_block from blocks.log, compare to from blocks.index
   EOS_ASSERT(status == 0,
              block_log_exception,
              "cannot seek to ${file} ${pos} from beginning of file",
              ("file", td.block_file_name.string())("pos", sizeof(uint64_t)));
   uint64_t file_pos;
   auto     size = fread((void*)&file_pos, sizeof(uint64_t), 1, td.blk_in);
   EOS_ASSERT(size == 1, block_log_exception, "${file} read fails", ("file", td.block_file_name.string()));
   status = fseek(td.blk_in, file_pos + trim_data::blknum_offset, SEEK_SET);
   EOS_ASSERT(status == 0,
              block_log_exception,
              "cannot seek to ${file} ${pos} from beginning of file",
              ("file", td.block_file_name.string())("pos", file_pos + trim_data::blknum_offset));
   uint32_t bnum;
   size = fread((void*)&bnum, sizeof(uint32_t), 1, td.blk_in);
   EOS_ASSERT(size == 1, block_log_exception, "${file} read fails", ("file", td.block_file_name.string()));
   bnum = endian_reverse_u32(bnum) + 1; // convert from big endian to little endian and add 1
   EOS_ASSERT(td.last_block == bnum,
              block_log_exception,
              "blocks.log says last block is ${lb} which disagrees with blocks.index",
              ("lb", bnum));
   cout << "blocks.log and blocks.index agree on number of blocks\n";
   uint32_t delta = (td.last_block + 8 - td.first_block) >> 3;
   if (delta < 1)
      delta = 1;
   for (uint32_t n = td.first_block;; n += delta) {
      if (n > td.last_block)
         n = td.last_block;
      td.block_pos(n); // check block 'n' is where blocks.index says
      if (n == td.last_block)
         break;
   }
   cout << "\nno problems found\n"; // if get here there were no exceptions
}

int main(int argc, char** argv)
{
   std::ios::sync_with_stdio(false); // for potential performance boost for large block log files
   options_description cli("eosio-blocklog command line options");
   try {
      blocklog blog;
      blog.set_program_options(cli);
      variables_map vmap;
      bpo::store(bpo::parse_command_line(argc, argv, cli), vmap);
      bpo::notify(vmap);
      if (blog.help) {
         cli.print(std::cerr);
         return 0;
      }
      if (blog.smoke_test) {
         smoke_test(vmap.at("blocks-dir").as<bfs::path>());
         return 0;
      }
      if (blog.trim_log) {
         if (blog.first_block == 0 && blog.last_block == std::numeric_limits<uint32_t>::max()) {
            std::cerr << "trim-blocklog does nothing unless first and/or last block are specified.";
            return -1;
         }
         if (blog.last_block != std::numeric_limits<uint32_t>::max()) {
            if (trim_blocklog_end(vmap.at("blocks-dir").as<bfs::path>(), blog.last_block) != 0)
               return -1;
         }
         if (blog.first_block != 0) {
            if (!trim_blocklog_front(vmap.at("blocks-dir").as<bfs::path>(), blog.first_block))
               return -1;
         }
         return 0;
      }
      if (blog.extract_blocks) {
         if (blog.first_block == 0 && blog.last_block == std::numeric_limits<uint32_t>::max()) {
            std::cerr << "extract-blocklog does nothing unless first and/or last block are specified.";
            return -1;
         }
         if (!extract_block_range(vmap.at("blocks-dir").as<bfs::path>(),
                                  vmap.at("output-dir").as<bfs::path>(),
                                  blog.first_block,
                                  blog.last_block))
            return -1;
         return 0;
      }
      if (blog.vacuum) {
         blog.initialize(vmap);
         blog.do_vacuum();
         return 0;
      }
      if (blog.make_index) {
         const bfs::path blocks_dir = vmap.at("blocks-dir").as<bfs::path>();
         bfs::path       out_file   = blocks_dir / "blocks.index";
         const bfs::path block_file = blocks_dir / "blocks.log";

         if (vmap.count("output-file") > 0)
            out_file = vmap.at("output-file").as<bfs::path>();

         report_time rt("making index");
         const auto  log_level = fc::logger::get(DEFAULT_LOGGER).get_log_level();
         fc::logger::get(DEFAULT_LOGGER).set_log_level(fc::log_level::debug);
         block_log::construct_index(block_file.generic_string(), out_file.generic_string());
         fc::logger::get(DEFAULT_LOGGER).set_log_level(log_level);
         rt.report();
         return 0;
      }
      // else print blocks.log as JSON
      blog.initialize(vmap);
      blog.read_log();
   } catch (const fc::exception& e) {
      elog("${e}", ("e", e.to_detail_string()));
      return -1;
   } catch (const boost::exception& e) {
      elog("${e}", ("e", boost::diagnostic_information(e)));
      return -1;
   } catch (const std::exception& e) {
      elog("${e}", ("e", e.what()));
      return -1;
   } catch (...) {
      elog("unknown exception");
      return -1;
   }

   return 0;
}

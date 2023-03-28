#include <boost/test/unit_test.hpp>

#include <fc/bitutil.hpp>

#include <eosio/chain/block_log.hpp>
#include <eosio/chain/block.hpp>
#include <regex>

using namespace eosio::chain;

struct block_log_extract_fixture {
   block_log_extract_fixture() {
      log.emplace(dir.path());
      log->reset(genesis_state(), std::make_shared<signed_block>());
      BOOST_REQUIRE_EQUAL(log->first_block_num(), 1);
      BOOST_REQUIRE_EQUAL(log->head()->block_num(), 1);
      for(uint32_t i = 2; i < 13; ++i) {
         add(i);
      }
      BOOST_REQUIRE_EQUAL(log->head()->block_num(), 12);
   };

   void add(uint32_t index) {
      signed_block_ptr p = std::make_shared<signed_block>();
      p->previous._hash[0] = fc::endian_reverse_u32(index-1);
      log->append(p, p->calculate_id());
   }

   static void rename_blocks_files(std::filesystem::path dir) {
   // rename blocks files with block number range with those without
   // i.e.   blocks-1-100.index  --> blocks.index
   //        blocks-1-100.log    --> blocks.log
            for (std::filesystem::directory_iterator itr(dir); itr != std::filesystem::directory_iterator{}; ++itr ) {
         auto file_path = itr->path();
         if ( !std::filesystem::is_regular_file( file_path )) continue;
         std::regex block_range_expression("blocks-\\d+-\\d+");
         auto new_path = std::regex_replace(file_path.string(), block_range_expression, "blocks");
         if (new_path != file_path) {
            std::filesystem::rename(file_path, new_path);
         }
      }
   }

   genesis_state gs;
   fc::temp_directory dir;
   std::optional<block_log> log;
};

BOOST_AUTO_TEST_SUITE(block_log_extraction_tests)

BOOST_FIXTURE_TEST_CASE(extract_from_middle, block_log_extract_fixture) try {

   fc::temp_directory output_dir;
   block_num_type start=3, end=7;
   block_log::extract_block_range(dir.path(), output_dir.path(), start, end);
   rename_blocks_files(output_dir.path());
   block_log new_log(output_dir.path());

   auto id = gs.compute_chain_id();
   BOOST_REQUIRE_EQUAL(new_log.extract_chain_id(output_dir.path()), id);
   BOOST_REQUIRE_EQUAL(new_log.first_block_num(), 3);
   BOOST_REQUIRE_EQUAL(new_log.head()->block_num(), 7);


} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(extract_from_start, block_log_extract_fixture) try {

   fc::temp_directory output_dir;
   block_num_type start=1, end=7;
   block_log::extract_block_range(dir.path(), output_dir.path(), start, end);
   rename_blocks_files(output_dir.path());
   block_log new_log(output_dir.path());

   auto id = gs.compute_chain_id();
   BOOST_REQUIRE_EQUAL(new_log.extract_chain_id(output_dir.path()), id);
   BOOST_REQUIRE_EQUAL(new_log.first_block_num(), 1);
   BOOST_REQUIRE_EQUAL(new_log.head()->block_num(), 7);

} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(reextract_from_start, block_log_extract_fixture) try {

   fc::temp_directory output_dir;
   block_num_type start=1, end=9;
   block_log::extract_block_range(dir.path(), output_dir.path(), start, end);
   rename_blocks_files(output_dir.path());
   fc::temp_directory output_dir2;
   end=6;
   block_log::extract_block_range(output_dir.path(), output_dir2.path(), start, end);
   rename_blocks_files(output_dir2.path());
   block_log new_log(output_dir2.path());

   auto id = gs.compute_chain_id();
   BOOST_REQUIRE_EQUAL(new_log.extract_chain_id(output_dir2.path()), id);
   BOOST_REQUIRE_EQUAL(new_log.first_block_num(), 1);
   BOOST_REQUIRE_EQUAL(new_log.head()->block_num(), 6);

} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(extract_to_end, block_log_extract_fixture) try {

   fc::temp_directory output_dir;
   block_num_type start=5, end=std::numeric_limits<block_num_type>::max();
   block_log::extract_block_range(dir.path(), output_dir.path(), start, end);
   rename_blocks_files(output_dir.path());
   block_log new_log(output_dir.path());

   auto id = gs.compute_chain_id();
   BOOST_REQUIRE_EQUAL(new_log.extract_chain_id(output_dir.path()), id);
   BOOST_REQUIRE_EQUAL(new_log.first_block_num(), 5);
   BOOST_REQUIRE_EQUAL(new_log.head()->block_num(), 12);

} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_SUITE_END()

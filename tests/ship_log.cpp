#include <boost/test/unit_test.hpp>
#include <boost/test/data/test_case.hpp>
#include <boost/test/data/monomorphic.hpp>
#include <boost/test/data/monomorphic/generators/xrange.hpp>

#include <fc/io/raw.hpp>
#include <fc/bitutil.hpp>

#include <eosio/state_history/log.hpp>
#include <boost/iostreams/copy.hpp>
#include <boost/iostreams/device/back_inserter.hpp>


namespace bdata = boost::unit_test::data;

struct ship_log_fixture {
   ship_log_fixture(bool enable_read, bool reopen_on_mark, bool remove_index_on_reopen, bool vacuum_on_exit_if_small, std::optional<uint32_t> prune_blocks) :
     enable_read(enable_read), reopen_on_mark(reopen_on_mark),
     remove_index_on_reopen(remove_index_on_reopen), vacuum_on_exit_if_small(vacuum_on_exit_if_small){
      if (prune_blocks)
         conf = eosio::state_history::prune_config{ .prune_blocks = *prune_blocks };
      bounce();
   }

   void add(uint32_t index, size_t size, char fillchar, char prevchar) {
      std::vector<char> a;
      a.assign(size, fillchar);

      auto block_for_id = [](const uint32_t bnum, const char fillc) {
         fc::sha256 m = fc::sha256::hash(fc::sha256::hash(std::to_string(bnum)+fillc));
         m._hash[0] = fc::endian_reverse_u32(bnum);
         return m;
      };

      eosio::state_history_log_header header;
      header.block_id = block_for_id(index, fillchar);
      header.payload_size = 0;

      log->pack_and_write_entry(header, block_for_id(index-1, prevchar), [&](auto& f) {
         boost::iostreams::write(f, a.data(), a.size());
      });

      if(index + 1 > written_data.size())
         written_data.resize(index + 1);
      written_data.at(index) = a;
   }

   void check_range_present(uint32_t first, uint32_t last) {
      namespace bio = boost::iostreams;
      auto r = log->block_range();
      BOOST_REQUIRE_EQUAL(r.first, first);
      BOOST_REQUIRE_EQUAL(r.second-1, last);
      if(enable_read) {
         for(auto i = first; i <= last; i++) {
            auto result = log->create_locked_decompress_stream();
            log->get_unpacked_entry(i, result);
            std::visit(eosio::chain::overloaded{
               [&](std::vector<char>& buff) { BOOST_REQUIRE(buff == written_data.at(i)); },
               [&](std::unique_ptr<bio::filtering_istreambuf>& strm) {
                  std::vector<char> buff;
                  boost::iostreams::copy(*strm, boost::iostreams::back_inserter(buff));
                  BOOST_REQUIRE(buff == written_data.at(i));
               }} , result.buf);
         }
      }
   }

   void check_not_present(uint32_t index) {
      auto result = log->create_locked_decompress_stream();
      BOOST_REQUIRE_EQUAL(log->get_unpacked_entry(index, result), 0);
   }

   void check_empty() {
      BOOST_REQUIRE(log->empty());
   }

   //double the fun
   template <typename F>
   void check_n_bounce(F&& f) {
      f();
      if(reopen_on_mark) {
         bounce();
         f();
      }
   }

   bool enable_read, reopen_on_mark, remove_index_on_reopen, vacuum_on_exit_if_small;
   eosio::state_history_log_config conf;
   fc::temp_directory log_dir;

   std::optional<eosio::state_history_log> log;

   std::vector<std::vector<char>> written_data;

private:
   void bounce() {
      log.reset();
      if(remove_index_on_reopen)
         std::filesystem::remove(log_dir.path()/ (std::string("shipit") + ".index"));
      auto prune_conf = std::get_if<eosio::state_history::prune_config>(&conf);
      if(prune_conf) {
         prune_conf->prune_threshold = 8; //every 8 bytes check in and see if to prune. should make it always check after each entry for us
         if(vacuum_on_exit_if_small)
            prune_conf->vacuum_on_close = 1024*1024*1024; //something large: always vacuum on close for these tests
      }
      log.emplace("shipit", log_dir.path(), conf);
   }
};

//can only punch holes on filesystem block boundaries. let's make sure the entries we add are larger than that
static size_t larger_than_tmpfile_blocksize() {
   fc::temp_cfile tf;
   auto& cf = tf.file();
   return cf.filesystem_block_size() + cf.filesystem_block_size()/2;
}

BOOST_AUTO_TEST_SUITE(ship_file_tests)

BOOST_DATA_TEST_CASE(basic_prune_test, bdata::xrange(2) * bdata::xrange(2) * bdata::xrange(2) * bdata::xrange(2), enable_read, reopen_on_mark, remove_index_on_reopen, vacuum_on_exit_if_small)  { try {
   ship_log_fixture t(enable_read, reopen_on_mark, remove_index_on_reopen, vacuum_on_exit_if_small, 4);

   t.check_empty();

   //with a small prune blocks value, the log will attempt to prune every filesystem block size. So let's just make
   // every entry be greater than that size
   size_t payload_size = larger_than_tmpfile_blocksize();

   //we'll start at 2 here, since that's what you'd get from starting from genesis, but it really doesn't matter
   // one way or another for the ship log logic
   t.add(2, payload_size, 'A', 'A');
   t.add(3, payload_size, 'B', 'A');
   t.add(4, payload_size, 'C', 'B');
   t.check_n_bounce([&]() {
      t.check_range_present(2, 4);
   });

   t.add(5, payload_size, 'D', 'C');
   t.check_n_bounce([&]() {
      t.check_range_present(2, 5);
   });

   t.add(6, payload_size, 'E', 'D');
   t.check_n_bounce([&]() {
      t.check_not_present(2);
      t.check_range_present(3, 6);
   });

   t.add(7, payload_size, 'F', 'E');
   t.check_n_bounce([&]() {
      t.check_not_present(2);
      t.check_not_present(3);
      t.check_range_present(4, 7);
   });

   //undo 6 & 7 and reapply 6
   t.add(6, payload_size, 'G', 'D');
   t.check_n_bounce([&]() {
      t.check_not_present(2);
      t.check_not_present(3);
      t.check_not_present(7);
      t.check_range_present(4, 6);
   });

   t.add(7, payload_size, 'H', 'G');
   t.check_n_bounce([&]() {
      t.check_not_present(2);
      t.check_not_present(3);
      t.check_range_present(4, 7);
   });

   t.add(8, payload_size, 'I', 'H');
   t.add(9, payload_size, 'J', 'I');
   t.add(10, payload_size, 'K', 'J');
   t.check_n_bounce([&]() {
      t.check_range_present(7, 10);
   });

   //undo back to the first stored block
   t.add(7, payload_size, 'L', 'G');
   t.check_n_bounce([&]() {
      t.check_range_present(7, 7);
      t.check_not_present(6);
      t.check_not_present(8);
   });

   t.add(8, payload_size, 'M', 'L');
   t.add(9, payload_size, 'N', 'M');
   t.add(10, payload_size, 'O', 'N');
   t.add(11, payload_size, 'P', 'O');
   t.check_n_bounce([&]() {
      t.check_range_present(8, 11);
      t.check_not_present(6);
      t.check_not_present(7);
   });

   //undo past the first stored
   t.add(6, payload_size, 'Q', 'D');
   t.check_n_bounce([&]() {
      t.check_range_present(6, 6);
      t.check_not_present(7);
      t.check_not_present(8);
   });

   //pile up a lot
   t.add(7, payload_size, 'R', 'Q');
   t.add(8, payload_size, 'S', 'R');
   t.add(9, payload_size, 'T', 'S');
   t.add(10, payload_size, 'U', 'T');
   t.add(11, payload_size, 'V', 'U');
   t.add(12, payload_size, 'W', 'V');
   t.add(13, payload_size, 'X', 'W');
   t.add(14, payload_size, 'Y', 'X');
   t.add(15, payload_size, 'Z', 'Y');
   t.add(16, payload_size, '1', 'Z');
   t.check_n_bounce([&]() {
      t.check_range_present(13, 16);
      t.check_not_present(12);
      t.check_not_present(17);
   });

   //invalid fork, previous should be 'X'
   BOOST_REQUIRE_EXCEPTION(t.add(14, payload_size, '*', 'W' ), eosio::chain::plugin_exception, [](const eosio::chain::plugin_exception& e) {
      return e.to_detail_string().find("missed a fork change") != std::string::npos;
   });

   //start from genesis not allowed
   BOOST_REQUIRE_EXCEPTION(t.add(2, payload_size, 'A', 'A');, eosio::chain::plugin_exception, [](const eosio::chain::plugin_exception& e) {
      std::string err = e.to_detail_string();
      return err.find("Existing ship log") != std::string::npos && err.find("when starting from genesis block") != std::string::npos;
   });

} FC_LOG_AND_RETHROW() }

BOOST_DATA_TEST_CASE(basic_test, bdata::xrange(2) * bdata::xrange(2) * bdata::xrange(2), enable_read, reopen_on_mark, remove_index_on_reopen)  { try {
   ship_log_fixture t(enable_read, reopen_on_mark, remove_index_on_reopen, false, std::optional<uint32_t>());

   t.check_empty();
   size_t payload_size = larger_than_tmpfile_blocksize();

   //we'll start off with a high number; but it really doesn't matter for ship's logs
   t.add(200, payload_size, 'A', 'A');
   t.add(201, payload_size, 'B', 'A');
   t.add(202, payload_size, 'C', 'B');
   t.check_n_bounce([&]() {
      t.check_range_present(200, 202);
   });
   t.add(203, payload_size, 'D', 'C');
   t.add(204, payload_size, 'E', 'D');
   t.add(205, payload_size, 'F', 'E');
   t.add(206, payload_size, 'G', 'F');
   t.add(207, payload_size, 'H', 'G');
   t.check_n_bounce([&]() {
      t.check_range_present(200, 207);
   });

   //fork off G & H
   t.add(206, payload_size, 'I', 'F');
   t.add(207, payload_size, 'J', 'I');
   t.check_n_bounce([&]() {
      t.check_range_present(200, 207);
   });

   t.add(208, payload_size, 'K', 'J');
   t.add(209, payload_size, 'L', 'K');
   t.check_n_bounce([&]() {
      t.check_range_present(200, 209);
      t.check_not_present(199);
      t.check_not_present(210);
   });

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(empty) { try {
   fc::temp_directory log_dir;

   {
      eosio::state_history_log log("empty", log_dir.path());
      BOOST_REQUIRE(log.empty());
   }
   //reopen
   {
      eosio::state_history_log log("empty", log_dir.path());
      BOOST_REQUIRE(log.empty());
   }
   //reopen but prunned set
   const eosio::state_history::prune_config simple_prune_conf = {
      .prune_blocks = 4
   };
   {
      eosio::state_history_log log("empty", log_dir.path(), simple_prune_conf);
      BOOST_REQUIRE(log.empty());
   }
   {
      eosio::state_history_log log("empty", log_dir.path(), simple_prune_conf);
      BOOST_REQUIRE(log.empty());
   }
   //back to non pruned
   {
      eosio::state_history_log log("empty", log_dir.path());
      BOOST_REQUIRE(log.empty());
   }
   {
      eosio::state_history_log log("empty", log_dir.path());
      BOOST_REQUIRE(log.empty());
   }

   auto log_file = (log_dir.path()/ (std::string("empty") + ".log")).string();
   auto index_file = (log_dir.path()/ (std::string("empty") + ".index")).string();

   BOOST_REQUIRE(std::filesystem::file_size(log_file.c_str()) == 0);
   BOOST_REQUIRE(std::filesystem::file_size(index_file.c_str()) == 0);

   //one more time to pruned, just to make sure
   {
      eosio::state_history_log log("empty", log_dir.path(), simple_prune_conf);
      BOOST_REQUIRE(log.empty());
   }
   BOOST_REQUIRE(std::filesystem::file_size(log_file.c_str()) == 0);
   BOOST_REQUIRE(std::filesystem::file_size(index_file.c_str()) == 0);
}  FC_LOG_AND_RETHROW() }

BOOST_DATA_TEST_CASE(non_prune_to_prune, bdata::xrange(2) * bdata::xrange(2), enable_read, remove_index_on_reopen)  { try {
   ship_log_fixture t(enable_read, true, remove_index_on_reopen, false, std::optional<uint32_t>());

   t.check_empty();
   size_t payload_size = larger_than_tmpfile_blocksize();

   t.add(2, payload_size, 'A', 'A');
   t.add(3, payload_size, 'B', 'A');
   t.add(4, payload_size, 'C', 'B');
   t.add(5, payload_size, 'D', 'C');
   t.add(6, payload_size, 'E', 'D');
   t.add(7, payload_size, 'F', 'E');
   t.add(8, payload_size, 'G', 'F');
   t.add(9, payload_size, 'H', 'G');
   t.check_n_bounce([&]() {
      t.check_range_present(2, 9);
   });

   //upgrade to pruned...
   t.conf = eosio::state_history::prune_config{ .prune_blocks = 4 };
   t.template check_n_bounce([]() {});

   t.check_n_bounce([&]() {
      t.check_range_present(6, 9);
   });
   t.add(10, payload_size, 'I', 'H');
   t.add(11, payload_size, 'J', 'I');
   t.add(12, payload_size, 'K', 'J');
   t.add(13, payload_size, 'L', 'K');
   t.check_n_bounce([&]() {
      t.check_range_present(10, 13);
   });

} FC_LOG_AND_RETHROW() }

BOOST_DATA_TEST_CASE(prune_to_non_prune, bdata::xrange(2) * bdata::xrange(2), enable_read, remove_index_on_reopen)  { try {
   ship_log_fixture t(enable_read, true, remove_index_on_reopen, false, 4);

   t.check_empty();
   size_t payload_size = larger_than_tmpfile_blocksize();

   t.add(2, payload_size, 'A', 'X');
   t.add(3, payload_size, 'B', 'A');
   t.add(4, payload_size, 'C', 'B');
   t.add(5, payload_size, 'D', 'C');
   t.add(6, payload_size, 'E', 'D');
   t.add(7, payload_size, 'F', 'E');
   t.add(8, payload_size, 'G', 'F');
   t.add(9, payload_size, 'H', 'G');
   t.check_n_bounce([&]() {
      t.check_range_present(6, 9);
   });

   //no more pruned
   t.conf = std::monostate{};
   t.template check_n_bounce([]() {});

   t.check_n_bounce([&]() {
      t.check_range_present(6, 9);
   });
   t.add(10, payload_size, 'I', 'H');
   t.add(11, payload_size, 'J', 'I');
   t.add(12, payload_size, 'K', 'J');
   t.add(13, payload_size, 'L', 'K');
   t.add(14, payload_size, 'M', 'L');
   t.add(15, payload_size, 'N', 'M');
   t.check_n_bounce([&]() {
      t.check_range_present(6, 15);
   });

} FC_LOG_AND_RETHROW() }

BOOST_DATA_TEST_CASE(prune_to_partitioned, bdata::xrange(2) * bdata::xrange(2), enable_read, remove_index_on_reopen)  { try {
   ship_log_fixture t(enable_read, true, remove_index_on_reopen, false, 4);

   t.check_empty();
   size_t payload_size = larger_than_tmpfile_blocksize();

   t.add(2, payload_size, 'A', 'A');
   t.add(3, payload_size, 'B', 'A');
   t.add(4, payload_size, 'C', 'B');
   t.add(5, payload_size, 'D', 'C');
   t.add(6, payload_size, 'E', 'D');
   t.add(7, payload_size, 'F', 'E');
   t.add(8, payload_size, 'G', 'F');
   t.add(9, payload_size, 'H', 'G');
   t.check_n_bounce([&]() {
      t.check_range_present(6, 9);
   });

   //no more pruned
   t.conf = eosio::state_history::partition_config{
       .stride  = 5
   };

   t.template check_n_bounce([]() {});

   t.check_n_bounce([&]() {
      t.check_range_present(6, 9);
   });
   t.add(10, payload_size, 'I', 'H');
   t.add(11, payload_size, 'J', 'I');
   t.add(12, payload_size, 'K', 'J');
   t.add(13, payload_size, 'L', 'K');
   t.add(14, payload_size, 'M', 'L');
   t.add(15, payload_size, 'N', 'M');
   t.check_n_bounce([&]() {
      t.check_range_present(6, 15);
   });

} FC_LOG_AND_RETHROW() }


BOOST_AUTO_TEST_SUITE_END()

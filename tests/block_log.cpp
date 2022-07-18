#include <boost/test/unit_test.hpp>
#include <boost/test/data/test_case.hpp>
#include <boost/test/data/monomorphic.hpp>
#include <boost/test/data/monomorphic/generators/xrange.hpp>

#include <fc/io/raw.hpp>
#include <fc/bitutil.hpp>
#include <fc/io/cfile.hpp>

#include <eosio/chain/block_log.hpp>
#include <eosio/chain/block.hpp>

namespace bdata = boost::unit_test::data;

struct block_log_fixture {
   block_log_fixture(bool enable_read, bool reopen_on_mark, bool remove_index_on_reopen, bool vacuum_on_exit_if_small, std::optional<uint32_t> prune_blocks) :
     enable_read(enable_read), reopen_on_mark(reopen_on_mark),
     remove_index_on_reopen(remove_index_on_reopen), vacuum_on_exit_if_small(vacuum_on_exit_if_small),
     prune_blocks(prune_blocks) {
      bounce();
   }

   fc::sha256 non_genesis_chain_id = fc::sha256::hash(std::string("spoon was here"));

   void startup(uint32_t first) {
      if(first > 1) {
         //bleh! but don't want to make a new friend in chain_id_type just for this test
         auto* chainid = reinterpret_cast<eosio::chain::chain_id_type*>(&non_genesis_chain_id);
         log->reset(*chainid, first);

         //let's go ahead and check that it's empty
         check_n_bounce([&]() {
            BOOST_REQUIRE(log->head() == nullptr);
            BOOST_REQUIRE(log->read_head() == eosio::chain::signed_block_ptr());
         });
      }
      else {
         eosio::chain::genesis_state gs;
         log->reset(gs, std::make_shared<eosio::chain::signed_block>());

         //in this case it's not really empty since the "genesis block" is present. These tests only
         // work because the default ctor of a block_header (used above) has previous 0'ed out which
         // means its block num is 1.
         check_n_bounce([&]() {
            BOOST_REQUIRE_EQUAL(log->first_block_num(), 1);
            BOOST_REQUIRE_EQUAL(log->head()->block_num(), 1);
            if(enable_read)
               BOOST_REQUIRE_EQUAL(log->read_block_by_num(1)->block_num(), 1);
         });
      }
   }

   void add(uint32_t index, size_t size, char fillchar) {
      std::vector<char> a;
      a.assign(size, fillchar);

      eosio::chain::signed_block_ptr p = std::make_shared<eosio::chain::signed_block>();
      p->previous._hash[0] = fc::endian_reverse_u32(index-1);
      p->header_extensions.push_back(std::make_pair<uint16_t, std::vector<char>>(0, std::vector<char>(a)));

      log->append(p, p->calculate_id());

      if(index + 1 > written_data.size())
         written_data.resize(index + 1);
      written_data.at(index) = a;
   }

   void check_range_present(uint32_t first, uint32_t last) {
      BOOST_REQUIRE_EQUAL(log->first_block_num(), first);
      BOOST_REQUIRE_EQUAL(eosio::chain::block_header::num_from_id(log->head_id()), last);
      if(enable_read) {
         for(auto i = first; i <= last; i++) {
            std::vector<char> buff;
            buff.resize(written_data.at(i).size());
            eosio::chain::signed_block_ptr p = log->read_block_by_num(i);
            if(i != 1) //don't check "genesis block"
               BOOST_REQUIRE(p->header_extensions.at(0).second == written_data.at(i));
         }
      }
   }

   void check_not_present(uint32_t index) {
      BOOST_REQUIRE(log->read_block_by_num(index) == nullptr);
   }

   template <typename F>
   void check_n_bounce(F&& f) {
      f();
      if(reopen_on_mark) {
         bounce();
         f();
      }
   }

   bool enable_read, reopen_on_mark, remove_index_on_reopen, vacuum_on_exit_if_small;
   std::optional<uint32_t> prune_blocks;
   fc::temp_directory dir;

   std::optional<eosio::chain::block_log> log;

   std::vector<std::vector<char>> written_data;

private:
   void bounce() {
      log.reset();
      if(remove_index_on_reopen)
         fc::remove(dir.path() / "blocks.index");
      std::optional<eosio::chain::block_log_prune_config> conf;
      if(prune_blocks) {
         conf.emplace();
         conf->prune_blocks = *prune_blocks;
         conf->prune_threshold = 8; //check to prune every 8 bytes; should guarantee always checking to prune for each block added
         if(vacuum_on_exit_if_small)
            conf->vacuum_on_close = 1024*1024*1024; //something large: will always vacuum on close for these small tests
      }
      log.emplace(dir.path(), conf);
   }
};

static size_t payload_size() {
   fc::temp_file tf;
   fc::cfile cf;
   cf.set_file_path(tf.path());
   cf.open("ab");
   return cf.filesystem_block_size()*2 + cf.filesystem_block_size()/2;
}

BOOST_AUTO_TEST_SUITE(blog_file_tests)

BOOST_DATA_TEST_CASE(basic_prune_test_genesis, bdata::xrange(2) * bdata::xrange(2) * bdata::xrange(2) * bdata::xrange(2),
                                               enable_read, reopen_on_mark, remove_index_on_reopen, vacuum_on_exit_if_small)  { try {
   block_log_fixture t(enable_read, reopen_on_mark, remove_index_on_reopen, vacuum_on_exit_if_small, 4);

   t.startup(1);

   t.add(2, payload_size(), 'A');
   t.check_n_bounce([&]() {
      t.check_range_present(1, 2);
   });

   t.add(3, payload_size(), 'B');
   t.add(4, payload_size(), 'C');
   t.check_n_bounce([&]() {
      t.check_range_present(1, 4);
   });

   t.add(5, payload_size(), 'D');
   t.check_n_bounce([&]() {
      t.check_range_present(2, 5);
   });

   t.add(6, payload_size(), 'E');
   t.check_n_bounce([&]() {
      t.check_range_present(3, 6);
   });

   t.add(7, payload_size(), 'F');
   t.add(8, payload_size(), 'G');
   t.add(9, payload_size(), 'H');
   t.check_n_bounce([&]() {
      t.check_range_present(6, 9);
   });
}  FC_LOG_AND_RETHROW() }

BOOST_DATA_TEST_CASE(basic_prune_test_nongenesis, bdata::xrange(2) * bdata::xrange(2) * bdata::xrange(2) * bdata::xrange(2),
                                                  enable_read, reopen_on_mark, remove_index_on_reopen, vacuum_on_exit_if_small)  { try {
   block_log_fixture t(enable_read, reopen_on_mark, remove_index_on_reopen, vacuum_on_exit_if_small, 4);

   t.startup(10);

   t.add(10, payload_size(), 'A');
   t.check_n_bounce([&]() {
      t.check_range_present(10, 10);
   });

   t.add(11, payload_size(), 'B');
   t.add(12, payload_size(), 'C');
   t.check_n_bounce([&]() {
      t.check_range_present(10, 12);
   });

   t.add(13, payload_size(), 'D');
   t.check_n_bounce([&]() {
      t.check_range_present(10, 13);
   });

   t.add(14, payload_size(), 'E');
   t.check_n_bounce([&]() {
      t.check_range_present(11, 14);
   });

   t.add(15, payload_size(), 'F');
   t.add(16, payload_size(), 'G');
   t.add(17, payload_size(), 'H');
   t.check_n_bounce([&]() {
      t.check_range_present(14, 17);
   });
}  FC_LOG_AND_RETHROW() }

//well we do let someone configure a single block prune; so let's make sure that works..
BOOST_DATA_TEST_CASE(single_prune_test_genesis, bdata::xrange(2) * bdata::xrange(2) * bdata::xrange(2) * bdata::xrange(2),
                                                enable_read, reopen_on_mark, remove_index_on_reopen, vacuum_on_exit_if_small)  { try {
   block_log_fixture t(enable_read, reopen_on_mark, remove_index_on_reopen, vacuum_on_exit_if_small, 1);

   t.startup(1);

   t.add(2, payload_size(), 'A');
   t.check_n_bounce([&]() {
      t.check_range_present(2, 2);
   });

   t.add(3, payload_size(), 'B');
   t.add(4, payload_size(), 'C');
   t.check_n_bounce([&]() {
      t.check_range_present(4, 4);
   });

}  FC_LOG_AND_RETHROW() }

BOOST_DATA_TEST_CASE(single_prune_test_nongenesis, bdata::xrange(2) * bdata::xrange(2) * bdata::xrange(2) * bdata::xrange(2),
                                                enable_read, reopen_on_mark, remove_index_on_reopen, vacuum_on_exit_if_small)  { try {
   block_log_fixture t(enable_read, reopen_on_mark, remove_index_on_reopen, vacuum_on_exit_if_small, 1);

   t.startup(10);

   t.add(10, payload_size(), 'A');
   t.check_n_bounce([&]() {
      t.check_range_present(10, 10);
   });

   t.add(11, payload_size(), 'B');
   t.add(12, payload_size(), 'C');
   t.check_n_bounce([&]() {
      t.check_range_present(12, 12);
   });

}  FC_LOG_AND_RETHROW() }

BOOST_DATA_TEST_CASE(nonprune_test_genesis, bdata::xrange(2) * bdata::xrange(2) * bdata::xrange(2),
                                            enable_read, reopen_on_mark, remove_index_on_reopen)  { try {
   block_log_fixture t(enable_read, reopen_on_mark, remove_index_on_reopen, false, std::optional<uint32_t>());

   t.startup(1);

   t.add(2, payload_size(), 'A');
   t.check_n_bounce([&]() {
      t.check_range_present(1, 2);
   });

   t.add(3, payload_size(), 'B');
   t.add(4, payload_size(), 'C');
   t.check_n_bounce([&]() {
      t.check_range_present(1, 4);
   });

   t.add(5, payload_size(), 'D');
   t.check_n_bounce([&]() {
      t.check_range_present(1, 5);
   });

   t.add(6, payload_size(), 'E');
   t.check_n_bounce([&]() {
      t.check_range_present(1, 6);
   });

   t.add(7, payload_size(), 'F');
   t.add(8, payload_size(), 'G');
   t.add(9, payload_size(), 'H');
   t.check_n_bounce([&]() {
      t.check_range_present(1, 9);
   });
}  FC_LOG_AND_RETHROW() }

BOOST_DATA_TEST_CASE(nonprune_test_nongenesis, bdata::xrange(2) * bdata::xrange(2) * bdata::xrange(2),
                                               enable_read, reopen_on_mark, remove_index_on_reopen)  { try {
   block_log_fixture t(enable_read, reopen_on_mark, remove_index_on_reopen, false, std::optional<uint32_t>());

   t.startup(10);

   t.add(10, payload_size(), 'A');
   t.check_n_bounce([&]() {
      t.check_range_present(10, 10);
   });

   t.add(11, payload_size(), 'B');
   t.add(12, payload_size(), 'C');
   t.check_n_bounce([&]() {
      t.check_range_present(10, 12);
   });

   t.add(13, payload_size(), 'D');
   t.check_n_bounce([&]() {
      t.check_range_present(10, 13);
   });

   t.add(14, payload_size(), 'E');
   t.check_n_bounce([&]() {
      t.check_range_present(10, 14);
   });

   t.add(15, payload_size(), 'F');
   t.add(16, payload_size(), 'G');
   t.add(17, payload_size(), 'H');
   t.check_n_bounce([&]() {
      t.check_range_present(10, 17);
   });
}  FC_LOG_AND_RETHROW() }

//the important part of this test is that we transition to a pruned log that still has the genesis state after transition to pruned
// and then try vacuuming it in both cases where it remains a genesis_state and gets converted to a chainid. basically we want to feel
// around in convert_existing_header_to_vacuumed()
BOOST_DATA_TEST_CASE(non_prune_to_prune_genesis, bdata::xrange(2) * bdata::xrange(2), enable_read, remove_index_on_reopen)  { try {
   block_log_fixture t(enable_read, true, remove_index_on_reopen, false, std::optional<uint32_t>());

   t.startup(1);

   t.add(2, payload_size(), 'A');
   t.add(3, payload_size(), 'B');
   t.add(4, payload_size(), 'C');
   t.check_n_bounce([&]() {
      t.check_range_present(1, 4);
   });

   t.prune_blocks = 10;
   t.check_n_bounce([&]() {});

   //we're now a pruned log with genesis state at the front still; however we didn't actually prune any entries
   t.check_range_present(1, 4);

   t.add(5, payload_size(), 'D');
   t.add(6, payload_size(), 'E');
   t.check_n_bounce([&]() {
      t.check_range_present(1, 6);
   });

   t.prune_blocks.reset();
   t.check_n_bounce([&]() {});
   //we've just been converted back to a non-pruned log. But since we never pruned any blocks, the front of the log
   // should still have the genesis_state
   t.check_range_present(1, 6);

   //not really sure what to do here other then try and read in the genesis state from the file manually
   {
      fc::cfile f;
      f.set_file_path(t.dir.path() / "blocks.log");
      f.open("rb");
      fc::cfile_datastream ds(f);

      uint32_t version;
      uint32_t first_block;
      eosio::chain::genesis_state gs;
      fc::raw::unpack(ds, version);
      fc::raw::unpack(ds, first_block);
      fc::raw::unpack(ds, gs);
      BOOST_REQUIRE(gs == eosio::chain::genesis_state());
   }

   t.add(7, payload_size(), 'F');

   t.prune_blocks = 10;
   //back to pruned mode..
   t.check_n_bounce([&]() {});
   t.check_range_present(1, 7);

   t.add(8, payload_size(), 'G');
   t.add(9, payload_size(), 'H');
   t.add(10, payload_size(), 'I');
   t.add(11, payload_size(), 'J');
   t.add(12, payload_size(), 'K');

   //and now we did prune some blocks while in prune mode
   t.check_range_present(3, 12);

   //so now when we vacuum there will be a transition from the log starting with a genesis_state to a chain_id
   t.prune_blocks.reset();
   t.check_n_bounce([&]() {});

   t.check_range_present(3, 12);

   //once again let's just read it in
   {
      fc::cfile f;
      f.set_file_path(t.dir.path() / "blocks.log");
      f.open("rb");
      fc::cfile_datastream ds(f);

      uint32_t version;
      uint32_t first_block;
      fc::sha256 cid;
      fc::raw::unpack(ds, version);
      fc::raw::unpack(ds, first_block);
      fc::raw::unpack(ds, cid);
      BOOST_REQUIRE(cid == eosio::chain::genesis_state().compute_chain_id());
   }

} FC_LOG_AND_RETHROW() }

//simpler than above, we'll start out with a non-genesis log and just make sure after pruning the chainid is still what we expect
BOOST_DATA_TEST_CASE(non_prune_to_prune_nongenesis, bdata::xrange(2) * bdata::xrange(2), enable_read, remove_index_on_reopen)  { try {
   block_log_fixture t(enable_read, true, remove_index_on_reopen, false, std::optional<uint32_t>());

   t.startup(10);

   t.add(10, payload_size(), 'A');
   t.add(11, payload_size(), 'B');
   t.add(12, payload_size(), 'C');
   t.check_n_bounce([&]() {
      t.check_range_present(10, 12);
   });

   t.prune_blocks = 10;
   t.check_n_bounce([&]() {});

   //once again, we're a prune-mode log but no entries have been pruned
   t.check_range_present(10, 12);

   t.add(13, payload_size(), 'D');
   t.add(14, payload_size(), 'E');
   t.check_n_bounce([&]() {
      t.check_range_present(10, 14);
   });

   t.prune_blocks.reset();
   t.check_n_bounce([&]() {});
   //back to non-prune mode
   t.check_range_present(10, 14);

   //check that chainid is still in the log file
   {
      fc::cfile f;
      f.set_file_path(t.dir.path() / "blocks.log");
      f.open("rb");
      fc::cfile_datastream ds(f);

      uint32_t version;
      uint32_t first_block;
      fc::sha256 cid;
      fc::raw::unpack(ds, version);
      fc::raw::unpack(ds, first_block);
      fc::raw::unpack(ds, cid);
      BOOST_REQUIRE(cid == t.non_genesis_chain_id);
   }

   t.add(15, payload_size(), 'F');

   t.prune_blocks = 10;
   //back to pruned mode..
   t.check_n_bounce([&]() {});
   t.check_range_present(10, 15);

   t.add(16, payload_size(), 'G');
   t.add(17, payload_size(), 'H');
   t.add(18, payload_size(), 'I');
   t.add(19, payload_size(), 'J');
   t.add(20, payload_size(), 'K');
   t.check_range_present(11, 20);

   //now we'll be moving some blocks around! but, chainid will just stay the same
   t.prune_blocks.reset();
   t.check_n_bounce([&]() {});

   t.check_range_present(11, 20);

   //check that chainid is still in the log file
   {
      fc::cfile f;
      f.set_file_path(t.dir.path() / "blocks.log");
      f.open("rb");
      fc::cfile_datastream ds(f);

      uint32_t version;
      uint32_t first_block;
      fc::sha256 cid;
      fc::raw::unpack(ds, version);
      fc::raw::unpack(ds, first_block);
      fc::raw::unpack(ds, cid);
      BOOST_REQUIRE(cid == t.non_genesis_chain_id);
   }

} FC_LOG_AND_RETHROW() }

BOOST_DATA_TEST_CASE(empty_nonprune_to_prune_transitions, bdata::xrange(2) * bdata::xrange(1, 11, 9), remove_index_on_reopen, starting_block)  { try {
   //start non pruned
   block_log_fixture t(false, true, remove_index_on_reopen, false, std::optional<uint32_t>());
   t.startup(starting_block);

   //pruned mode..
   t.prune_blocks = 5;
   t.check_n_bounce([&]() {});
   if(starting_block == 1) {
      t.check_range_present(1, 1);
      t.check_not_present(2);
   }
   else
      t.check_not_present(starting_block);

   //vacuum back to non-pruned
   t.prune_blocks.reset();
   t.check_n_bounce([&]() {});
   if(starting_block == 1) {
      t.check_range_present(1, 1);
      t.check_not_present(2);
   }
   else
      t.check_not_present(starting_block);
}  FC_LOG_AND_RETHROW() }

BOOST_DATA_TEST_CASE(empty_prune_to_nonprune_transitions, bdata::xrange(2) * bdata::xrange(1, 11, 9), remove_index_on_reopen, starting_block)  { try {
   //start pruned
   block_log_fixture t(false, true, remove_index_on_reopen, false, 5);
   t.startup(starting_block);

   //vacuum back to non-pruned
   t.prune_blocks.reset();
   t.check_n_bounce([&]() {});
   if(starting_block == 1) {
      t.check_range_present(1, 1);
      t.check_not_present(2);
   }
   else
      t.check_not_present(starting_block);

   //and back to pruned
   t.prune_blocks = 5;
   t.check_n_bounce([&]() {});
   if(starting_block == 1) {
      t.check_range_present(1, 1);
      t.check_not_present(2);
   }
   else
      t.check_not_present(starting_block);
}  FC_LOG_AND_RETHROW() }

// Test when prune_blocks is set to 0, no block log is generated
BOOST_DATA_TEST_CASE(no_block_log_basic_genesis, bdata::xrange(2) * bdata::xrange(2) * bdata::xrange(2) * bdata::xrange(2),
                                               enable_read, reopen_on_mark, remove_index_on_reopen, vacuum_on_exit_if_small)  { try {
   // set enable_read to false: when it is true, startup calls
   // log->read_block_by_num which always returns null when block log does not exist.
   // set reopen_on_mark to false: when it is ture, check_n_bounce resets block
   // object but does not reinitialze.
   block_log_fixture t(false, false, remove_index_on_reopen, vacuum_on_exit_if_small, 0);

   t.startup(1);

   t.add(2, payload_size(), 'A');
   t.check_not_present(2);

   t.add(3, payload_size(), 'B');
   t.add(4, payload_size(), 'C');
   t.check_not_present(3);
   t.check_not_present(4);

   t.add(5, payload_size(), 'D');
   t.check_not_present(5);
}  FC_LOG_AND_RETHROW() }

// Test when prune_blocks is set to 0, no block log is generated
BOOST_DATA_TEST_CASE(no_block_log_basic_nongenesis, bdata::xrange(2) * bdata::xrange(2) * bdata::xrange(2) * bdata::xrange(2),
                                               enable_read, reopen_on_mark, remove_index_on_reopen, vacuum_on_exit_if_small)  { try {
   block_log_fixture t(enable_read, reopen_on_mark, remove_index_on_reopen, vacuum_on_exit_if_small, 0);

   t.startup(10);

   t.add(10, payload_size(), 'A');
   t.check_not_present(10);

   t.add(11, payload_size(), 'B');
   t.add(12, payload_size(), 'C');
   t.check_not_present(11);
   t.check_not_present(12);

   t.add(13, payload_size(), 'D');
   t.check_not_present(13);
}  FC_LOG_AND_RETHROW() }

void no_block_log_public_functions_test( block_log_fixture& t) {
   BOOST_REQUIRE_NO_THROW(t.log->flush());
   BOOST_REQUIRE(t.log->read_block(1) == nullptr);
   BOOST_REQUIRE_NO_THROW(
      eosio::chain::block_header bh;
      t.log->read_block_header(bh, 1);
   );
   BOOST_REQUIRE(t.log->read_block_by_num(1) == nullptr);
   BOOST_REQUIRE(t.log->read_block_id_by_num(1) == eosio::chain::block_id_type{});
   BOOST_REQUIRE(t.log->get_block_pos(1) == eosio::chain::block_log::npos);
   BOOST_REQUIRE(t.log->read_head() == nullptr);
}

// Test when prune_blocks is set to 0, block_log's public methods work
BOOST_DATA_TEST_CASE(no_block_log_public_functions_genesis, bdata::xrange(2) * bdata::xrange(2) * bdata::xrange(2) * bdata::xrange(2),
                                               enable_read, reopen_on_mark, remove_index_on_reopen, vacuum_on_exit_if_small)  { try {
   block_log_fixture t(false, false, remove_index_on_reopen, vacuum_on_exit_if_small, 0);

   t.startup(1);
   no_block_log_public_functions_test(t);
}  FC_LOG_AND_RETHROW() }

// Test when prune_blocks is set to 0, block_log's public methods work
BOOST_DATA_TEST_CASE(no_block_log_public_functions_nogenesis, bdata::xrange(2) * bdata::xrange(2) * bdata::xrange(2) * bdata::xrange(2),
                                               enable_read, reopen_on_mark, remove_index_on_reopen, vacuum_on_exit_if_small)  { try {
   block_log_fixture t(enable_read, reopen_on_mark, remove_index_on_reopen, vacuum_on_exit_if_small, 0);

   t.startup(10);
   no_block_log_public_functions_test(t);
}  FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()

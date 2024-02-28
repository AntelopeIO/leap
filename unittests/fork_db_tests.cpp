#include <eosio/chain/types.hpp>
#include <eosio/chain/fork_database.hpp>
#include <eosio/testing/tester.hpp>
#include <fc/bitutil.hpp>
#include <boost/test/unit_test.hpp>


namespace eosio::chain {

inline block_id_type make_block_id(block_num_type block_num) {
   static uint32_t nonce = 0;
   ++nonce;
   block_id_type id = fc::sha256::hash(std::to_string(block_num) + "-" + std::to_string(nonce));
   id._hash[0] &= 0xffffffff00000000;
   id._hash[0] += fc::endian_reverse_u32(block_num); // store the block num in the ID, 160 bits is plenty for the hash
   return id;
}

// Used to access privates of block_state
struct block_state_accessor {
   static auto make_genesis_block_state() {
      block_state_ptr root = std::make_shared<block_state>();
      block_id_type genesis_id = make_block_id(10);
      root->block_id = genesis_id;
      root->header.timestamp = block_timestamp_type{10};
      root->core = finality_core::create_core_for_genesis_block(10);
      root->best_qc_claim = {.block_num = 10, .is_strong_qc = false};
      return root;
   }

   // use block_num > 10
   static auto make_unique_block_state(block_num_type block_num, const block_state_ptr& prev) {
      block_state_ptr bsp = std::make_shared<block_state>();
      bsp->block_id = make_block_id(block_num);
      bsp->header.timestamp.slot = prev->header.timestamp.slot + 1;
      bsp->header.previous = prev->id();
      block_ref parent_block {
         .block_id  = prev->id(),
         .timestamp = prev->timestamp()
      };
      bsp->core = prev->core.next(parent_block, prev->best_qc_claim);
      bsp->best_qc_claim = bsp->core.latest_qc_claim();
      bsp->updated_core = bsp->core.next_metadata(bsp->best_qc_claim);
      return bsp;
   }

   static auto get_best_qc_claim(const block_state_ptr& bs) {
      return bs->best_qc_claim;
   }
};

} // namespace eosio::chain

using namespace eosio::chain;

BOOST_AUTO_TEST_SUITE(fork_database_tests)

BOOST_AUTO_TEST_CASE(update_best_qc) try {
   fork_database_if_t forkdb;

   // Setup fork database with blocks based on a root of block 10
   // Add a number of forks in the fork database
   auto root = block_state_accessor::make_genesis_block_state();
   auto   bsp11a = block_state_accessor::make_unique_block_state(11, root);
   auto     bsp12a = block_state_accessor::make_unique_block_state(12, bsp11a);
   auto       bsp13a = block_state_accessor::make_unique_block_state(13, bsp12a);
   auto   bsp11b = block_state_accessor::make_unique_block_state(11, root);
   auto     bsp12b = block_state_accessor::make_unique_block_state(12, bsp11b);
   auto       bsp13b = block_state_accessor::make_unique_block_state(13, bsp12b);
   auto         bsp14b = block_state_accessor::make_unique_block_state(14, bsp13b);
   auto     bsp12bb = block_state_accessor::make_unique_block_state(12, bsp11b);
   auto       bsp13bb = block_state_accessor::make_unique_block_state(13, bsp12bb);
   auto       bsp13bbb = block_state_accessor::make_unique_block_state(13, bsp12bb);
   auto     bsp12bbb = block_state_accessor::make_unique_block_state(12, bsp11b);
   auto   bsp11c = block_state_accessor::make_unique_block_state(11, root);
   auto     bsp12c = block_state_accessor::make_unique_block_state(12, bsp11c);
   auto       bsp13c = block_state_accessor::make_unique_block_state(13, bsp12c);

   // keep track of all those added for easy verification
   std::vector<block_state_ptr> all { bsp11a, bsp12a, bsp13a, bsp11b, bsp12b, bsp12bb, bsp12bbb, bsp13b, bsp13bb, bsp13bbb, bsp14b, bsp11c, bsp12c, bsp13c };

   forkdb.reset_root(*root);
   forkdb.add(bsp11a, mark_valid_t::no, ignore_duplicate_t::no);
   forkdb.add(bsp11b, mark_valid_t::no, ignore_duplicate_t::no);
   forkdb.add(bsp11c, mark_valid_t::no, ignore_duplicate_t::no);
   forkdb.add(bsp12a, mark_valid_t::no, ignore_duplicate_t::no);
   forkdb.add(bsp13a, mark_valid_t::no, ignore_duplicate_t::no);
   forkdb.add(bsp12b, mark_valid_t::no, ignore_duplicate_t::no);
   forkdb.add(bsp12bb, mark_valid_t::no, ignore_duplicate_t::no);
   forkdb.add(bsp12bbb, mark_valid_t::no, ignore_duplicate_t::no);
   forkdb.add(bsp12c, mark_valid_t::no, ignore_duplicate_t::no);
   forkdb.add(bsp13b, mark_valid_t::no, ignore_duplicate_t::no);
   forkdb.add(bsp13bb, mark_valid_t::no, ignore_duplicate_t::no);
   forkdb.add(bsp13bbb, mark_valid_t::no, ignore_duplicate_t::no);
   forkdb.add(bsp14b, mark_valid_t::no, ignore_duplicate_t::no);
   forkdb.add(bsp13c, mark_valid_t::no, ignore_duplicate_t::no);

   // test get_block
   for (auto& i : all) {
      BOOST_TEST(forkdb.get_block(i->id()) == i);
   }

   // test remove, should remove descendants
   forkdb.remove(bsp12b->id());
   BOOST_TEST(!forkdb.get_block(bsp12b->id()));
   BOOST_TEST(!forkdb.get_block(bsp13b->id()));
   BOOST_TEST(!forkdb.get_block(bsp14b->id()));
   forkdb.add(bsp12b, mark_valid_t::no, ignore_duplicate_t::no); // will throw if already exists
   forkdb.add(bsp13b, mark_valid_t::no, ignore_duplicate_t::no); // will throw if already exists
   forkdb.add(bsp14b, mark_valid_t::no, ignore_duplicate_t::no); // will throw if already exists

   // test update_best_qc, should update descendants
   BOOST_TEST(block_state_accessor::get_best_qc_claim(bsp11b).block_num == 10);
   BOOST_TEST(block_state_accessor::get_best_qc_claim(bsp12b).block_num == 10);
   forkdb.update_best_qc(bsp11b->id(), {.block_num = 11, .is_strong_qc = false});
   BOOST_TEST(block_state_accessor::get_best_qc_claim(bsp12b).block_num == 11);
   BOOST_TEST(block_state_accessor::get_best_qc_claim(bsp12b).is_strong_qc == false);
   BOOST_TEST(block_state_accessor::get_best_qc_claim(bsp13b).block_num == 11);
   BOOST_TEST(block_state_accessor::get_best_qc_claim(bsp14b).block_num == 11);
   BOOST_TEST(block_state_accessor::get_best_qc_claim(bsp12bb).block_num == 11);
   BOOST_TEST(block_state_accessor::get_best_qc_claim(bsp13bb).block_num == 11);
   BOOST_TEST(block_state_accessor::get_best_qc_claim(bsp13bbb).block_num == 11);
   BOOST_TEST(block_state_accessor::get_best_qc_claim(bsp12bbb).block_num == 11);
   forkdb.update_best_qc(bsp13bb->id(), {.block_num = 11, .is_strong_qc = true});
   BOOST_TEST(block_state_accessor::get_best_qc_claim(bsp13bb).block_num == 11);
   BOOST_TEST(block_state_accessor::get_best_qc_claim(bsp13bb).is_strong_qc == true);
   forkdb.update_best_qc(bsp11b->id(), {.block_num = 11, .is_strong_qc = true});
   BOOST_TEST(block_state_accessor::get_best_qc_claim(bsp12b).is_strong_qc == true);
   BOOST_TEST(block_state_accessor::get_best_qc_claim(bsp13bbb).is_strong_qc == true);

} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_SUITE_END()

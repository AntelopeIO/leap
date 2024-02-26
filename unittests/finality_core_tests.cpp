#include <eosio/chain/finality_core.hpp>
#include <fc/crypto/sha256.hpp>
#include <fc/bitutil.hpp>
#include <boost/test/unit_test.hpp>

using namespace eosio::chain;

struct test_core {
   finality_core   core;
   block_time_type timestamp;

   test_core() {
      core = finality_core::create_core_for_genesis_block(0);

      next(0, qc_claim_t{.block_num = 0, .is_strong_qc = true});
      verify_post_conditions(0, 0);
      // block 1 -- last_final_block_num: 0, final_on_strong_qc_block_num: 0 

      next(1, qc_claim_t{.block_num = 1, .is_strong_qc = true});
      verify_post_conditions(0, 0);
      // block 2 -- last_final_block_num: 0, final_on_strong_qc_block_num: 0 

      // Make a strong qc_claim on block 2.
      // block 2 has a strong qc_claim on block 1, which makes final_on_strong_qc_block_num 1;
      // block 1 has a qc_claim on block 0, which makes last_final_block_num 0
      next(2, qc_claim_t{.block_num = 2, .is_strong_qc = true});
      verify_post_conditions(0, 1);
      // block 3 -- last_final_block_num: 0, final_on_strong_qc_block_num: 1 

      // Make a strong QC claim on block 3.
      // block 3 has a strong qc_claim on block 2, which makes final_on_strong_qc_block_num 2;
      // block 2 has a qc_claim on block 1, which makes last_final_block_num 1
      next(3, qc_claim_t{.block_num = 3, .is_strong_qc = true});
      verify_post_conditions(1, 2);
   }

   void next(block_num_type curr_block_num, qc_claim_t qc_claim) {
      timestamp = timestamp.next();
      core = core.next(
         block_ref {.block_id = id_from_num(curr_block_num), .timestamp = timestamp},
         qc_claim);
      // next block num is current block number + 1, qc_claim becomes latest_qc_claim
      BOOST_REQUIRE_EQUAL(core.current_block_num(), curr_block_num + 1);
      BOOST_REQUIRE(core.latest_qc_claim() == qc_claim);
   }

   void verify_post_conditions( block_num_type expected_last_final_block_num,
                                block_num_type expected_final_on_strong_qc_block_num) {
      BOOST_REQUIRE_EQUAL(core.last_final_block_num(), expected_last_final_block_num);
      BOOST_REQUIRE_EQUAL(core.final_on_strong_qc_block_num, expected_final_on_strong_qc_block_num);
   }

   // This function is intentionally simplified for tests here only.
   block_id_type id_from_num(block_num_type block_num) {
      block_id_type result;
      result._hash[0] &= 0xffffffff00000000;
      result._hash[0] += fc::endian_reverse_u32(block_num);
      return result;
   }
};

BOOST_AUTO_TEST_SUITE(finality_core_tests)

// Verify post conditions of IF genesis block core
BOOST_AUTO_TEST_CASE(create_core_for_genesis_block_test) { try {
   finality_core core = finality_core::create_core_for_genesis_block(0);

   BOOST_REQUIRE_EQUAL(core.current_block_num(), 0u);
   qc_claim_t qc_claim{.block_num=0, .is_strong_qc=false};
   BOOST_REQUIRE(core.latest_qc_claim() == qc_claim);
   BOOST_REQUIRE_EQUAL(core.final_on_strong_qc_block_num, 0u);
   BOOST_REQUIRE_EQUAL(core.last_final_block_num(), 0u);
} FC_LOG_AND_RETHROW() }

// verify straight strong qc claims work
BOOST_AUTO_TEST_CASE(strong_qc_claim_test) { try {
   {
      test_core core;
      // post conditions of core::
      // current_block_num() == 4,
      // last_final_block_num() == 1,
      // final_on_strong_qc_block_num == 2
      // latest qc_claim == {"block_num":3,"is_strong_qc":true}

      // Strong QC claim on block 3 is the same as the latest qc_claim;
      // Nothing changes.
      core.next(4, qc_claim_t{.block_num = 3, .is_strong_qc = true });
      core.verify_post_conditions(1, 2);
   }
   {
      test_core core;

      // strong QC claim on block 4 will addvance LIB to 2
      core.next(4, qc_claim_t{.block_num = 4, .is_strong_qc = true });
      core.verify_post_conditions(2, 3);

      // strong QC claim on block 5 will addvance LIB to 2
      core.next(5, qc_claim_t{.block_num = 5, .is_strong_qc = true });
      core.verify_post_conditions(3, 4);
   }
} FC_LOG_AND_RETHROW() }

// verify blocks b4, b5 and b6 have same qc claims on b3 and then a qc claim on b4
BOOST_AUTO_TEST_CASE(same_strong_qc_claim_test_1) { try {
   test_core core;
   // post conditions of core::
   // current_block_num() == 4,
   // last_final_block_num() == 1,
   // final_on_strong_qc_block_num == 2
   // latest qc_claim == {"block_num":3,"is_strong_qc":true}

   // same QC claim on block 3 will not addvance last_final_block_num
   core.next(4, qc_claim_t{.block_num = 3, .is_strong_qc = true });
   core.verify_post_conditions(1, 2);

   // same QC claim on block 3 will not addvance last_final_block_num
   core.next(5, qc_claim_t{.block_num = 3, .is_strong_qc = true });
   core.verify_post_conditions(1, 2);

   // strong QC claim on block 4.
   core.next(6, qc_claim_t{.block_num = 4, .is_strong_qc = true });
   core.verify_post_conditions(2, 3);

   core.next(7, qc_claim_t{.block_num = 5, .is_strong_qc = true });
   core.verify_post_conditions(2, 3);

   core.next(8, qc_claim_t{.block_num = 6, .is_strong_qc = true });
   core.verify_post_conditions(2, 3);

   core.next(9, qc_claim_t{.block_num = 7, .is_strong_qc = true });
   core.verify_post_conditions(3, 4);
} FC_LOG_AND_RETHROW() }

// verify blocks b4, b5 and b6 have same strong qc claims on b3 and
// then a qc claim on b5 (b4 is skipped)
BOOST_AUTO_TEST_CASE(same_strong_qc_claim_test_2) { try {
   test_core core;
   // post conditions of core::
   // current_block_num() == 4,
   // last_final_block_num() == 1,
   // final_on_strong_qc_block_num == 2
   // latest qc_claim == {"block_num":3,"is_strong_qc":true}

   // same QC claim on block 3 will not addvance last_final_block_num
   core.next(4, qc_claim_t{.block_num = 3, .is_strong_qc = true });
   core.verify_post_conditions(1, 2);

   // same QC claim on block 3 will not addvance last_final_block_num
   core.next(5, qc_claim_t{.block_num = 3, .is_strong_qc = true });
   core.verify_post_conditions(1, 2);

   // Skip qc claim on block 4. Make a strong QC claim on block 5.
   core.next(6, qc_claim_t{.block_num = 5, .is_strong_qc = true });
   core.verify_post_conditions(2, 3);

   // A new qc claim advances last_final_block_num
   core.next(7, qc_claim_t{.block_num = 7, .is_strong_qc = true });
   core.verify_post_conditions(3, 5);
} FC_LOG_AND_RETHROW() }

// verify blocks b4, b5 and b6 have same strong qc claims on b3 and then
// a qc claim on b6 (b4 and b5 is skipped)
BOOST_AUTO_TEST_CASE(same_strong_qc_claim_test_3) { try {
   test_core core;
   // post conditions of core::
   // current_block_num() == 4,
   // last_final_block_num() == 1,
   // final_on_strong_qc_block_num == 2
   // latest qc_claim == {"block_num":3,"is_strong_qc":true}

   // same QC claim on block 3 will not addvance last_final_block_num
   core.next(4, qc_claim_t{.block_num = 3, .is_strong_qc = true });
   core.verify_post_conditions(1, 2);

   // same QC claim on block 3 will not addvance last_final_block_num
   core.next(5, qc_claim_t{.block_num = 3, .is_strong_qc = true });
   core.verify_post_conditions(1, 2);

   // Skip qc claim on block 4, 5. Make a strong QC claim on block 6.
   core.next(6, qc_claim_t{.block_num = 6, .is_strong_qc = true });
   core.verify_post_conditions(2, 3);
} FC_LOG_AND_RETHROW() }

// verify blocks b5, b6 and b7 have same weak qc claims on b4 and then
// b8 has a strong qc claim on b4
BOOST_AUTO_TEST_CASE(same_weak_qc_claim_test_1) { try {
   test_core core;
   // post conditions of core::
   // current_block_num() == 4,
   // last_final_block_num() == 1,
   // final_on_strong_qc_block_num == 2
   // latest qc_claim == {"block_num":3,"is_strong_qc":true}

   // weak QC claim on block 4; nothing changes
   core.next(4, qc_claim_t{.block_num = 4, .is_strong_qc = false });
   core.verify_post_conditions(1, 2);

   // same weak QC claim on block 4; nothing changes
   core.next(5, qc_claim_t{.block_num = 4, .is_strong_qc = false });
   core.verify_post_conditions(1, 2);

   // same weak QC claim on block 4; nothing changes
   core.next(6, qc_claim_t{.block_num = 4, .is_strong_qc = false });
   core.verify_post_conditions(1, 2);

   // strong QC claim on block 4
   core.next(7, qc_claim_t{.block_num = 4, .is_strong_qc = true });
   core.verify_post_conditions(2, 3);

   core.next(8, qc_claim_t{.block_num = 5, .is_strong_qc = true });
   core.verify_post_conditions(2, 4);

   core.next(9, qc_claim_t{.block_num = 6, .is_strong_qc = true });
   core.verify_post_conditions(2, 4);

   core.next(10, qc_claim_t{.block_num = 7, .is_strong_qc = true });
   core.verify_post_conditions(2, 4);

   core.next(11, qc_claim_t{.block_num = 8, .is_strong_qc = true });
   core.verify_post_conditions(3, 4);

   core.next(12, qc_claim_t{.block_num = 9, .is_strong_qc = true });
   core.verify_post_conditions(4, 5);
} FC_LOG_AND_RETHROW() }

// verify blocks b5, b6 and b7 have same weak qc claims on b4 and then
// b8 has a strong qc claim on b5
BOOST_AUTO_TEST_CASE(same_weak_qc_claim_test_2) { try {
   test_core core;
   // post conditions of core::
   // current_block_num() == 4,
   // last_final_block_num() == 1,
   // final_on_strong_qc_block_num == 2
   // latest qc_claim == {"block_num":3,"is_strong_qc":true}

   // weak QC claim on block 4; nothing changes
   core.next(4, qc_claim_t{.block_num = 4, .is_strong_qc = false });
   core.verify_post_conditions(1, 2);

   // same weak QC claim on block 4; nothing changes
   core.next(5, qc_claim_t{.block_num = 4, .is_strong_qc = false });
   core.verify_post_conditions(1, 2);

   // same weak QC claim on block 4; nothing changes
   core.next(6, qc_claim_t{.block_num = 4, .is_strong_qc = false });
   core.verify_post_conditions(1, 2);

   // strong QC claim on block 5
   core.next(7, qc_claim_t{.block_num = 5, .is_strong_qc = true });
   core.verify_post_conditions(1, 4);

   core.next(8, qc_claim_t{.block_num = 6, .is_strong_qc = true });
   core.verify_post_conditions(1, 4);

   core.next(9, qc_claim_t{.block_num = 7, .is_strong_qc = true });
   core.verify_post_conditions(1, 4);

   core.next(10, qc_claim_t{.block_num = 8, .is_strong_qc = true });
   core.verify_post_conditions(4, 5);

   core.next(11, qc_claim_t{.block_num = 9, .is_strong_qc = true });
   core.verify_post_conditions(4, 6);

   core.next(12, qc_claim_t{.block_num = 10, .is_strong_qc = true });
   core.verify_post_conditions(4, 7);

   core.next(13, qc_claim_t{.block_num = 11, .is_strong_qc = true });
   core.verify_post_conditions(5, 8);
} FC_LOG_AND_RETHROW() }

// verify blocks b5, b6 and b7 have same weak qc claims on b4 and then
// b8 has a strong qc claim on b6
BOOST_AUTO_TEST_CASE(same_weak_qc_claim_test_3) { try {
   test_core core;
   // post conditions of core::
   // current_block_num() == 4,
   // last_final_block_num() == 1,
   // final_on_strong_qc_block_num == 2
   // latest qc_claim == {"block_num":3,"is_strong_qc":true}

   // weak QC claim on block 4; nothing changes
   core.next(4, qc_claim_t{.block_num = 4, .is_strong_qc = false });
   core.verify_post_conditions(1, 2);

   // same weak QC claim on block 4; nothing changes
   core.next(5, qc_claim_t{.block_num = 4, .is_strong_qc = false });
   core.verify_post_conditions(1, 2);

   // same weak QC claim on block 4; nothing changes
   core.next(6, qc_claim_t{.block_num = 4, .is_strong_qc = false });
   core.verify_post_conditions(1, 2);

   // strong QC claim on block 6
   core.next(7, qc_claim_t{.block_num = 6, .is_strong_qc = true });
   core.verify_post_conditions(1, 4);

   core.next(8, qc_claim_t{.block_num = 7, .is_strong_qc = true });
   core.verify_post_conditions(1, 4);

   core.next(9, qc_claim_t{.block_num = 8, .is_strong_qc = true });
   core.verify_post_conditions(4, 6);

   core.next(10, qc_claim_t{.block_num = 9, .is_strong_qc = true });
   core.verify_post_conditions(4, 7);

   core.next(11, qc_claim_t{.block_num = 10, .is_strong_qc = true });
   core.verify_post_conditions(6, 8);

   core.next(12, qc_claim_t{.block_num = 11, .is_strong_qc = true });
   core.verify_post_conditions(7, 9);
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()

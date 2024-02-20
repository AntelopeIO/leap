#warning ToDo: adapt those tests to new core
#if 0
#include <eosio/chain/block_header_state_legacy.hpp>
#include <eosio/chain/block_header_state.hpp>

#include <boost/test/unit_test.hpp>

using namespace eosio::chain;

BOOST_AUTO_TEST_SUITE(block_header_state_tests)

// test for block_header_state_core constructor
BOOST_AUTO_TEST_CASE(block_header_state_core_constructor_test)
{
   // verifies members are constructed correctly
   block_header_state_core bhs_core1(1, 2, 3);
   BOOST_REQUIRE_EQUAL(bhs_core1.last_final_block_num, 1u);
   BOOST_REQUIRE_EQUAL(*bhs_core1.final_on_strong_qc_block_num, 2u);
   BOOST_REQUIRE_EQUAL(*bhs_core1.last_qc_block_num, 3u);

   // verifies optional arguments work as expected
   block_header_state_core bhs_core2(10, std::nullopt, {});
   BOOST_REQUIRE_EQUAL(bhs_core2.last_final_block_num, 10u);
   BOOST_REQUIRE(!bhs_core2.final_on_strong_qc_block_num.has_value());
   BOOST_REQUIRE(!bhs_core2.last_qc_block_num.has_value());
}

// comprehensive state transition test
BOOST_AUTO_TEST_CASE(block_header_state_core_state_transition_test)
{
   constexpr auto old_last_final_block_num = 1u;
   constexpr auto old_final_on_strong_qc_block_num = 2u;
   constexpr auto old_last_qc_block_num = 3u;
   const block_timestamp_type old_last_qc_block_timestamp(3);
   block_header_state_core old_bhs_core(old_last_final_block_num, old_final_on_strong_qc_block_num, old_last_qc_block_num);

   // verifies the state is kept the same when old last_final_block_num
   // and new last_final_block_num are the same
   for (bool is_last_qc_strong: { true, false }) {
      auto new_bhs_core = old_bhs_core.next({old_last_qc_block_num, old_last_qc_block_timestamp, is_last_qc_strong});
      BOOST_REQUIRE_EQUAL(new_bhs_core.last_final_block_num, old_bhs_core.last_final_block_num);
      BOOST_REQUIRE_EQUAL(*new_bhs_core.final_on_strong_qc_block_num, *old_bhs_core.final_on_strong_qc_block_num);
      BOOST_REQUIRE_EQUAL(*new_bhs_core.last_qc_block_num, *old_bhs_core.last_qc_block_num);
      BOOST_REQUIRE(new_bhs_core.last_qc_block_timestamp == old_bhs_core.last_qc_block_timestamp);
   }

   // verifies state cannot be transitioned to a smaller last_qc_block_num
   for (bool is_last_qc_strong: { true, false }) {
      BOOST_REQUIRE_THROW(old_bhs_core.next({old_last_qc_block_num - 1, old_last_qc_block_timestamp, is_last_qc_strong}),
                          block_validate_exception);
   }

   // verifies state transition works when is_last_qc_strong is true
   constexpr auto input_last_qc_block_num = 4u;
   const block_timestamp_type input_last_qc_block_timestamp(4);
   auto new_bhs_core = old_bhs_core.next({input_last_qc_block_num, input_last_qc_block_timestamp, true});
   // old final_on_strong_qc block became final
   BOOST_REQUIRE_EQUAL(new_bhs_core.last_final_block_num, old_final_on_strong_qc_block_num);
   // old last_qc block became final_on_strong_qc block
   BOOST_REQUIRE_EQUAL(*new_bhs_core.final_on_strong_qc_block_num, old_last_qc_block_num);
   // new last_qc_block_num is the same as input
   BOOST_REQUIRE_EQUAL(*new_bhs_core.last_qc_block_num, input_last_qc_block_num);
   BOOST_REQUIRE(new_bhs_core.last_qc_block_timestamp == input_last_qc_block_timestamp);

   // verifies state transition works when is_last_qc_strong is false
   new_bhs_core = old_bhs_core.next({input_last_qc_block_num, input_last_qc_block_timestamp, false});
   // last_final_block_num should not change
   BOOST_REQUIRE_EQUAL(new_bhs_core.last_final_block_num, old_last_final_block_num);
   // new final_on_strong_qc_block_num should not be present
   BOOST_REQUIRE(!new_bhs_core.final_on_strong_qc_block_num.has_value());
   // new last_qc_block_num is the same as input
   BOOST_REQUIRE_EQUAL(*new_bhs_core.last_qc_block_num, input_last_qc_block_num);
   BOOST_REQUIRE(new_bhs_core.last_qc_block_timestamp == input_last_qc_block_timestamp);
}

// A test to demonstrate 3-chain state transitions from the first
// block after hotstuff activation
BOOST_AUTO_TEST_CASE(block_header_state_core_3_chain_transition_test)
{
   // block2: initial setup
   constexpr auto block2_last_final_block_num = 1u;
   block_header_state_core block2_bhs_core(block2_last_final_block_num, {}, {});

   // block2 --> block3
   constexpr auto block3_input_last_qc_block_num = 2u;
   const block_timestamp_type  block3_input_last_qc_block_timestamp(2);
   auto block3_bhs_core = block2_bhs_core.next({block3_input_last_qc_block_num, block3_input_last_qc_block_timestamp, true});
   // last_final_block_num should be the same as old one
   BOOST_REQUIRE_EQUAL(block3_bhs_core.last_final_block_num, block2_last_final_block_num);
   // final_on_strong_qc_block_num should be same as old one
   BOOST_REQUIRE(!block3_bhs_core.final_on_strong_qc_block_num.has_value());
   // new last_qc_block_num is the same as input
   BOOST_REQUIRE_EQUAL(*block3_bhs_core.last_qc_block_num, block3_input_last_qc_block_num);
   BOOST_REQUIRE(block3_bhs_core.last_qc_block_timestamp == block3_input_last_qc_block_timestamp);
   auto block3_last_qc_block_num = *block3_bhs_core.last_qc_block_num;

   // block3 --> block4
   constexpr auto block4_input_last_qc_block_num = 3u;
   const block_timestamp_type  block4_input_last_qc_block_timestamp(3);
   auto block4_bhs_core = block3_bhs_core.next({block4_input_last_qc_block_num, block4_input_last_qc_block_timestamp, true});
   // last_final_block_num should not change
   BOOST_REQUIRE_EQUAL(block4_bhs_core.last_final_block_num, block2_last_final_block_num);
   // final_on_strong_qc_block_num should be block3's last_qc_block_num
   BOOST_REQUIRE_EQUAL(*block4_bhs_core.final_on_strong_qc_block_num, block3_last_qc_block_num);
   // new last_qc_block_num is the same as input
   BOOST_REQUIRE_EQUAL(*block4_bhs_core.last_qc_block_num, block4_input_last_qc_block_num);
   BOOST_REQUIRE(block4_bhs_core.last_qc_block_timestamp == block4_input_last_qc_block_timestamp);
   auto block4_final_on_strong_qc_block_num = *block4_bhs_core.final_on_strong_qc_block_num;
   auto block4_last_qc_block_num = *block4_bhs_core.last_qc_block_num;

   // block4 --> block5
   constexpr auto block5_input_last_qc_block_num = 4u;
   const block_timestamp_type  block5_input_last_qc_block_timestamp(4);
   auto block5_bhs_core = block4_bhs_core.next({block5_input_last_qc_block_num, block5_input_last_qc_block_timestamp, true});
   // last_final_block_num should have a new value
   BOOST_REQUIRE_EQUAL(block5_bhs_core.last_final_block_num, block4_final_on_strong_qc_block_num);
   // final_on_strong_qc_block_num should be block4's last_qc_block_num
   BOOST_REQUIRE_EQUAL(*block5_bhs_core.final_on_strong_qc_block_num, block4_last_qc_block_num);
   // new last_qc_block_num is the same as input
   BOOST_REQUIRE_EQUAL(*block5_bhs_core.last_qc_block_num, block5_input_last_qc_block_num);
   BOOST_REQUIRE(block5_bhs_core.last_qc_block_timestamp == block5_input_last_qc_block_timestamp);
}

BOOST_AUTO_TEST_SUITE_END()
#endif

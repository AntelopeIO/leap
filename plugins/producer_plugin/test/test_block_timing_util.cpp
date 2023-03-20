#define BOOST_TEST_MODULE block_timing_util
#include <boost/test/included/unit_test.hpp>
#include <eosio/producer_plugin/block_timing_util.hpp>
#include <fc/mock_time.hpp>

namespace fc {
std::ostream& boost_test_print_type(std::ostream& os, const time_point& t) { return os << (std::string)t; }
} // namespace fc

BOOST_AUTO_TEST_SUITE(block_timing_util)

struct test_fixture : eosio::block_timing_util {
   static_assert(eosio::chain::config::block_interval_ms == 500);

   static constexpr auto       block_interval = fc::microseconds(eosio::chain::config::block_interval_us);
   static constexpr auto       cpu_effort     = fc::milliseconds(400);
   static constexpr auto       production_round_1st_block_slot = 100 * eosio::chain::config::producer_repetitions;
   static const fc::time_point production_round_1st_block_time;
   test_fixture() { cpu_effort_us = cpu_effort.count(); }
};
const fc::time_point test_fixture::production_round_1st_block_time =
      eosio::chain::block_timestamp_type(production_round_1st_block_slot).to_time_point();

BOOST_FIXTURE_TEST_CASE(test_production_round_block_start_time, test_fixture) {

   auto expected_start_time = production_round_1st_block_time - block_interval;
   for (int i = 0; i < eosio::chain::config::producer_repetitions;
        ++i, expected_start_time = expected_start_time + cpu_effort) {
      auto block_time = eosio::chain::block_timestamp_type(production_round_1st_block_slot + i);
      BOOST_CHECK_EQUAL(production_round_block_start_time(block_time), expected_start_time);
   }
}

BOOST_FIXTURE_TEST_CASE(test_calculate_block_deadline, test_fixture) {
   {
      // Scenario 1:
      // In speculating mode, the deadline of a block will always be ahead of its block_time by 100 ms,
      // These deadlines are referred as hard deadlines.
      for (int i = 0; i < eosio::chain::config::producer_repetitions; ++i) {
         auto block_time        = eosio::chain::block_timestamp_type(production_round_1st_block_slot + i);
         auto expected_deadline = block_time.to_time_point() - fc::milliseconds(100);
         BOOST_CHECK_EQUAL(calculate_block_deadline(eosio::pending_block_mode::speculating, block_time),
                           expected_deadline);
         BOOST_CHECK_EQUAL(expected_deadline, pending_block_deadline);
      }
   }
   {
      // Scenario 2:
      // In producing mode, the deadline of a block will be ahead of its block_time from 100, 200, 300, ...ms,
      // depending on the its index to the starting block of a production round. These deadlines are referred
      // as optimized deadlines.
      fc::mock_time_traits::set_now(production_round_1st_block_time - block_interval + fc::milliseconds(10));
      for (int i = 0; i < eosio::chain::config::producer_repetitions; ++i) {
         auto block_time        = eosio::chain::block_timestamp_type(production_round_1st_block_slot + i);
         auto expected_deadline = block_time.to_time_point() - fc::milliseconds((i + 1) * 100);
         BOOST_CHECK_EQUAL(calculate_block_deadline(eosio::pending_block_mode::producing, block_time),
                           expected_deadline);
         BOOST_CHECK_EQUAL(expected_deadline, pending_block_deadline);
         fc::mock_time_traits::set_now(expected_deadline);
      }
   }
   {
      // Scenario 3:
      // In producing mode and it is already too late to meet the optimized deadlines,
      // the returned deadline can never be later than the hard deadlines.

      auto second_block_time = eosio::chain::block_timestamp_type(production_round_1st_block_slot + 1);
      fc::mock_time_traits::set_now(second_block_time.to_time_point() - fc::milliseconds(200));
      auto second_block_hard_deadline = second_block_time.to_time_point() - fc::milliseconds(100);
      BOOST_CHECK_EQUAL(calculate_block_deadline(eosio::pending_block_mode::producing, second_block_time),
                        second_block_hard_deadline);
      // use previous deadline as now
      fc::mock_time_traits::set_now(second_block_hard_deadline);
      auto third_block_time = eosio::chain::block_timestamp_type(production_round_1st_block_slot + 2);
      BOOST_CHECK_EQUAL(calculate_block_deadline(eosio::pending_block_mode::producing, third_block_time),
                        third_block_time.to_time_point() - fc::milliseconds(300));

      // use previous deadline as now
      fc::mock_time_traits::set_now(third_block_time.to_time_point() - fc::milliseconds(300));
      auto forth_block_time = eosio::chain::block_timestamp_type(production_round_1st_block_slot + 3);
      BOOST_CHECK_EQUAL(calculate_block_deadline(eosio::pending_block_mode::producing, forth_block_time),
                        forth_block_time.to_time_point() - fc::milliseconds(400));

      ///////////////////////////////////////////////////////////////////////////////////////////////////

      auto seventh_block_time = eosio::chain::block_timestamp_type(production_round_1st_block_slot + 6);
      fc::mock_time_traits::set_now(seventh_block_time.to_time_point() - fc::milliseconds(500));

      BOOST_CHECK_EQUAL(calculate_block_deadline(eosio::pending_block_mode::producing, seventh_block_time),
                        seventh_block_time.to_time_point() - fc::milliseconds(100));

      // use previous deadline as now
      fc::mock_time_traits::set_now(seventh_block_time.to_time_point() - fc::milliseconds(100));
      auto eighth_block_time = eosio::chain::block_timestamp_type(production_round_1st_block_slot + 7);

      BOOST_CHECK_EQUAL(calculate_block_deadline(eosio::pending_block_mode::producing, eighth_block_time),
                        eighth_block_time.to_time_point() - fc::milliseconds(200));

      // use previous deadline as now
      fc::mock_time_traits::set_now(eighth_block_time.to_time_point() - fc::milliseconds(200));
      auto ninth_block_time = eosio::chain::block_timestamp_type(production_round_1st_block_slot + 8);

      BOOST_CHECK_EQUAL(calculate_block_deadline(eosio::pending_block_mode::producing, ninth_block_time),
                        ninth_block_time.to_time_point() - fc::milliseconds(300));
   }
}

BOOST_AUTO_TEST_SUITE_END()

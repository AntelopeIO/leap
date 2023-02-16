#define BOOST_TEST_MODULE snapshot_scheduler
#include <boost/test/included/unit_test.hpp>

#include <eosio/chain/exceptions.hpp>
#include <eosio/producer_plugin/snapshot_scheduler.hpp>


#include <eosio/testing/tester.hpp>

namespace {

using namespace eosio;
using namespace eosio::chain;

BOOST_AUTO_TEST_SUITE(snapshot_scheduler_test)

BOOST_AUTO_TEST_CASE(snapshot_scheduler_test) {

   fc::logger log;
   snapshot_scheduler scheduler;

   {
      // add/remove test
      producer_plugin::snapshot_request_information sri1 = {.snapshot_request_id = 0, .block_spacing = 100, .start_block_num = 5000, .end_block_num = 10000, .snapshot_description = "Example of recurring snapshot"};
      producer_plugin::snapshot_request_information sri2 = {.snapshot_request_id = 1, .block_spacing = 0, .start_block_num = 5200, .end_block_num = 5200, .snapshot_description = "Example of one-time snapshot"};

      scheduler.schedule_snapshot(sri1);
      scheduler.schedule_snapshot(sri2);

      BOOST_CHECK_EQUAL(2, scheduler.get_snapshots().size());

      BOOST_CHECK_EXCEPTION(scheduler.schedule_snapshot(sri1), duplicate_snapshot_request, [](const fc::assert_exception& e) {
         return e.to_detail_string().find("Duplicate snapshot request") != std::string::npos;
      });

      producer_plugin::snapshot_request_information sri_delete_1 = {.snapshot_request_id = 0};
      scheduler.unschedule_snapshot(sri_delete_1);

      BOOST_CHECK_EQUAL(1, scheduler.get_snapshots().size());

      producer_plugin::snapshot_request_information sri_delete_none = {.snapshot_request_id = 2};
      BOOST_CHECK_EXCEPTION(scheduler.unschedule_snapshot(sri_delete_none), snapshot_request_not_found, [](const fc::assert_exception& e) {
         return e.to_detail_string().find("Snapshot request not found") != std::string::npos;
      });

      producer_plugin::snapshot_request_information sri_delete_2 = {.snapshot_request_id = 1};
      scheduler.unschedule_snapshot(sri_delete_2);

      BOOST_CHECK_EQUAL(0, scheduler.get_snapshots().size());

      producer_plugin::snapshot_request_information sri_large_spacing = {.snapshot_request_id = 0, .block_spacing = 1000, .start_block_num = 5000, .end_block_num = 5010 };
      BOOST_CHECK_EXCEPTION(scheduler.schedule_snapshot(sri_large_spacing), invalid_snapshot_request, [](const fc::assert_exception& e) {
         return e.to_detail_string().find("Block spacing exceeds defined by start and end range") != std::string::npos;
      });

      producer_plugin::snapshot_request_information sri_start_end = {.snapshot_request_id = 0, .block_spacing = 1000, .start_block_num = 50000, .end_block_num = 5000 };
      BOOST_CHECK_EXCEPTION(scheduler.schedule_snapshot(sri_start_end), invalid_snapshot_request, [](const fc::assert_exception& e) {
         return e.to_detail_string().find("End block number should be greater or equal to start block number") != std::string::npos;
      });
   }
}

BOOST_AUTO_TEST_SUITE_END()

}// namespace

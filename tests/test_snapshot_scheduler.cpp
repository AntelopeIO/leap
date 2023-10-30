#include <boost/test/unit_test.hpp>
#include <eosio/chain/authority.hpp>
#include <eosio/chain/exceptions.hpp>
#include <eosio/producer_plugin/producer_plugin.hpp>
#include <eosio/testing/tester.hpp>

using namespace eosio;
using namespace eosio::chain;

using snapshot_request_information = snapshot_scheduler::snapshot_request_information;
using snapshot_request_params = snapshot_scheduler::snapshot_request_params;
using snapshot_request_id_information = snapshot_scheduler::snapshot_request_id_information;

BOOST_AUTO_TEST_SUITE(producer_snapshot_scheduler_tests)

BOOST_AUTO_TEST_CASE(snapshot_scheduler_test) {
   fc::logger log;
   snapshot_scheduler scheduler;

   {
      // add/remove test
      snapshot_request_information sri1 = {.block_spacing = 100, .start_block_num = 5000, .end_block_num = 10000, .snapshot_description = "Example of recurring snapshot"};
      snapshot_request_information sri2 = {.block_spacing = 0, .start_block_num = 5200, .end_block_num = 5200, .snapshot_description = "Example of one-time snapshot"};

      scheduler.schedule_snapshot(sri1);
      scheduler.schedule_snapshot(sri2);

      BOOST_CHECK_EQUAL(2u, scheduler.get_snapshot_requests().snapshot_requests.size());

      BOOST_CHECK_EXCEPTION(scheduler.schedule_snapshot(sri1), duplicate_snapshot_request, [](const fc::assert_exception& e) {
         return e.to_detail_string().find("Duplicate snapshot request") != std::string::npos;
      });

      scheduler.unschedule_snapshot(0);
      BOOST_CHECK_EQUAL(1u, scheduler.get_snapshot_requests().snapshot_requests.size());

      BOOST_CHECK_EXCEPTION(scheduler.unschedule_snapshot(0), snapshot_request_not_found, [](const fc::assert_exception& e) {
         return e.to_detail_string().find("Snapshot request not found") != std::string::npos;
      });

      scheduler.unschedule_snapshot(1);
      BOOST_CHECK_EQUAL(0u, scheduler.get_snapshot_requests().snapshot_requests.size());

      snapshot_request_information sri_large_spacing = {.block_spacing = 1000, .start_block_num = 5000, .end_block_num = 5010};
      BOOST_CHECK_EXCEPTION(scheduler.schedule_snapshot(sri_large_spacing), invalid_snapshot_request, [](const fc::assert_exception& e) {
         return e.to_detail_string().find("Block spacing exceeds defined by start and end range") != std::string::npos;
      });

      snapshot_request_information sri_start_end = {.block_spacing = 1000, .start_block_num = 50000, .end_block_num = 5000};
      BOOST_CHECK_EXCEPTION(scheduler.schedule_snapshot(sri_start_end), invalid_snapshot_request, [](const fc::assert_exception& e) {
         return e.to_detail_string().find("End block number should be greater or equal to start block number") != std::string::npos;
      });
   }
   {
      fc::temp_directory temp_dir;
      const auto& temp = temp_dir.path();
      appbase::scoped_app app;

      try {
         std::promise<std::tuple<producer_plugin*, chain_plugin*>> plugin_promise;
         std::future<std::tuple<producer_plugin*, chain_plugin*>> plugin_fut = plugin_promise.get_future();

         std::promise<void> at_block_20_promise;
         std::future<void> at_block_20_fut = at_block_20_promise.get_future();

         std::thread app_thread([&]() {
            try {
               fc::logger::get(DEFAULT_LOGGER).set_log_level(fc::log_level::debug);
               std::vector<const char*> argv =
                     {"test", "--data-dir", temp.c_str(), "--config-dir", temp.c_str(),
                      "-p", "eosio", "-e"};
               app->initialize<chain_plugin, producer_plugin>(argv.size(), (char**) &argv[0]);
               app->startup();

               producer_plugin* prod_plug = app->find_plugin<producer_plugin>();
               chain_plugin* chain_plug = app->find_plugin<chain_plugin>();
               plugin_promise.set_value({prod_plug, chain_plug});

               auto bs = chain_plug->chain().block_start.connect([&prod_plug, &at_block_20_promise](uint32_t bn) {
                  if(bn == 20u)
                     at_block_20_promise.set_value();
                  // catching pending snapshot
                  if (!prod_plug->get_snapshot_requests().snapshot_requests.empty()) {
                     const auto& snapshot_requests = prod_plug->get_snapshot_requests().snapshot_requests;

                     auto validate_snapshot_request = [&](uint32_t sid, uint32_t block_num, uint32_t spacing = 0, bool fuzzy_start = false) {
                        auto it = find_if(snapshot_requests.begin(), snapshot_requests.end(), [sid](const snapshot_scheduler::snapshot_schedule_information& obj) {return obj.snapshot_request_id == sid;});
                        if (it != snapshot_requests.end()) {
                           auto& pending = it->pending_snapshots;
                           if (pending.size()==1u) {
                              // pending snapshot block number
                              auto pbn = pending.begin()->head_block_num;

                              // first pending snapshot
                              auto ps_start = (spacing != 0) ? (spacing + (pbn%spacing)) : pbn;

                              if (!fuzzy_start) {
                                 BOOST_CHECK_EQUAL(block_num, ps_start);
                              }
                              else {
                                 int diff = block_num - ps_start;
                                 BOOST_CHECK(std::abs(diff) <= 5); // accept +/- 5 blocks if start block not specified
                              }
                           }
                           return true;
                        }
                        return false;
                     };

                     BOOST_REQUIRE(validate_snapshot_request(0, 9,  8));         // snapshot #0 should have pending snapshot at block #9 (8 + 1) and it never expires
                     BOOST_REQUIRE(validate_snapshot_request(4, 12, 10, true));  // snapshot #4 should have pending snapshot at block # at the moment of scheduling (2) plus 10 = 12
                     BOOST_REQUIRE(validate_snapshot_request(5, 10, 10));        // snapshot #5 should have pending snapshot at block #10, #20 etc
                  }
               });

               app->exec();
               return;
            } FC_LOG_AND_DROP()
            BOOST_CHECK(!"app threw exception see logged error");
         });

         auto [prod_plug, chain_plug] = plugin_fut.get();

         snapshot_request_params sri1 = {.block_spacing = 8, .start_block_num = 1, .end_block_num = 300000, .snapshot_description = "Example of recurring snapshot 1"};
         snapshot_request_params sri2 = {.block_spacing = 5000, .start_block_num = 100000, .end_block_num = 300000, .snapshot_description = "Example of recurring snapshot 2 that wont happen in test"};
         snapshot_request_params sri3 = {.block_spacing = 2, .start_block_num = 0, .end_block_num = 3, .snapshot_description = "Example of recurring snapshot 3 that will expire"};
         snapshot_request_params sri4 = {.start_block_num = 1, .snapshot_description = "One time snapshot on first block"};
         snapshot_request_params sri5 = {.block_spacing = 10, .snapshot_description = "Recurring every 10 blocks snapshot starting now"};
         snapshot_request_params sri6 = {.block_spacing = 10, .start_block_num = 0, .snapshot_description = "Recurring every 10 blocks snapshot starting from 0"};

         app->post(appbase::priority::medium_low, [&]() {
            prod_plug->schedule_snapshot(sri1);
            prod_plug->schedule_snapshot(sri2);
            prod_plug->schedule_snapshot(sri3);
            prod_plug->schedule_snapshot(sri4);
            prod_plug->schedule_snapshot(sri5);
            prod_plug->schedule_snapshot(sri6);

            // all six snapshot requests should be present now
            BOOST_CHECK_EQUAL(6u, prod_plug->get_snapshot_requests().snapshot_requests.size());
         });

         at_block_20_fut.get();

         app->post(appbase::priority::medium_low, [&]() {
            // two of the snapshots are done here and requests, corresponding to them should be deleted
            BOOST_CHECK_EQUAL(4u, prod_plug->get_snapshot_requests().snapshot_requests.size());

            // check whether no pending snapshots present for a snapshot with id 0
            const auto& snapshot_requests = prod_plug->get_snapshot_requests().snapshot_requests;
            auto it = find_if(snapshot_requests.begin(), snapshot_requests.end(),[](const snapshot_scheduler::snapshot_schedule_information& obj) {return obj.snapshot_request_id == 0;});

            // snapshot request with id = 0 should be found and should not have any pending snapshots
            BOOST_REQUIRE(it != snapshot_requests.end());
            BOOST_CHECK(!it->pending_snapshots.size());

            // quit app
            app->quit();
         });
         app_thread.join();

         // lets check whether schedule can be read back after restart
         snapshot_scheduler::snapshot_db_json db;
         std::vector<snapshot_scheduler::snapshot_schedule_information> ssi;
         db.set_path(temp / "snapshots");
         db >> ssi;
         BOOST_CHECK_EQUAL(4u, ssi.size());
         BOOST_CHECK_EQUAL(ssi.begin()->block_spacing, *sri1.block_spacing);
      } catch(...) {
         throw;
      }
   }
}

BOOST_AUTO_TEST_SUITE_END()

#define BOOST_TEST_MODULE snapshot_scheduler
#include <boost/test/included/unit_test.hpp>

#include <eosio/chain/exceptions.hpp>
#include <eosio/producer_plugin/producer_plugin.hpp>
#include <eosio/producer_plugin/snapshot_db_json.hpp>
#include <eosio/testing/tester.hpp>
namespace {

using namespace eosio;
using namespace eosio::chain;

BOOST_AUTO_TEST_SUITE(snapshot_scheduler_test)

BOOST_AUTO_TEST_CASE(snapshot_scheduler_test) {

   fc::logger log;
   producer_plugin scheduler;

   {
      // add/remove test
      producer_plugin::snapshot_request_information sri1 = {.block_spacing = 100, .start_block_num = 5000, .end_block_num = 10000, .snapshot_description = "Example of recurring snapshot"};
      producer_plugin::snapshot_request_information sri2 = {.block_spacing = 0, .start_block_num = 5200, .end_block_num = 5200, .snapshot_description = "Example of one-time snapshot"};

      scheduler.schedule_snapshot(sri1);
      scheduler.schedule_snapshot(sri2);

      BOOST_CHECK_EQUAL(2, scheduler.get_snapshot_requests().snapshot_requests.size());

      BOOST_CHECK_EXCEPTION(scheduler.schedule_snapshot(sri1), duplicate_snapshot_request, [](const fc::assert_exception& e) {
         return e.to_detail_string().find("Duplicate snapshot request") != std::string::npos;
      });

      producer_plugin::snapshot_request_id_information sri_delete_1 = {.snapshot_request_id = 0};
      scheduler.unschedule_snapshot(sri_delete_1);

      BOOST_CHECK_EQUAL(1, scheduler.get_snapshot_requests().snapshot_requests.size());

      producer_plugin::snapshot_request_id_information sri_delete_none = {.snapshot_request_id = 2};
      BOOST_CHECK_EXCEPTION(scheduler.unschedule_snapshot(sri_delete_none), snapshot_request_not_found, [](const fc::assert_exception& e) {
         return e.to_detail_string().find("Snapshot request not found") != std::string::npos;
      });

      producer_plugin::snapshot_request_id_information sri_delete_2 = {.snapshot_request_id = 1};
      scheduler.unschedule_snapshot(sri_delete_2);

      BOOST_CHECK_EQUAL(0, scheduler.get_snapshot_requests().snapshot_requests.size());

      producer_plugin::snapshot_request_information sri_large_spacing = {.block_spacing = 1000, .start_block_num = 5000, .end_block_num = 5010};
      BOOST_CHECK_EXCEPTION(scheduler.schedule_snapshot(sri_large_spacing), invalid_snapshot_request, [](const fc::assert_exception& e) {
         return e.to_detail_string().find("Block spacing exceeds defined by start and end range") != std::string::npos;
      });

      producer_plugin::snapshot_request_information sri_start_end = {.block_spacing = 1000, .start_block_num = 50000, .end_block_num = 5000};
      BOOST_CHECK_EXCEPTION(scheduler.schedule_snapshot(sri_start_end), invalid_snapshot_request, [](const fc::assert_exception& e) {
         return e.to_detail_string().find("End block number should be greater or equal to start block number") != std::string::npos;
      });
   }
   {
      boost::filesystem::path temp = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();

      try {
         std::promise<std::tuple<producer_plugin*, chain_plugin*>> plugin_promise;
         std::future<std::tuple<producer_plugin*, chain_plugin*>> plugin_fut = plugin_promise.get_future();

         std::thread app_thread([&]() {
            fc::logger::get(DEFAULT_LOGGER).set_log_level(fc::log_level::debug);
            std::vector<const char*> argv =
                  {"test", "--data-dir", temp.c_str(), "--config-dir", temp.c_str(),
                   "-p", "eosio", "-e", "--disable-subjective-billing=true"};
            appbase::app().initialize<chain_plugin, producer_plugin>(argv.size(), (char**) &argv[0]);
            appbase::app().startup();
            plugin_promise.set_value(
                  {appbase::app().find_plugin<producer_plugin>(), appbase::app().find_plugin<chain_plugin>()});
            appbase::app().exec();
         });

         auto [prod_plug, chain_plug] = plugin_fut.get();
         std::deque<block_state_ptr> all_blocks;
         std::promise<void> empty_blocks_promise;
         std::future<void> empty_blocks_fut = empty_blocks_promise.get_future();
         auto ab = chain_plug->chain().accepted_block.connect([&](const block_state_ptr& bsp) {
            static int num_empty = std::numeric_limits<int>::max();
            all_blocks.push_back(bsp);
            if(bsp->block->transactions.empty()) {
               --num_empty;
               if(num_empty == 0) empty_blocks_promise.set_value();
            } else {// we want a few empty blocks after we have some non-empty blocks
               num_empty = 10;
            }
         });
         auto pp = appbase::app().find_plugin<producer_plugin>();
         auto bs = chain_plug->chain().block_start.connect([&pp](uint32_t bn) {
            // catching pending snapshot
            if (!pp->get_snapshot_requests().snapshot_requests.empty()) {
               auto& pending = pp->get_snapshot_requests().snapshot_requests.begin()->pending_snapshots;
               if (pending && pending->size()==1) {
                  // lets check the head block num of it, it should be 8 + 1 = 9
                  // this means we are getting a snapshot for correct block # as well
                  BOOST_CHECK_EQUAL(9, pending->begin()->head_block_num);   
               }
            }
         });
       
         producer_plugin::snapshot_request_information sri1 = {.block_spacing = 8, .start_block_num = 1, .end_block_num = 300000, .snapshot_description = "Example of recurring snapshot 2"};
         producer_plugin::snapshot_request_information sri2 = {.block_spacing = 5000, .start_block_num = 100000, .end_block_num = 300000, .snapshot_description = "Example of recurring snapshot 2"};
         producer_plugin::snapshot_request_information sri3 = {.block_spacing = 2, .start_block_num = 0, .end_block_num = 3, .snapshot_description = "Example of recurring snapshot 1"};

         pp->schedule_snapshot(sri1);
         pp->schedule_snapshot(sri2);
         pp->schedule_snapshot(sri3);

         // all three snapshot requests should be present now
         BOOST_CHECK_EQUAL(3, pp->get_snapshot_requests().snapshot_requests.size());

         empty_blocks_fut.wait_for(std::chrono::seconds(5));

         // one of the snapshots is done here and request, corresponding to it should be deleted
         BOOST_CHECK_EQUAL(2, pp->get_snapshot_requests().snapshot_requests.size());

         // check whether no pending snapshots present
         BOOST_CHECK_EQUAL(0, pp->get_snapshot_requests().snapshot_requests.begin()->pending_snapshots->size());

         // quit app
         appbase::app().quit();
         app_thread.join();

         // lets check whether schedule can be read back after restart
         snapshot_db_json db;
         std::vector<producer_plugin::snapshot_schedule_information> ssi;
         db.set_path(temp / "snapshots");
         db >> ssi;
         BOOST_CHECK_EQUAL(2, ssi.size());
         BOOST_CHECK_EQUAL(ssi.begin()->block_spacing, sri1.block_spacing);
      } catch(...) {
         bfs::remove_all(temp);
         throw;
      }
      bfs::remove_all(temp);
   }
}
   BOOST_AUTO_TEST_SUITE_END()
}// namespace

#define BOOST_TEST_MODULE full_producer_trxs
#include <boost/test/included/unit_test.hpp>

#include <eosio/producer_plugin/producer_plugin.hpp>

#include <eosio/testing/tester.hpp>

#include <eosio/chain/genesis_state.hpp>
#include <eosio/chain/thread_utils.hpp>
#include <eosio/chain/trace.hpp>
#include <eosio/chain/name.hpp>

#include <appbase/application.hpp>

using namespace eosio;
using namespace eosio::chain;

BOOST_AUTO_TEST_SUITE(program_options)

BOOST_AUTO_TEST_CASE(state_dir) {
   boost::filesystem::path temp = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();
   boost::filesystem::path state_dir = temp / "state";
   boost::filesystem::path custom_state_dir = temp / "custom_state_dir";
      
   try {
      std::promise<std::tuple<producer_plugin*, chain_plugin*>> plugin_promise;
      std::future<std::tuple<producer_plugin*, chain_plugin*>> plugin_fut = plugin_promise.get_future();
      std::thread app_thread( [&]() {
         fc::logger::get(DEFAULT_LOGGER).set_log_level(fc::log_level::debug);
         std::vector<const char*> argv =
               {"test", "--data-dir", temp.c_str(),  "--state-dir", custom_state_dir.c_str(), "--config-dir", temp.c_str(),
                "-p", "eosio", "-e", "--max-transaction-time", "475", "--disable-subjective-billing=true" };
         appbase::app().initialize<chain_plugin, producer_plugin>( argv.size(), (char**) &argv[0] );
         appbase::app().startup();
         plugin_promise.set_value(
               {appbase::app().find_plugin<producer_plugin>(), appbase::app().find_plugin<chain_plugin>()} );
         appbase::app().exec();
      } );

      auto[prod_plug, chain_plug] = plugin_fut.get();
      [[maybe_unused]] auto chain_id = chain_plug->get_chain_id();

      // check that "--state-dir" option was taken into account
      BOOST_CHECK(  exists( custom_state_dir ));
      BOOST_CHECK( !exists( state_dir ));
      
      appbase::app().quit();
      app_thread.join();

   } catch ( ... ) {
      bfs::remove_all( temp );
      throw;
   }
   bfs::remove_all( temp );
}

BOOST_AUTO_TEST_SUITE_END()

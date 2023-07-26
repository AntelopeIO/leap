#include <boost/test/unit_test.hpp>

#include <eosio/producer_plugin/producer_plugin.hpp>

#include <eosio/testing/tester.hpp>

#include <eosio/chain/genesis_state.hpp>
#include <eosio/chain/thread_utils.hpp>
#include <eosio/chain/trace.hpp>
#include <eosio/chain/name.hpp>

#include <eosio/chain/application.hpp>

using namespace eosio;
using namespace eosio::chain;

BOOST_AUTO_TEST_SUITE(program_options)

BOOST_AUTO_TEST_CASE(state_dir) {
   fc::temp_directory temp;
   auto temp_dir = temp.path();
   auto state_dir = temp.path() / "state";
   auto custom_state_dir = temp.path() / "custom_state_dir";

   auto temp_dir_str = temp_dir.string();
   auto custom_state_dir_str = custom_state_dir.string();
      
   appbase::scoped_app app;

   std::promise<std::tuple<producer_plugin*, chain_plugin*>> plugin_promise;
   std::future<std::tuple<producer_plugin*, chain_plugin*>> plugin_fut = plugin_promise.get_future();
   std::thread app_thread( [&]() {
      try {
         fc::logger::get(DEFAULT_LOGGER).set_log_level(fc::log_level::debug);
         std::vector<const char*> argv =
            {"test",
             "--data-dir",   temp_dir_str.c_str(),
             "--state-dir",  custom_state_dir_str.c_str(),
             "--config-dir", temp_dir_str.c_str(),
             "-p", "eosio", "-e" };
         app->initialize<chain_plugin, producer_plugin>( argv.size(), (char**) &argv[0] );
         app->startup();
         plugin_promise.set_value( {app->find_plugin<producer_plugin>(), app->find_plugin<chain_plugin>()} );
         app->exec();
         return;
      } FC_LOG_AND_DROP()
      BOOST_CHECK(!"app threw exception see logged error");
   } );

   auto[prod_plug, chain_plug] = plugin_fut.get();
   [[maybe_unused]] auto chain_id = chain_plug->get_chain_id();

   // check that "--state-dir" option was taken into account
   BOOST_CHECK(  exists( custom_state_dir ));
   BOOST_CHECK( !exists( state_dir ));
      
   app->quit();
   app_thread.join();
}

BOOST_AUTO_TEST_SUITE_END()

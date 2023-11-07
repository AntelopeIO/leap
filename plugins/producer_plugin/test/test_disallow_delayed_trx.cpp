#include <eosio/producer_plugin/producer_plugin.hpp>
#include <eosio/testing/tester.hpp>
#include <boost/test/unit_test.hpp>

namespace eosio::test::detail {
using namespace eosio::chain::literals;
struct testit {
   uint64_t id;

   testit( uint64_t id = 0 ) :id(id){}

   static account_name get_account() {
      return chain::config::system_account_name;
   }

   static action_name get_name() {
      return "testit"_n;
   }
};
}
FC_REFLECT( eosio::test::detail::testit, (id) )

namespace {

using namespace eosio;
using namespace eosio::chain;
using namespace eosio::test::detail;

auto make_delayed_trx( const chain_id_type& chain_id ) {
   account_name creator = config::system_account_name;

   signed_transaction trx;
   trx.actions.emplace_back( vector<permission_level>{{creator, config::active_name}}, testit{0} );
   trx.delay_sec = 10;
   auto priv_key = private_key_type::regenerate<fc::ecc::private_key_shim>(fc::sha256::hash(std::string("nathan")));
   trx.sign( priv_key, chain_id );

   return std::make_shared<packed_transaction>( std::move(trx) );
}
}

BOOST_AUTO_TEST_SUITE(disallow_delayed_trx_test)

// Verifies that incoming delayed transactions are blocked.
BOOST_AUTO_TEST_CASE(delayed_trx) {
   using namespace std::chrono_literals;
   fc::temp_directory temp;
   appbase::scoped_app app;
   auto temp_dir_str = temp.path().string();
   
   std::promise<std::tuple<producer_plugin*, chain_plugin*>> plugin_promise;
   std::future<std::tuple<producer_plugin*, chain_plugin*>> plugin_fut = plugin_promise.get_future();
   std::thread app_thread( [&]() {
      try {
         fc::logger::get(DEFAULT_LOGGER).set_log_level(fc::log_level::debug);
         std::vector<const char*> argv =
            {"test", "--data-dir", temp_dir_str.c_str(), "--config-dir", temp_dir_str.c_str(),
               "-p", "eosio", "-e", "--disable-subjective-p2p-billing=true" };
         app->initialize<chain_plugin, producer_plugin>( argv.size(), (char**) &argv[0] );
         app->startup();
         plugin_promise.set_value(
            {app->find_plugin<producer_plugin>(), app->find_plugin<chain_plugin>()} );
         app->exec();
         return;
      } FC_LOG_AND_DROP()
      BOOST_CHECK(!"app threw exception see logged error");
   } );

   auto[prod_plug, chain_plug] = plugin_fut.get();
   auto chain_id = chain_plug->get_chain_id();

   // create a delayed trx
   auto ptrx = make_delayed_trx( chain_id );

   // send it as incoming trx
   app->post( priority::low, [ptrx, &app]() {
      bool return_failure_traces = true;

      // the delayed trx is blocked
      BOOST_REQUIRE_EXCEPTION(
         app->get_method<plugin_interface::incoming::methods::transaction_async>()(ptrx,
            false,
            transaction_metadata::trx_type::input,
            return_failure_traces,
            [ptrx] (const next_function_variant<transaction_trace_ptr>& result) {
               elog( "trace with except ${e}", ("e", fc::json::to_pretty_string( *std::get<chain::transaction_trace_ptr>( result ) )) );
            }
         ),
         fc::exception,
         eosio::testing::fc_exception_message_starts_with("transaction cannot be delayed")
      );
   });

   // leave time for transaction to be executed
   std::this_thread::sleep_for( 2000ms );

   app->quit();
   app_thread.join();
}

BOOST_AUTO_TEST_SUITE_END()

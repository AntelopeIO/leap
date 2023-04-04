#define BOOST_TEST_MODULE producer_read_only_trxs
#include <boost/test/included/unit_test.hpp>

#include <eosio/producer_plugin/producer_plugin.hpp>
#include <eosio/testing/tester.hpp>
#include <eosio/chain/genesis_state.hpp>
#include <eosio/chain/thread_utils.hpp>
#include <eosio/chain/transaction_metadata.hpp>
#include <eosio/chain/trace.hpp>
#include <eosio/chain/name.hpp>
#include <eosio/chain/application.hpp>

namespace eosio::test::detail {
using namespace eosio::chain::literals;
struct testit {
   uint64_t      id;
   testit( uint64_t id = 0 )
         :id(id){}
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

auto make_unique_trx( const chain_id_type& chain_id ) {
   static uint64_t nextid = 0;
   ++nextid;
   account_name creator = config::system_account_name;
   signed_transaction trx;
   trx.expiration = fc::time_point::now() + fc::seconds( nextid % 50 == 0 ? 0 : 60 ); // fail some transactions via expired
   if( nextid % 10 == 0 ) {
      // fail some for authorization (read-only transaction should not have authorization)
      trx.actions.emplace_back( vector<permission_level>{{creator, config::active_name}}, testit{nextid} );
   } else {
      vector<permission_level> no_auth{};
      trx.actions.emplace_back( no_auth, testit{nextid} );
   }
   return std::make_shared<packed_transaction>( std::move(trx) );
}
}

BOOST_AUTO_TEST_SUITE(read_only_trxs)

void error_handling_common(std::vector<const char*>& specific_args) {
   appbase::scoped_app app;
   fc::temp_directory temp;
   auto temp_dir_str = temp.path().string();
   
   fc::logger::get(DEFAULT_LOGGER).set_log_level(fc::log_level::debug);
   std::vector<const char*> argv =
      {"test", "--data-dir", temp_dir_str.c_str(), "--config-dir", temp_dir_str.c_str()};
   argv.insert( argv.end(), specific_args.begin(), specific_args.end() );
   BOOST_CHECK_EQUAL( app->initialize<producer_plugin>( argv.size(), (char**) &argv[0]), false );
}

// --read-only-thread not allowed on producer node
BOOST_AUTO_TEST_CASE(read_only_on_producer) {
   std::vector<const char*> specific_args = {"-p", "eosio", "-e", "--read-only-threads", "2" };
   error_handling_common(specific_args);
}

// read_window_time must be greater than max_transaction_time + 10ms
BOOST_AUTO_TEST_CASE(invalid_read_window_time) {
   std::vector<const char*> specific_args = { "--read-only-threads", "2", "--max-transaction-time", "10", "--read-only-write-window-time-us", "50000", "--read-only-read-window-time-us", "20000" }; // 20000 not greater than --max-transaction-time (10ms) + 10000us (minimum margin)
   error_handling_common(specific_args);
}

void test_trxs_common(std::vector<const char*>& specific_args) {
   using namespace std::chrono_literals;
   appbase::scoped_app app;
   fc::temp_directory temp;
   auto temp_dir_str = temp.path().string();
   producer_plugin::set_test_mode(true);
   
   std::promise<std::tuple<producer_plugin*, chain_plugin*>> plugin_promise;
   std::future<std::tuple<producer_plugin*, chain_plugin*>> plugin_fut = plugin_promise.get_future();
   std::thread app_thread( [&]() {
      fc::logger::get(DEFAULT_LOGGER).set_log_level(fc::log_level::debug);
      std::vector<const char*> argv = {"test", "--data-dir", temp_dir_str.c_str(), "--config-dir", temp_dir_str.c_str()};
      argv.insert( argv.end(), specific_args.begin(), specific_args.end() );
      app->initialize<chain_plugin, producer_plugin>( argv.size(), (char**) &argv[0] );
      app->startup();
      plugin_promise.set_value( {app->find_plugin<producer_plugin>(), app->find_plugin<chain_plugin>()} );
      app->exec();
   } );

   auto[prod_plug, chain_plug] = plugin_fut.get();
   auto chain_id = chain_plug->get_chain_id();

   std::atomic<size_t> next_calls = 0;
   std::atomic<size_t> num_get_account_calls = 0;
   std::atomic<size_t> num_posts = 0;
   std::atomic<size_t> trace_with_except = 0;
   std::atomic<bool> trx_match = true;
   const size_t num_pushes = 4242;

   for( size_t i = 1; i <= num_pushes; ++i ) {
      auto ptrx = make_unique_trx( chain_id );
      app->executor().post( priority::low, exec_queue::read_only, [&chain_plug=chain_plug, &num_get_account_calls]() {
         chain_plug->get_read_only_api(fc::seconds(90)).get_account(chain_apis::read_only::get_account_params{.account_name=config::system_account_name}, fc::time_point::now()+fc::seconds(90));
         ++num_get_account_calls;
      });
      app->executor().post( priority::low, exec_queue::read_write, [ptrx, &next_calls, &num_posts, &trace_with_except, &trx_match, &app]() {
         ++num_posts;
         bool return_failure_traces = true;
         app->get_method<plugin_interface::incoming::methods::transaction_async>()(ptrx,
            false, // api_trx
            transaction_metadata::trx_type::read_only, // trx_type
            return_failure_traces,
            [ptrx, &next_calls, &trace_with_except, &trx_match, return_failure_traces]
            (const std::variant<fc::exception_ptr, transaction_trace_ptr>& result) {
               if( !std::holds_alternative<fc::exception_ptr>( result ) && !std::get<chain::transaction_trace_ptr>( result )->except ) {
                  if( std::get<chain::transaction_trace_ptr>( result )->id != ptrx->id() ) {
                     elog( "trace not for trx ${id}: ${t}",
                           ("id", ptrx->id())("t", fc::json::to_pretty_string(*std::get<chain::transaction_trace_ptr>(result))) );
                     trx_match = false;
                  }
               } else if( !return_failure_traces && !std::holds_alternative<fc::exception_ptr>( result ) && std::get<chain::transaction_trace_ptr>( result )->except ) {
                  elog( "trace with except ${e}",
                        ("e", fc::json::to_pretty_string( *std::get<chain::transaction_trace_ptr>( result ) )) );
                  ++trace_with_except;
               }
               ++next_calls;
        });
      });
      app->executor().post( priority::low, exec_queue::read_only, [&chain_plug=chain_plug]() {
         chain_plug->get_read_only_api(fc::seconds(90)).get_consensus_parameters(chain_apis::read_only::get_consensus_parameters_params{}, fc::time_point::now()+fc::seconds(90));
      });
   }

   // Wait long enough such that all transactions are executed
   auto start = fc::time_point::now();
   auto hard_deadline = start + fc::seconds(10); // To protect against waiting forever
   while ( (next_calls < num_pushes || num_get_account_calls < num_pushes) && fc::time_point::now() < hard_deadline ){
      std::this_thread::sleep_for( 100ms );;
   }

   app->quit();
   app_thread.join();

   BOOST_CHECK_EQUAL( trace_with_except, 0 ); // should not have any traces with except in it
   BOOST_CHECK_EQUAL( num_pushes, num_posts );
   BOOST_CHECK_EQUAL( num_pushes, next_calls.load() );
   BOOST_CHECK_EQUAL( num_pushes, num_get_account_calls.load() );
   BOOST_CHECK( trx_match.load() );  // trace should match the transaction
}

// test read-only trxs on main thread (no --read-only-threads)
BOOST_AUTO_TEST_CASE(no_read_only_threads) {
   std::vector<const char*> specific_args = { "-p", "eosio", "-e", "--abi-serializer-max-time-ms=999" };
   test_trxs_common(specific_args);
}

// test read-only trxs on 1 threads (with --read-only-threads)
BOOST_AUTO_TEST_CASE(with_1_read_only_threads) {
   std::vector<const char*> specific_args = { "-p", "eosio", "-e",
                                              "--read-only-threads=1",
                                              "--max-transaction-time=10",
                                              "--abi-serializer-max-time-ms=999",
                                              "--read-only-write-window-time-us=100000",
                                              "--read-only-read-window-time-us=40000",
                                              "--disable-subjective-billing=true" };
   test_trxs_common(specific_args);
}

// test read-only trxs on 16 separate threads (with --read-only-threads)
BOOST_AUTO_TEST_CASE(with_16_read_only_threads) {
   std::vector<const char*> specific_args = { "-p", "eosio", "-e",
                                              "--read-only-threads=16",
                                              "--max-transaction-time=10",
                                              "--abi-serializer-max-time-ms=999",
                                              "--read-only-write-window-time-us=100000",
                                              "--read-only-read-window-time-us=40000",
                                              "--disable-subjective-billing=true" };
   test_trxs_common(specific_args);
}


BOOST_AUTO_TEST_SUITE_END()

#include <boost/test/unit_test.hpp>

#include <test_utils.hpp>
#include <eosio/producer_plugin/producer_plugin.hpp>
#include <eosio/testing/tester.hpp>
#include <eosio/chain/block.hpp>
#include <eosio/chain/config.hpp>
#include <eosio/chain/types.hpp>
#include <eosio/chain/controller.hpp>
#include <eosio/chain/genesis_state.hpp>
#include <eosio/chain/thread_utils.hpp>
#include <eosio/chain/transaction.hpp>
#include <eosio/chain/transaction_metadata.hpp>
#include <eosio/chain/trace.hpp>
#include <eosio/chain/name.hpp>
#include <eosio/chain/application.hpp>

#include <contracts.hpp>

#include <fc/scoped_exit.hpp>
#include <chrono>


namespace {
using namespace eosio;
using namespace eosio::chain;
using namespace eosio::test_utils;

auto make_unique_trx() {
   static uint64_t nextid = 0;
   ++nextid;
   account_name creator = config::system_account_name;
   signed_transaction trx;
   trx.expiration = fc::time_point_sec{fc::time_point::now() + fc::seconds( nextid % 50 == 0 ? 0 : 60 )}; // fail some transactions via expired
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

enum class app_init_status { failed, succeeded };

void test_configs_common(std::vector<const char*>& specific_args, app_init_status expected_status) {
   fc::temp_directory temp;
   appbase::scoped_app app;
   auto temp_dir_str = temp.path().string();
   
   fc::logger::get(DEFAULT_LOGGER).set_log_level(fc::log_level::debug);
   std::vector<const char*> argv =
      {"test", "--data-dir", temp_dir_str.c_str(), "--config-dir", temp_dir_str.c_str()};
   argv.insert( argv.end(), specific_args.begin(), specific_args.end() );

   // app->initialize() returns a boolean. BOOST_CHECK_EQUAL cannot compare
   // a boolean with a app_init_status directly
   bool rc = (expected_status == app_init_status::succeeded) ? true : false;
   bool result = false;
   try {
      result = app->initialize<producer_plugin>( argv.size(), (char**) &argv[0]);
   } catch(...) {}
   BOOST_CHECK_EQUAL( result, rc );
}

// --read-only-thread not allowed on producer node
BOOST_AUTO_TEST_CASE(read_only_on_producer) {
   std::vector<const char*> specific_args = {"-p", "eosio", "-e", "--read-only-threads", "2" };
   test_configs_common(specific_args, app_init_status::failed);
}

// if --read-only-threads is not configured, read-only trx related configs should
// not be checked
BOOST_AUTO_TEST_CASE(not_check_configs_if_no_read_only_threads) {
   std::vector<const char*> specific_args = { "--max-transaction-time", "10", "--read-only-write-window-time-us", "50000", "--read-only-read-window-time-us", "20000" }; // 20000 not greater than --max-transaction-time (10ms) + 10000us (minimum margin)
   test_configs_common(specific_args, app_init_status::succeeded);
}

void test_trxs_common(std::vector<const char*>& specific_args, bool test_disable_tierup = false) {
   try {
      fc::scoped_exit<std::function<void()>> on_exit = []() {
         chain::wasm_interface::test_disable_tierup = false;
      };
      chain::wasm_interface::test_disable_tierup = test_disable_tierup;

      using namespace std::chrono_literals;
      fc::temp_directory temp;
      appbase::scoped_app app;
      auto temp_dir_str = temp.path().string();
      producer_plugin::set_test_mode(true);

      std::atomic<size_t> next_calls = 0;
      std::atomic<size_t> num_get_account_calls = 0;
      std::atomic<size_t> num_posts = 0;
      std::atomic<size_t> trace_with_except = 0;
      std::atomic<bool> trx_match = true;
      const size_t num_pushes = 4242;

      {
         std::promise<std::tuple<producer_plugin*, chain_plugin*>> plugin_promise;
         std::future<std::tuple<producer_plugin*, chain_plugin*>> plugin_fut = plugin_promise.get_future();
         std::thread app_thread( [&]() {
            try {
               fc::logger::get(DEFAULT_LOGGER).set_log_level(fc::log_level::debug);
               std::vector<const char*> argv = {
                  "test",  // dummy executible name
                  "-p", "eosio", "-e", // actual arguments follow
                  "--data-dir", temp_dir_str.c_str(),
                  "--config-dir", temp_dir_str.c_str(),
                  "--max-transaction-time=100",
                  "--abi-serializer-max-time-ms=999",
                  "--read-only-write-window-time-us=10000",
                  "--read-only-read-window-time-us=400000"
               };
               argv.insert(argv.end(), specific_args.begin(), specific_args.end());
               app->initialize<chain_plugin, producer_plugin>(argv.size(), (char**)&argv[0]);
               app->find_plugin<chain_plugin>()->chain();
               app->startup();
               plugin_promise.set_value({app->find_plugin<producer_plugin>(), app->find_plugin<chain_plugin>()});
               app->exec();
               return;
            } FC_LOG_AND_DROP()
            BOOST_CHECK(!"app threw exception see logged error");
         } );
         fc::scoped_exit<std::function<void()>> on_except = [&](){
            if (app_thread.joinable())
               app_thread.join();
         };

         auto[prod_plug, chain_plug] = plugin_fut.get();

         activate_protocol_features_set_bios_contract(app, chain_plug);

         for( size_t i = 1; i <= num_pushes; ++i ) {
            auto ptrx = i % 3 == 0 ? make_unique_trx() : make_bios_ro_trx(chain_plug->chain());
            app->executor().post( priority::low, exec_queue::read_only, [&chain_plug=chain_plug, &num_get_account_calls]() {
               chain_plug->get_read_only_api(fc::seconds(90)).get_account(chain_apis::read_only::get_account_params{.account_name=config::system_account_name}, fc::time_point::now()+fc::seconds(90));
               ++num_get_account_calls;
            });
            app->executor().post( priority::low, exec_queue::read_exclusive, [ptrx, &next_calls, &num_posts, &trace_with_except, &trx_match, &app]() {
               ++num_posts;
               bool return_failure_traces = true;
               app->get_method<plugin_interface::incoming::methods::transaction_async>()(ptrx,
                  false, // api_trx
                  transaction_metadata::trx_type::read_only, // trx_type
                  return_failure_traces,
                  [ptrx, &next_calls, &trace_with_except, &trx_match, return_failure_traces]
                  (const next_function_variant<transaction_trace_ptr>& result) {
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
            std::this_thread::sleep_for( 100ms );
         }

         app->quit();
      }

      BOOST_CHECK_EQUAL( trace_with_except, 0u ); // should not have any traces with except in it
      BOOST_CHECK_EQUAL( num_pushes, num_posts );
      BOOST_CHECK_EQUAL( num_pushes, next_calls.load() );
      BOOST_CHECK_EQUAL( num_pushes, num_get_account_calls.load() );
      BOOST_CHECK( trx_match.load() );  // trace should match the transaction
   } FC_LOG_AND_RETHROW()
}

// test read-only trxs on 1 threads (with --read-only-threads)
BOOST_AUTO_TEST_CASE(with_1_read_only_threads) {
   std::vector<const char*> specific_args = { "--read-only-threads=1" };
   test_trxs_common(specific_args);
}

// test read-only trxs on 3 threads (with --read-only-threads)
BOOST_AUTO_TEST_CASE(with_3_read_only_threads) {
   std::vector<const char*> specific_args = { "--read-only-threads=3" };
   test_trxs_common(specific_args);
}

// test read-only trxs on 3 threads (with --read-only-threads)
BOOST_AUTO_TEST_CASE(with_3_read_only_threads_no_tierup) {
   std::vector<const char*> specific_args = { "--read-only-threads=3",
#ifdef EOSIO_EOS_VM_OC_RUNTIME_ENABLED
                                             "--eos-vm-oc-enable=none",
#endif
                                            };
   test_trxs_common(specific_args, true);
}

// test read-only trxs on 8 separate threads (with --read-only-threads)
BOOST_AUTO_TEST_CASE(with_8_read_only_threads) {
   std::vector<const char*> specific_args = { "--read-only-threads=8" };
   test_trxs_common(specific_args);
}

// test read-only trxs on 8 separate threads (with --read-only-threads)
BOOST_AUTO_TEST_CASE(with_8_read_only_threads_no_tierup) {
   std::vector<const char*> specific_args = { "--read-only-threads=8",
#ifdef EOSIO_EOS_VM_OC_RUNTIME_ENABLED
                                             "--eos-vm-oc-enable=none",
#endif
                                            };
   test_trxs_common(specific_args, true);
}

BOOST_AUTO_TEST_SUITE_END()

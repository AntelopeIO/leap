#include <trx_provider.hpp>
#include <trx_generator.cpp>
#define BOOST_TEST_MODULE trx_generator_tests
#include <boost/test/included/unit_test.hpp>

using namespace eosio::testing;

struct simple_tps_monitor {
   std::vector<tps_test_stats> _calls;
   bool monitor_test(const tps_test_stats& stats) {
      _calls.push_back(stats);
      return true;
   }

   simple_tps_monitor(size_t expected_num_calls) { _calls.reserve(expected_num_calls); }
};

struct mock_trx_generator {
   std::vector<fc::time_point> _calls;
   std::chrono::microseconds _delay;

   bool setup() {return true;}
   bool tear_down() {return true;}

   bool generate_and_send() {
      _calls.push_back(fc::time_point::now());
      if (_delay.count() > 0) {
         std::this_thread::sleep_for(_delay);
      }
      return true;
   }

   bool stop_on_trx_fail() {
      return false;
   }

   mock_trx_generator(size_t expected_num_calls, uint32_t delay=0) :_calls(), _delay(delay) {
      _calls.reserve(expected_num_calls);
   }
};

BOOST_AUTO_TEST_SUITE(trx_generator_tests)

BOOST_AUTO_TEST_CASE(tps_short_run_low_tps)
{
   constexpr uint32_t test_duration_s = 5;
   constexpr uint32_t test_tps = 5;
   constexpr uint32_t expected_trxs = test_duration_s * test_tps;
   constexpr uint64_t expected_runtime_us = test_duration_s * 1000000;
   constexpr uint64_t allowable_runtime_deviation_per = 20;
   constexpr uint64_t allowable_runtime_deviation_us = expected_runtime_us / allowable_runtime_deviation_per;
   constexpr uint64_t minimum_runtime_us = expected_runtime_us - allowable_runtime_deviation_us;
   constexpr uint64_t maximum_runtime_us = expected_runtime_us + allowable_runtime_deviation_us;

   std::shared_ptr<mock_trx_generator> generator = std::make_shared<mock_trx_generator>(expected_trxs);
   std::shared_ptr<simple_tps_monitor> monitor = std::make_shared<simple_tps_monitor>(expected_trxs);

   trx_tps_tester<mock_trx_generator, simple_tps_monitor> t1(generator, monitor, test_duration_s, test_tps);

   fc::time_point start = fc::time_point::now();
   t1.run();
   fc::time_point end = fc::time_point::now();
   fc::microseconds runtime_us = end.time_since_epoch() - start.time_since_epoch() ;

   BOOST_REQUIRE_EQUAL(generator->_calls.size(), expected_trxs);
   BOOST_REQUIRE_GT(runtime_us.count(), minimum_runtime_us);
   BOOST_REQUIRE_LT(runtime_us.count(), maximum_runtime_us);
}

BOOST_AUTO_TEST_CASE(tps_short_run_high_tps)
{
   constexpr uint32_t test_duration_s = 5;
   constexpr uint32_t test_tps = 50000;
   constexpr uint32_t expected_trxs = test_duration_s * test_tps;
   constexpr uint64_t expected_runtime_us = test_duration_s * 1000000;
   constexpr uint64_t allowable_runtime_deviation_per = 20;
   constexpr uint64_t allowable_runtime_deviation_us = expected_runtime_us / allowable_runtime_deviation_per;
   constexpr uint64_t minimum_runtime_us = expected_runtime_us - allowable_runtime_deviation_us;
   constexpr uint64_t maximum_runtime_us = expected_runtime_us + allowable_runtime_deviation_us;

   std::shared_ptr<mock_trx_generator> generator = std::make_shared<mock_trx_generator>(expected_trxs);
   std::shared_ptr<simple_tps_monitor> monitor = std::make_shared<simple_tps_monitor>(expected_trxs);


   trx_tps_tester<mock_trx_generator, simple_tps_monitor> t1(generator, monitor, test_duration_s, test_tps);

   fc::time_point start = fc::time_point::now();
   t1.run();
   fc::time_point end = fc::time_point::now();
   fc::microseconds runtime_us = end.time_since_epoch() - start.time_since_epoch() ;

   BOOST_REQUIRE_EQUAL(generator->_calls.size(), expected_trxs);
   BOOST_REQUIRE_GT(runtime_us.count(), minimum_runtime_us);

   if (runtime_us.count() > maximum_runtime_us) {
      ilog("couldn't sustain transaction rate.  ran ${rt}us vs expected max ${mx}us",
           ("rt", runtime_us.count())("mx", maximum_runtime_us ) );
      BOOST_REQUIRE_LT(monitor->_calls.back().time_to_next_trx_us, 0);
   }

}

BOOST_AUTO_TEST_CASE(tps_short_run_med_tps_med_delay)
{
   constexpr uint32_t test_duration_s = 5;
   constexpr uint32_t test_tps = 10000;
   constexpr uint32_t trx_delay_us = 10;
   constexpr uint32_t expected_trxs = test_duration_s * test_tps;
   constexpr uint64_t expected_runtime_us = test_duration_s * 1000000;
   constexpr uint64_t allowable_runtime_deviation_per = 20;
   constexpr uint64_t allowable_runtime_deviation_us = expected_runtime_us / allowable_runtime_deviation_per;
   constexpr uint64_t minimum_runtime_us = expected_runtime_us - allowable_runtime_deviation_us;
   constexpr uint64_t maximum_runtime_us = expected_runtime_us + allowable_runtime_deviation_us;

   std::shared_ptr<mock_trx_generator> generator = std::make_shared<mock_trx_generator>(expected_trxs, trx_delay_us);
   std::shared_ptr<simple_tps_monitor> monitor = std::make_shared<simple_tps_monitor>(expected_trxs);


   trx_tps_tester<mock_trx_generator, simple_tps_monitor> t1(generator, monitor, test_duration_s, test_tps);

   fc::time_point start = fc::time_point::now();
   t1.run();
   fc::time_point end = fc::time_point::now();
   fc::microseconds runtime_us = end.time_since_epoch() - start.time_since_epoch() ;

   BOOST_REQUIRE_EQUAL(generator->_calls.size(), expected_trxs);
   BOOST_REQUIRE_GT(runtime_us.count(), minimum_runtime_us);

   if (runtime_us.count() > maximum_runtime_us) {
      ilog("couldn't sustain transaction rate.  ran ${rt}us vs expected max ${mx}us",
           ("rt", runtime_us.count())("mx", maximum_runtime_us ) );
      BOOST_REQUIRE_LT(monitor->_calls.back().time_to_next_trx_us, 0);
   }
}

BOOST_AUTO_TEST_CASE(tps_med_run_med_tps_med_delay)
{
   constexpr uint32_t test_duration_s = 30;
   constexpr uint32_t test_tps = 10000;
   constexpr uint32_t trx_delay_us = 10;
   constexpr uint32_t expected_trxs = test_duration_s * test_tps;
   constexpr uint64_t expected_runtime_us = test_duration_s * 1000000;
   constexpr uint64_t allowable_runtime_deviation_per = 20;
   constexpr uint64_t allowable_runtime_deviation_us = expected_runtime_us / allowable_runtime_deviation_per;
   constexpr uint64_t minimum_runtime_us = expected_runtime_us - allowable_runtime_deviation_us;
   constexpr uint64_t maximum_runtime_us = expected_runtime_us + allowable_runtime_deviation_us;

   std::shared_ptr<mock_trx_generator> generator = std::make_shared<mock_trx_generator>(expected_trxs, trx_delay_us);
   std::shared_ptr<simple_tps_monitor> monitor = std::make_shared<simple_tps_monitor>(expected_trxs);


   trx_tps_tester<mock_trx_generator, simple_tps_monitor> t1(generator, monitor, test_duration_s, test_tps);

   fc::time_point start = fc::time_point::now();
   t1.run();
   fc::time_point end = fc::time_point::now();
   fc::microseconds runtime_us = end.time_since_epoch() - start.time_since_epoch() ;

   BOOST_REQUIRE_EQUAL(generator->_calls.size(), expected_trxs);
   BOOST_REQUIRE_GT(runtime_us.count(), minimum_runtime_us);

   if (runtime_us.count() > maximum_runtime_us) {
      ilog("couldn't sustain transaction rate.  ran ${rt}us vs expected max ${mx}us",
           ("rt", runtime_us.count())("mx", maximum_runtime_us ) );
      BOOST_REQUIRE_LT(monitor->_calls.back().time_to_next_trx_us, 0);
   }
}
BOOST_AUTO_TEST_CASE(tps_cant_keep_up)
{
   constexpr uint32_t test_duration_s = 5;
   constexpr uint32_t test_tps = 100000;
   constexpr uint32_t trx_delay_us = 10;
   constexpr uint32_t expected_trxs = test_duration_s * test_tps;
   constexpr uint64_t expected_runtime_us = test_duration_s * 1000000;
   constexpr uint64_t allowable_runtime_deviation_per = 20;
   constexpr uint64_t allowable_runtime_deviation_us = expected_runtime_us / allowable_runtime_deviation_per;
   constexpr uint64_t minimum_runtime_us = expected_runtime_us - allowable_runtime_deviation_us;
   constexpr uint64_t maximum_runtime_us = expected_runtime_us + allowable_runtime_deviation_us;

   std::shared_ptr<mock_trx_generator> generator = std::make_shared<mock_trx_generator>(expected_trxs, trx_delay_us);
   std::shared_ptr<simple_tps_monitor> monitor = std::make_shared<simple_tps_monitor>(expected_trxs);


   trx_tps_tester<mock_trx_generator, simple_tps_monitor> t1(generator, monitor, test_duration_s, test_tps);

   fc::time_point start = fc::time_point::now();
   t1.run();
   fc::time_point end = fc::time_point::now();
   fc::microseconds runtime_us = end.time_since_epoch() - start.time_since_epoch() ;

   BOOST_REQUIRE_EQUAL(generator->_calls.size(), expected_trxs);
   BOOST_REQUIRE_GT(runtime_us.count(), minimum_runtime_us);

   if (runtime_us.count() > maximum_runtime_us) {
      ilog("couldn't sustain transaction rate.  ran ${rt}us vs expected max ${mx}us",
           ("rt", runtime_us.count())("mx", maximum_runtime_us ) );
      BOOST_REQUIRE_LT(monitor->_calls.back().time_to_next_trx_us, 0);
   }
}
BOOST_AUTO_TEST_CASE(tps_med_run_med_tps_30us_delay)
{
   constexpr uint32_t test_duration_s = 15;
   constexpr uint32_t test_tps = 3000;
   constexpr uint32_t trx_delay_us = 30;
   constexpr uint32_t expected_trxs = test_duration_s * test_tps;
   constexpr uint64_t expected_runtime_us = test_duration_s * 1000000;
   constexpr uint64_t allowable_runtime_deviation_per = 20;
   constexpr uint64_t allowable_runtime_deviation_us = expected_runtime_us / allowable_runtime_deviation_per;
   constexpr uint64_t minimum_runtime_us = expected_runtime_us - allowable_runtime_deviation_us;
   constexpr uint64_t maximum_runtime_us = expected_runtime_us + allowable_runtime_deviation_us;

   std::shared_ptr<mock_trx_generator> generator = std::make_shared<mock_trx_generator>(expected_trxs, trx_delay_us);
   std::shared_ptr<simple_tps_monitor> monitor = std::make_shared<simple_tps_monitor>(expected_trxs);


   trx_tps_tester<mock_trx_generator, simple_tps_monitor> t1(generator, monitor, test_duration_s, test_tps);

   fc::time_point start = fc::time_point::now();
   t1.run();
   fc::time_point end = fc::time_point::now();
   fc::microseconds runtime_us = end.time_since_epoch() - start.time_since_epoch() ;

   BOOST_REQUIRE_EQUAL(generator->_calls.size(), expected_trxs);
   BOOST_REQUIRE_GT(runtime_us.count(), minimum_runtime_us);

   if (runtime_us.count() > maximum_runtime_us) {
      ilog("couldn't sustain transaction rate.  ran ${rt}us vs expected max ${mx}us",
           ("rt", runtime_us.count())("mx", maximum_runtime_us ) );
      BOOST_REQUIRE_LT(monitor->_calls.back().time_to_next_trx_us, 0);
   }

}

BOOST_AUTO_TEST_CASE(tps_performance_monitor_during_spin_up)
{
   tps_test_stats stats;
   tps_performance_monitor monitor{std::chrono::microseconds(5s).count()};
   stats.total_trxs = 1000;
   stats.start_time = fc::time_point{fc::microseconds{0}};
   stats.expected_sent = 100;
   stats.trxs_sent = 90;

   // behind, but still within spin up window
   stats.last_run =  fc::time_point{fc::microseconds{100000}};
   BOOST_REQUIRE(monitor.monitor_test(stats));

   // violation, but still within spin up window
   stats.last_run =  fc::time_point{fc::microseconds{1100000}};
   BOOST_REQUIRE(monitor.monitor_test(stats));
}

BOOST_AUTO_TEST_CASE(tps_performance_monitor_outside_spin_up)
{
   tps_test_stats stats;
   tps_performance_monitor monitor{std::chrono::microseconds(5s).count()};
   stats.total_trxs = 1000;
   stats.start_time = fc::time_point{fc::microseconds{0}};
   stats.expected_sent = 100;
   stats.trxs_sent = 90;

   // behind, out of spin up window
   stats.last_run =  fc::time_point{fc::microseconds{5500000}};
   BOOST_REQUIRE(monitor.monitor_test(stats));

   // violation, out of spin up window
   stats.last_run =  fc::time_point{fc::microseconds{6600000}};
   BOOST_REQUIRE(!monitor.monitor_test(stats));
}

BOOST_AUTO_TEST_CASE(tps_performance_monitor_outside_spin_up_within_limit)
{
   tps_test_stats stats;
   tps_performance_monitor monitor{std::chrono::microseconds(5s).count()};
   stats.total_trxs = 1000;
   stats.start_time = fc::time_point{fc::microseconds{0}};
   stats.expected_sent = 100;
   stats.trxs_sent = 90;

   // outside of limit,  out of spin up window
   stats.last_run =  fc::time_point{fc::microseconds{5500000}};
   BOOST_REQUIRE(monitor.monitor_test(stats));

   // outside of limit, less than max violation duration
   stats.last_run =  fc::time_point{fc::microseconds{6000000}};
   BOOST_REQUIRE(monitor.monitor_test(stats));

   stats.trxs_sent = 98;
   // behind, but within limit, out of spin up window
   stats.last_run =  fc::time_point{fc::microseconds{6600000}};
   BOOST_REQUIRE(monitor.monitor_test(stats));

   stats.expected_sent = 150;
   // outside of limit again, out of spin up window
   stats.last_run =  fc::time_point{fc::microseconds{7000000}};
   BOOST_REQUIRE(monitor.monitor_test(stats));

   // outside of limit for too long
   stats.last_run =  fc::time_point{fc::microseconds{8100000}};
   BOOST_REQUIRE(!monitor.monitor_test(stats));
}

BOOST_AUTO_TEST_CASE(tps_cant_keep_up_monitored)
{
   constexpr uint32_t test_duration_s = 5;
   constexpr uint32_t test_tps = 100000;
   constexpr uint32_t trx_delay_us = 10;
   constexpr uint32_t expected_trxs = test_duration_s * test_tps;
   constexpr uint64_t expected_runtime_us = test_duration_s * 1000000;

   std::shared_ptr<mock_trx_generator> generator = std::make_shared<mock_trx_generator>(expected_trxs, trx_delay_us);
   std::shared_ptr<tps_performance_monitor> monitor = std::make_shared<tps_performance_monitor>();


   trx_tps_tester<mock_trx_generator, tps_performance_monitor> t1(generator, monitor, test_duration_s, test_tps);

   fc::time_point start = fc::time_point::now();
   t1.run();
   fc::time_point end = fc::time_point::now();
   fc::microseconds runtime_us = end.time_since_epoch() - start.time_since_epoch() ;

   BOOST_REQUIRE_LT(runtime_us.count(), expected_runtime_us);
   BOOST_REQUIRE_LT(generator->_calls.size(), expected_trxs);

}

BOOST_AUTO_TEST_CASE(trx_generator_constructor)
{
   std::string chain_id = "999";
   std::string contract_owner_account = "eosio";
   std::string acct = "aaa";
   std::string action_name = "transfer";
   const std::string action_data = "{\"from\":\"aaa\",\"to\":\"bbb\",\"quantity\":\"10.0000 SYS\",\"memo\":\"hello\"}";
   const std::string abi_file = "../../unittests/contracts/eosio.token/eosio.token.abi";
   fc::microseconds trx_expr = fc::seconds(3600);
   std::string log_dir = ".";
   std::string lib_id_str = "00000062989f69fd251df3e0b274c3364ffc2f4fce73de3f1c7b5e11a4c92f21";
   std::string private_key_str = "5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3";
   std::string peer_endpoint = "127.0.0.1";
   unsigned short port = 9876;
   bool stop_on_trx_failed = true;

   auto generator = trx_generator(chain_id, abi_file, contract_owner_account, private_key_str, acct, action_name, action_data, trx_expr, private_key_str, lib_id_str, log_dir, stop_on_trx_failed, peer_endpoint, port);
}

BOOST_AUTO_TEST_SUITE_END()

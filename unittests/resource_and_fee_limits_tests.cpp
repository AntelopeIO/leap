#include <algorithm>

#include <eosio/chain/config.hpp>
#include <eosio/chain/resource_limits.hpp>
#include <eosio/testing/chainbase_fixture.hpp>
#include <eosio/testing/tester.hpp>

#include <boost/test/unit_test.hpp>

using namespace eosio::chain::resource_limits;
using namespace eosio::testing;
using namespace eosio::chain;


class resource_limits_fixture: private chainbase_fixture<1024*1024>, public tester, public resource_limits_manager
{
   public:
      resource_limits_fixture()
      :chainbase_fixture(), tester( setup_policy::full )
      ,resource_limits_manager(*tester::control, *chainbase_fixture::_db)
      {
         add_indices();
         initialize_database();
         add_fee_params_db();
      }

      ~resource_limits_fixture() {}

      chainbase::database::session start_session() {
         return chainbase_fixture::_db->start_undo_session(true);
      }
};

constexpr uint64_t expected_elastic_iterations(uint64_t from, uint64_t to, uint64_t rate_num, uint64_t rate_den ) {
   uint64_t result = 0;
   uint64_t cur = from;

   while((from < to && cur < to) || (from > to && cur > to)) {
      cur = cur * rate_num / rate_den;
      result += 1;
   }

   return result;
}


constexpr uint64_t expected_exponential_average_iterations( uint64_t from, uint64_t to, uint64_t value, uint64_t window_size ) {
   uint64_t result = 0;
   uint64_t cur = from;

   while((from < to && cur < to) || (from > to && cur > to)) {
      cur = cur * (uint64_t)(window_size - 1) / (uint64_t)(window_size);
      cur += value / (uint64_t)(window_size);
      result += 1;
   }

   return result;
}

BOOST_AUTO_TEST_SUITE(resource_and_fee_limits_tests)

   /**
    * Test to make sure that the elastic limits for blocks relax and contract as expected
    */
   BOOST_FIXTURE_TEST_CASE(elastic_cpu_relax_contract, resource_limits_fixture) try {

      const uint64_t desired_virtual_limit = config::default_max_block_cpu_usage * config::maximum_elastic_resource_multiplier;
      const uint64_t expected_relax_iterations = expected_elastic_iterations( config::default_max_block_cpu_usage, desired_virtual_limit, 1000, 999 );

      // this is enough iterations for the average to reach/exceed the target (triggering congestion handling) and then the iterations to contract down to the min
      // subtracting 1 for the iteration that pulls double duty as reaching/exceeding the target and starting congestion handling
      const uint64_t expected_contract_iterations =
              expected_exponential_average_iterations(0, EOS_PERCENT(config::default_max_block_cpu_usage, config::default_target_block_cpu_usage_pct), config::default_max_block_cpu_usage, config::block_cpu_usage_average_window_ms / config::block_interval_ms ) +
              expected_elastic_iterations( desired_virtual_limit, config::default_max_block_cpu_usage, 99, 100 ) - 1;

      const account_name account(1);
      initialize_account(account, false);
      set_account_limits(account, -1, -1, -1, false);
      process_account_limit_updates();

      // relax from the starting state (congested) to the idle state as fast as possible
      uint32_t iterations = 0;
      while( get_virtual_block_cpu_limit() < desired_virtual_limit && iterations <= expected_relax_iterations ) {
         add_transaction_usage_and_fees({account},0,0, -1, -1, iterations);
         process_block_usage(iterations++);
      }

      BOOST_REQUIRE_EQUAL(iterations, expected_relax_iterations);
      BOOST_REQUIRE_EQUAL(get_virtual_block_cpu_limit(), desired_virtual_limit);

      // push maximum resources to go from idle back to congested as fast as possible
      while( get_virtual_block_cpu_limit() > config::default_max_block_cpu_usage
              && iterations <= expected_relax_iterations + expected_contract_iterations ) {
         add_transaction_usage_and_fees({account}, config::default_max_block_cpu_usage, 0, -1, -1, iterations);
         process_block_usage(iterations++);
      }

      BOOST_REQUIRE_EQUAL(iterations, expected_relax_iterations + expected_contract_iterations);
      BOOST_REQUIRE_EQUAL(get_virtual_block_cpu_limit(), config::default_max_block_cpu_usage);
   } FC_LOG_AND_RETHROW();

   /**
    * Test to make sure that the elastic limits for blocks relax and contract as expected
    */
   BOOST_FIXTURE_TEST_CASE(elastic_net_relax_contract, resource_limits_fixture) try {
      const uint64_t desired_virtual_limit = config::default_max_block_net_usage * config::maximum_elastic_resource_multiplier;
      const uint64_t expected_relax_iterations = expected_elastic_iterations( config::default_max_block_net_usage, desired_virtual_limit, 1000, 999 );

      // this is enough iterations for the average to reach/exceed the target (triggering congestion handling) and then the iterations to contract down to the min
      // subtracting 1 for the iteration that pulls double duty as reaching/exceeding the target and starting congestion handling
      const uint64_t expected_contract_iterations =
              expected_exponential_average_iterations(0, EOS_PERCENT(config::default_max_block_net_usage, config::default_target_block_net_usage_pct), config::default_max_block_net_usage, config::block_size_average_window_ms / config::block_interval_ms ) +
              expected_elastic_iterations( desired_virtual_limit, config::default_max_block_net_usage, 99, 100 ) - 1;

      const account_name account(1);
      initialize_account(account, false);
      set_account_limits(account, -1, -1, -1, false);
      process_account_limit_updates();

      // relax from the starting state (congested) to the idle state as fast as possible
      uint32_t iterations = 0;
      while( get_virtual_block_net_limit() < desired_virtual_limit && iterations <= expected_relax_iterations ) {
         add_transaction_usage_and_fees({account},0,0, -1, -1,iterations);
         process_block_usage(iterations++);
      }

      BOOST_REQUIRE_EQUAL(iterations, expected_relax_iterations);
      BOOST_REQUIRE_EQUAL(get_virtual_block_net_limit(), desired_virtual_limit);

      // push maximum resources to go from idle back to congested as fast as possible
      while( get_virtual_block_net_limit() > config::default_max_block_net_usage
              && iterations <= expected_relax_iterations + expected_contract_iterations ) {
         add_transaction_usage_and_fees({account},0, config::default_max_block_net_usage, -1, -1, iterations);
         process_block_usage(iterations++);
      }

      BOOST_REQUIRE_EQUAL(iterations, expected_relax_iterations + expected_contract_iterations);
      BOOST_REQUIRE_EQUAL(get_virtual_block_net_limit(), config::default_max_block_net_usage);
   } FC_LOG_AND_RETHROW();

   /**
    * create 5 accounts with different weights, verify that the capacities are as expected and that usage properly enforces them
    */


// TODO: restore weighted capacity cpu tests
#if 0
   BOOST_FIXTURE_TEST_CASE(weighted_capacity_cpu, resource_limits_fixture) try {
      const vector<int64_t> weights = { 234, 511, 672, 800, 1213 };
      const int64_t total = std::accumulate(std::begin(weights), std::end(weights), 0LL);
      vector<int64_t> expected_limits;
      std::transform(std::begin(weights), std::end(weights), std::back_inserter(expected_limits), [total](const auto& v){ return v * config::default_max_block_cpu_usage / total; });

      for (int64_t idx = 0; idx < weights.size(); idx++) {
         const account_name account(idx + 100);
         initialize_account(account, false);
         set_account_limits(account, -1, -1, weights.at(idx), false);
      }

      process_account_limit_updates();

      for (int64_t idx = 0; idx < weights.size(); idx++) {
         const account_name account(idx + 100);
         BOOST_CHECK_EQUAL(get_account_cpu_limit(account), expected_limits.at(idx));

         {  // use the expected limit, should succeed ... roll it back
            auto s = start_session();
            add_transaction_usage_and_fees({account}, expected_limits.at(idx), 0, -1, -1, 0);
            s.undo();
         }

         // use too much, and expect failure;
         BOOST_REQUIRE_THROW(add_transaction_usage_and_fees({account}, expected_limits.at(idx) + 1, 0, -1, -1, 0), tx_cpu_usage_exceeded);
      }
   } FC_LOG_AND_RETHROW();

   /**
    * create 5 accounts with different weights, verify that the capacities are as expected and that usage properly enforces them
    */
   BOOST_FIXTURE_TEST_CASE(weighted_capacity_net, resource_limits_fixture) try {
      const vector<int64_t> weights = { 234, 511, 672, 800, 1213 };
      const int64_t total = std::accumulate(std::begin(weights), std::end(weights), 0LL);
      vector<int64_t> expected_limits;
      std::transform(std::begin(weights), std::end(weights), std::back_inserter(expected_limits), [total](const auto& v){ return v * config::default_max_block_net_usage / total; });

      for (int64_t idx = 0; idx < weights.size(); idx++) {
         const account_name account(idx + 100);
         initialize_account(account, false);
         set_account_limits(account, -1, weights.at(idx), -1, false);
      }

      process_account_limit_updates();

      for (int64_t idx = 0; idx < weights.size(); idx++) {
         const account_name account(idx + 100);
         BOOST_CHECK_EQUAL(get_account_net_limit(account), expected_limits.at(idx));

         {  // use the expected limit, should succeed ... roll it back
            auto s = start_session();
            add_transaction_usage_and_fees({account}, 0, expected_limits.at(idx), -1, -1, 0);
            s.undo();
         }

         // use too much, and expect failure;
         BOOST_REQUIRE_THROW(add_transaction_usage_and_fees({account}, 0, expected_limits.at(idx) + 1, -1, -1, 0), tx_net_usage_exceeded);
      }
   } FC_LOG_AND_RETHROW();
#endif

   BOOST_FIXTURE_TEST_CASE(enforce_block_limits_cpu, resource_limits_fixture) try {
      const account_name account(1);
      initialize_account(account, false);
      set_account_limits(account, -1, -1, -1, false );
      process_account_limit_updates();

      const uint64_t increment = 1000;
      const uint64_t expected_iterations = config::default_max_block_cpu_usage / increment;

      for (uint64_t idx = 0; idx < expected_iterations; idx++) {
         add_transaction_usage_and_fees({account}, increment, 0, -1, -1,  0);
      }

      BOOST_REQUIRE_THROW(add_transaction_usage_and_fees({account}, increment, 0, -1, -1, 0), block_resource_exhausted);

   } FC_LOG_AND_RETHROW();

   BOOST_FIXTURE_TEST_CASE(enforce_block_limits_net, resource_limits_fixture) try {
      const account_name account(1);
      initialize_account(account, false);
      set_account_limits(account, -1, -1, -1, false );
      process_account_limit_updates();

      const uint64_t increment = 1000;
      const uint64_t expected_iterations = config::default_max_block_net_usage / increment;

      for (uint64_t idx = 0; idx < expected_iterations; idx++) {
         add_transaction_usage_and_fees({account}, 0, increment, -1, -1, 0);
      }

      BOOST_REQUIRE_THROW(add_transaction_usage_and_fees({account}, 0, increment, -1, -1, 0), block_resource_exhausted);

   } FC_LOG_AND_RETHROW();

   BOOST_FIXTURE_TEST_CASE(enforce_account_ram_limit, resource_limits_fixture) try {
      const uint64_t limit = 1000;
      const uint64_t increment = 77;
      const uint64_t expected_iterations = (limit + increment - 1 ) / increment;


      const account_name account(1);
      initialize_account(account, false);
      set_account_limits(account, limit, -1, -1, false );
      process_account_limit_updates();

      for (uint64_t idx = 0; idx < expected_iterations - 1; idx++) {
         add_pending_ram_usage(account, increment);
         verify_account_ram_usage(account);
      }

      add_pending_ram_usage(account, increment);
      BOOST_REQUIRE_THROW(verify_account_ram_usage(account), ram_usage_exceeded);
   } FC_LOG_AND_RETHROW();

   BOOST_FIXTURE_TEST_CASE(enforce_account_ram_limit_underflow, resource_limits_fixture) try {
      const account_name account(1);
      initialize_account(account, false);
      set_account_limits(account, 100, -1, -1, false );
      verify_account_ram_usage(account);
      process_account_limit_updates();
      BOOST_REQUIRE_THROW(add_pending_ram_usage(account, -101), transaction_exception);

   } FC_LOG_AND_RETHROW();

   BOOST_FIXTURE_TEST_CASE(enforce_account_ram_limit_overflow, resource_limits_fixture) try {
      const account_name account(1);
      initialize_account(account, false);
      set_account_limits(account, UINT64_MAX, -1, -1, false );
      verify_account_ram_usage(account);
      process_account_limit_updates();
      add_pending_ram_usage(account, UINT64_MAX/2);
      verify_account_ram_usage(account);
      add_pending_ram_usage(account, UINT64_MAX/2);
      verify_account_ram_usage(account);
      BOOST_REQUIRE_THROW(add_pending_ram_usage(account, 2), transaction_exception);

   } FC_LOG_AND_RETHROW();

   BOOST_FIXTURE_TEST_CASE(enforce_account_ram_commitment, resource_limits_fixture) try {
      const int64_t limit = 1000;
      const int64_t commit = 600;
      const int64_t increment = 77;
      const int64_t expected_iterations = (limit - commit + increment - 1 ) / increment;


      const account_name account(1);
      initialize_account(account, false);
      set_account_limits(account, limit, -1, -1, false );
      process_account_limit_updates();
      add_pending_ram_usage(account, commit);
      verify_account_ram_usage(account);

      for (int idx = 0; idx < expected_iterations - 1; idx++) {
         set_account_limits(account, limit - increment * idx, -1, -1, false);
         verify_account_ram_usage(account);
         process_account_limit_updates();
      }

      set_account_limits(account, limit - increment * expected_iterations, -1, -1, false);
      BOOST_REQUIRE_THROW(verify_account_ram_usage(account), ram_usage_exceeded);
   } FC_LOG_AND_RETHROW();


   BOOST_FIXTURE_TEST_CASE(sanity_check, resource_limits_fixture) try {
      int64_t  total_staked_tokens = 1'000'000'000'0000ll;
      int64_t  user_stake = 1'0000ll;
      uint64_t max_block_cpu = 200'000.; // us;
      uint64_t blocks_per_day = 2*60*60*24;
      uint64_t total_cpu_per_period = max_block_cpu * blocks_per_day;

      double congested_cpu_time_per_period = (double(total_cpu_per_period) * user_stake) / total_staked_tokens;
      wdump((congested_cpu_time_per_period));
      double uncongested_cpu_time_per_period = congested_cpu_time_per_period * config::maximum_elastic_resource_multiplier;
      wdump((uncongested_cpu_time_per_period));

      initialize_account( "dan"_n, false );
      initialize_account( "everyone"_n, false );
      set_account_limits( "dan"_n, 0, 0, user_stake, false );
      set_account_limits( "everyone"_n, 0, 0, (total_staked_tokens - user_stake), false );
      process_account_limit_updates();

      // dan cannot consume more than 34 us per day
      BOOST_REQUIRE_THROW( add_transaction_usage_and_fees( {"dan"_n}, 35, 0, -1, -1, 1 ), tx_cpu_usage_exceeded );

      // Ensure CPU usage is 0 by "waiting" for one day's worth of blocks to pass.
      add_transaction_usage_and_fees( {"dan"_n}, 0, 0, -1, -1, 1 + blocks_per_day );

      // But dan should be able to consume up to 34 us per day.
      add_transaction_usage_and_fees( {"dan"_n}, 34, 0, -1, -1, 2 + blocks_per_day );
   } FC_LOG_AND_RETHROW();

   BOOST_FIXTURE_TEST_CASE(charge_tx_fee_cpu, resource_limits_fixture) try {
      initialize_account( "alice"_n, false );
      initialize_account( "everyone"_n, false );
      uint64_t cpu_usage = 123;
      int64_t  alice_stake = 50'0000;
      int64_t  everyone_stake = 100'000'000'0000;
      set_account_limits( "alice"_n, 0, 0, alice_stake, false );
      set_account_limits( "everyone"_n, 0, 0, everyone_stake, false );
      process_account_limit_updates();
      // alice send transactions until there are no available cpu resource
      auto iterations = 0;
      while(uint64_t(get_account_cpu_limit("alice"_n).first) > cpu_usage){
         add_transaction_usage_and_fees( {"alice"_n}, cpu_usage, 0, -1, -1, iterations );
         process_block_usage(iterations++);
      }
      // alice cannot consume more than 123 us
      BOOST_REQUIRE_THROW( add_transaction_usage_and_fees( {"alice"_n}, cpu_usage, 0, -1, -1, iterations++ ), tx_cpu_usage_exceeded );
      BOOST_REQUIRE_THROW( add_transaction_usage_and_fees( {"alice"_n}, cpu_usage, 0, -1, -1, iterations++ ), tx_cpu_usage_exceeded );
      // alice enable charging fee and without limitation
      config_account_fee_limits("alice"_n, -1, -1, false);
      // system contract set the staked cpu resource by alice
      set_account_fee_limits("alice"_n, 0, alice_stake, false);

      auto cpu_consumed_fee1 = get_cpu_usage_fee_to_bill(cpu_usage);
      // alice retry send transaction
      add_transaction_usage_and_fees({"alice"_n}, cpu_usage, 0, cpu_consumed_fee1, -1, iterations++);
      int64_t net_pending_weight1, cpu_consumed_weight1;
      get_account_fee_consumption("alice"_n, net_pending_weight1, cpu_consumed_weight1);

      // alice should consumed cpu fee
      BOOST_REQUIRE_EQUAL(cpu_consumed_weight1, cpu_consumed_fee1);
      BOOST_REQUIRE_EQUAL(net_pending_weight1, 0);

      // alice send one more transaction
      auto cpu_consumed_fee2 = get_cpu_usage_fee_to_bill(cpu_usage*2);
      add_transaction_usage_and_fees({"alice"_n}, cpu_usage*2, 0, cpu_consumed_fee2, -1, iterations++);
      int64_t net_pending_weight2, cpu_consumed_weight2;
      get_account_fee_consumption("alice"_n, net_pending_weight2, cpu_consumed_weight2);

      // the comsumed fee should be accumulated
      BOOST_REQUIRE_EQUAL(cpu_consumed_weight2, cpu_consumed_fee1 + cpu_consumed_fee2);
      BOOST_REQUIRE_EQUAL(net_pending_weight2, 0);

   } FC_LOG_AND_RETHROW();

   BOOST_FIXTURE_TEST_CASE(charge_tx_fee_net, resource_limits_fixture) try {
      initialize_account( "alice"_n, false );
      initialize_account( "everyone"_n, false );
      uint64_t net_usage = 321;
      int64_t  alice_stake = 50'0000;
      int64_t  everyone_stake = 10'000'000'000'0000;
      set_account_limits( "alice"_n, 0, alice_stake, 0, false);
      set_account_limits( "everyone"_n, 0, everyone_stake, 0, false);
      process_account_limit_updates();
      // alice send transactions until there are no available net resource
      auto iterations = 0;
      while(uint64_t(get_account_net_limit("alice"_n).first) > net_usage){
         add_transaction_usage_and_fees( {"alice"_n}, 0, net_usage, -1, -1, iterations );
         process_block_usage(iterations++);
      }
      // alice cannot consume more than 1234 net
      BOOST_REQUIRE_THROW( add_transaction_usage_and_fees( {"alice"_n}, 0, net_usage, -1, -1, iterations++ ), tx_net_usage_exceeded );
      // alice enable charging fee and without limitation
      config_account_fee_limits("alice"_n, -1, -1, false);
      // system contract set the staked resource by alice
      set_account_fee_limits("alice"_n, alice_stake, 0, false);

      auto net_consumed_fee1 = get_net_usage_fee_to_bill(net_usage);
      // alice retry send transaction
      add_transaction_usage_and_fees({"alice"_n}, 0, net_usage, -1, net_consumed_fee1, iterations++);
      int64_t net_pending_weight1, cpu_consumed_weight1;
      get_account_fee_consumption("alice"_n, net_pending_weight1, cpu_consumed_weight1);

      // alice should consumed net fee
      BOOST_REQUIRE_EQUAL(cpu_consumed_weight1, 0);
      BOOST_REQUIRE_EQUAL(net_pending_weight1, net_consumed_fee1);

      // alice send one more transaction
      auto net_consumed_fee2 = get_net_usage_fee_to_bill(net_usage*2);
      add_transaction_usage_and_fees({"alice"_n}, 0, net_usage*2, -1, net_consumed_fee2, iterations++);
      int64_t net_pending_weight2, cpu_consumed_weight2;
      get_account_fee_consumption("alice"_n, net_pending_weight2, cpu_consumed_weight2);
      // the comsumed fee should be accumulated
      BOOST_REQUIRE_EQUAL(cpu_consumed_weight2, 0);
      BOOST_REQUIRE_EQUAL(net_pending_weight2, net_consumed_fee1 + net_consumed_fee2);

   } FC_LOG_AND_RETHROW();

   BOOST_FIXTURE_TEST_CASE(throw_if_insufficient_staked_cpu_fee_to_pay_cpu_fee, resource_limits_fixture) try {
      initialize_account( "alice"_n, false );
      initialize_account( "everyone"_n, false );
      uint64_t cpu_usage = 123;
      int64_t  alice_stake = 50'0000;
      int64_t  everyone_stake = 100'000'000'0000;
      set_account_limits( "alice"_n, 0, 0, alice_stake, false );
      set_account_limits( "everyone"_n, 0, 0, everyone_stake, false );
      process_account_limit_updates();
      // alice send transactions until there are no available cpu resource
      auto iterations = 0;
      while(uint64_t(get_account_cpu_limit("alice"_n).first) > cpu_usage){
         add_transaction_usage_and_fees( {"alice"_n}, cpu_usage, 0, -1, -1, iterations );
         process_block_usage(iterations++);
      }
      // alice cannot consume more than 123 us
      BOOST_REQUIRE_THROW( add_transaction_usage_and_fees( {"alice"_n}, cpu_usage, 0, -1, -1, iterations++ ), tx_cpu_usage_exceeded );
      // alice enable charging fee and without limitation
      config_account_fee_limits("alice"_n, -1, -1, false);
      // system contract set the staked resource by alice
      set_account_fee_limits("alice"_n, 0, alice_stake, false);
      int64_t cpu_weight, x, y;
      get_account_limits( "alice"_n, x, y, cpu_weight );

      int64_t net_weight_consumption, cpu_weight_consumption;
      get_account_fee_consumption("alice"_n, net_weight_consumption, cpu_weight_consumption);

      auto cpu_consumed_fee_per_tx = get_cpu_usage_fee_to_bill(cpu_usage);
      // alice sends transactions until there no more cpu_weight to comsume
      while (
         cpu_weight_consumption + cpu_consumed_fee_per_tx <
         cpu_weight
      ){
         add_transaction_usage_and_fees({"alice"_n}, cpu_usage, 0, cpu_consumed_fee_per_tx, -1, iterations);
         process_block_usage(iterations++);
         get_account_fee_consumption("alice"_n, net_weight_consumption, cpu_weight_consumption);
      }

      // alice send one more transaction
      cpu_consumed_fee_per_tx = get_cpu_usage_fee_to_bill(cpu_usage);
      // alice cannot consume more than 123 us
      BOOST_REQUIRE_THROW( add_transaction_usage_and_fees( {"alice"_n}, cpu_usage, 0, cpu_consumed_fee_per_tx, -1, iterations ), tx_cpu_fee_exceeded );
   } FC_LOG_AND_RETHROW();

   BOOST_FIXTURE_TEST_CASE(throw_if_insufficient_staked_net_fee_to_pay_net_fee, resource_limits_fixture) try {
      initialize_account( "alice"_n, false );
      initialize_account( "everyone"_n, false );
      uint64_t net_usage = 321;
      int64_t  alice_stake = 50'0000;
      int64_t  everyone_stake = 10'000'000'000'0000;
      set_account_limits( "alice"_n, 0, alice_stake, 0, false);
      set_account_limits( "everyone"_n, 0, everyone_stake, 0, false);
      process_account_limit_updates();
      // alice send transactions until there are no available net resource
      auto iterations = 0;
      while(uint64_t(get_account_net_limit("alice"_n).first) > net_usage){
         add_transaction_usage_and_fees( {"alice"_n}, 0, net_usage, -1, -1, iterations );
         process_block_usage(iterations++);
      }
      // alice cannot consume more than 1234 net
      BOOST_REQUIRE_THROW( add_transaction_usage_and_fees( {"alice"_n}, 0, net_usage, -1, -1, iterations++ ), tx_net_usage_exceeded );
      // alice enable charging fee and without limitation
      config_account_fee_limits("alice"_n, -1, -1, false);
      // system contract set the staked resource by alice
      set_account_fee_limits("alice"_n, alice_stake, 0, false);
      
      int64_t net_weight, x, y;
      get_account_limits( "alice"_n, x, net_weight, y );
      int64_t net_weight_consumption, cpu_weight_consumption;
      get_account_fee_consumption("alice"_n, net_weight_consumption, cpu_weight_consumption);
      auto net_consumed_fee_per_tx = get_net_usage_fee_to_bill(net_usage);
      // alice sends transactions until there no more net_weight to comsume
      while (
         net_weight_consumption + net_consumed_fee_per_tx <
         net_weight
      ){
         add_transaction_usage_and_fees({"alice"_n}, 0, net_usage, -1, net_consumed_fee_per_tx, iterations);
         process_block_usage(iterations++);
         get_account_fee_consumption("alice"_n, net_weight_consumption, cpu_weight_consumption);
         net_consumed_fee_per_tx = get_net_usage_fee_to_bill(net_usage);
      }
      // alice send one more transaction
      net_consumed_fee_per_tx = get_net_usage_fee_to_bill(net_usage);
      // alice cannot consume more net
      BOOST_REQUIRE_THROW( add_transaction_usage_and_fees( {"alice"_n}, 0, net_usage, -1, net_consumed_fee_per_tx, iterations ), tx_net_fee_exceeded );

   } FC_LOG_AND_RETHROW();

   BOOST_FIXTURE_TEST_CASE(clear_cpu_comsumed_fee, resource_limits_fixture) try {
      initialize_account( "alice"_n, false );
      initialize_account( "everyone"_n, false );
      uint64_t cpu_usage = 123;
      int64_t  alice_stake = 50'0000;
      int64_t  everyone_stake = 100'000'000'0000;
      set_account_limits( "alice"_n, 0, 0, alice_stake, false );
      set_account_limits( "everyone"_n, 0, 0, everyone_stake, false );
      process_account_limit_updates();
      // alice send transactions until there are no available cpu resource
      auto iterations = 0;
      while(uint64_t(get_account_cpu_limit("alice"_n).first) > cpu_usage){
         add_transaction_usage_and_fees( {"alice"_n}, cpu_usage, 0, -1, -1, iterations );
         process_block_usage(iterations++);
      }
      // alice cannot consume more than 123 us
      BOOST_REQUIRE_THROW( add_transaction_usage_and_fees( {"alice"_n}, cpu_usage, 0, -1, -1, iterations++ ), tx_cpu_usage_exceeded );
      // alice enable charging fee and without limitation
      config_account_fee_limits("alice"_n, -1, -1, false);
      // system contract set the staked resource by alice
      set_account_fee_limits("alice"_n, 0, alice_stake, false);

      auto cpu_consumed_fee = get_cpu_usage_fee_to_bill(cpu_usage);
      // alice retry send transaction
      add_transaction_usage_and_fees({"alice"_n}, cpu_usage, 0, cpu_consumed_fee, -1, iterations++);
      int64_t net_weight_consumption, cpu_weight_consumption;
      get_account_fee_consumption("alice"_n, net_weight_consumption, cpu_weight_consumption);

      // alice should consumed cpu fee
      BOOST_REQUIRE_EQUAL(cpu_weight_consumption, cpu_consumed_fee);
      BOOST_REQUIRE_EQUAL(net_weight_consumption, 0);

      set_account_fee_limits("alice"_n, 0, alice_stake, false);
      get_account_fee_consumption("alice"_n, net_weight_consumption, cpu_weight_consumption);
      BOOST_REQUIRE_EQUAL(cpu_weight_consumption, 0);
      BOOST_REQUIRE_EQUAL(net_weight_consumption, 0);
   } FC_LOG_AND_RETHROW();

   BOOST_FIXTURE_TEST_CASE(throw_if_cpu_comsumed_fee_exceed_maximum_fee, resource_limits_fixture) try {
      initialize_account( "alice"_n, false );
      initialize_account( "everyone"_n, false );
      uint64_t cpu_usage = 123;
      int64_t  alice_stake = 50'0000;
      int64_t  everyone_stake = 100'000'000'0000;
      set_account_limits( "alice"_n, 0, 0, alice_stake, false );
      set_account_limits( "everyone"_n, 0, 0, everyone_stake, false );
      process_account_limit_updates();
      // alice send transactions until there are no available cpu resource
      auto iterations = 0;
      while(uint64_t(get_account_cpu_limit("alice"_n).first) > cpu_usage){
         add_transaction_usage_and_fees( {"alice"_n}, cpu_usage, 0, -1, -1, iterations );
         process_block_usage(iterations++);
      }
      // alice cannot consume more than 123 us
      BOOST_REQUIRE_THROW( add_transaction_usage_and_fees( {"alice"_n}, cpu_usage, 0, -1, -1, iterations++ ), tx_cpu_usage_exceeded );

      auto tx_fee_limit = 12;
      // set maximum fee per transaction, unlimited max fee
      config_account_fee_limits("alice"_n, 1, -1, false);
      BOOST_REQUIRE_THROW( add_transaction_usage_and_fees( {"alice"_n}, cpu_usage, 0, -1, -1, iterations++ ), tx_cpu_usage_exceeded );

      // alice enable charging fee and config maximum fee per transaction
      config_account_fee_limits("alice"_n, tx_fee_limit, -1, false);
      // system contract set the staked resource by alice
      set_account_fee_limits("alice"_n, 0, alice_stake, false);

      // should throw if the transaction fee exceeds max fee per transaction configuration
      auto cpu_consumed_fee = get_cpu_usage_fee_to_bill(cpu_usage);
      BOOST_REQUIRE_THROW( add_transaction_usage_and_fees( {"alice"_n}, cpu_usage, 0, cpu_consumed_fee, -1, iterations++ ), max_tx_fee_exceeded );
      
      // update maximum fee per tx
      config_account_fee_limits("alice"_n, cpu_consumed_fee, -1, false);
      // alice should able to send transaction
      add_transaction_usage_and_fees({"alice"_n}, cpu_usage, 0, cpu_consumed_fee, -1, iterations++);
      
      int64_t net_weight_consumption, cpu_weight_consumption;
      get_account_fee_consumption("alice"_n, net_weight_consumption, cpu_weight_consumption);
      cpu_consumed_fee = get_cpu_usage_fee_to_bill(cpu_usage);
      // alice set max fee is 20'0000 weight
      auto account_fee_limit = 200000;
      config_account_fee_limits("alice"_n, -1, account_fee_limit, false);
      // send transactions until the consumed fee reach to near the maximum fee
      while (
         account_fee_limit >= cpu_consumed_fee + cpu_weight_consumption + net_weight_consumption
      ) {
         add_transaction_usage_and_fees({"alice"_n}, cpu_usage, 0, cpu_consumed_fee, -1, iterations++);
         get_account_fee_consumption("alice"_n, net_weight_consumption, cpu_weight_consumption);
         cpu_consumed_fee = get_cpu_usage_fee_to_bill(cpu_usage);
      }
      // alice should not push transaction exceeds max fee limit
      BOOST_REQUIRE_THROW( add_transaction_usage_and_fees( {"alice"_n}, cpu_usage, 0, cpu_consumed_fee, -1, iterations++ ), max_account_fee_exceeded );
      
      // config new maximum fee
      account_fee_limit = 300000;
      config_account_fee_limits("alice"_n, -1, account_fee_limit, false);

      // alice should able to send transaction
      add_transaction_usage_and_fees({"alice"_n}, cpu_usage, 0, cpu_consumed_fee, -1, iterations++);
   } FC_LOG_AND_RETHROW()

   BOOST_FIXTURE_TEST_CASE(charge_zero_fee_cpu_if_ema_resource_smaller_than_threashold, resource_limits_fixture) try {
      initialize_account( "alice"_n, false );
      initialize_account( "everyone"_n, false );
      uint64_t cpu_usage = 123;
      int64_t  alice_stake = 50'0000;
      int64_t  everyone_stake = 100'000'000'0000;
      set_account_limits( "alice"_n, 0, 0, alice_stake, false );
      set_account_limits( "everyone"_n, 0, 0, everyone_stake, false );
      process_account_limit_updates();
      // alice send transactions until there are no available cpu resource
      auto iterations = 0;
      while(uint64_t(get_account_cpu_limit("alice"_n).first) > cpu_usage){
         add_transaction_usage_and_fees( {"alice"_n}, cpu_usage, 0, -1, -1, iterations );
         process_block_usage(iterations++);
      }
      // alice cannot consume more than 123 us
      BOOST_REQUIRE_THROW( add_transaction_usage_and_fees( {"alice"_n}, cpu_usage, 0, -1, -1, iterations++ ), tx_cpu_usage_exceeded );
      BOOST_REQUIRE_THROW( add_transaction_usage_and_fees( {"alice"_n}, cpu_usage, 0, -1, -1, iterations++ ), tx_cpu_usage_exceeded );
      // set fee threshold that is always return zero fee
      set_fee_parameters(50000000000, 199'999, 50000000000, 0);
      // alice enable charging fee and without limitation
      config_account_fee_limits("alice"_n, -1, -1, false);
      // system contract set the staked cpu resource by alice
      set_account_fee_limits("alice"_n, 0, alice_stake, false);

      auto cpu_consumed_fee = get_cpu_usage_fee_to_bill(cpu_usage);
      // charge zero fee
      BOOST_REQUIRE_EQUAL(cpu_consumed_fee, 0);

      // alice retry send transaction
      add_transaction_usage_and_fees({"alice"_n}, cpu_usage, 0, cpu_consumed_fee, -1, iterations++);
      int64_t net_weight_consumption, cpu_weight_consumption;
      get_account_fee_consumption("alice"_n, net_weight_consumption, cpu_weight_consumption);

      // alice should consumed cpu fee
      BOOST_REQUIRE_EQUAL(cpu_weight_consumption, cpu_consumed_fee);
      BOOST_REQUIRE_EQUAL(net_weight_consumption, 0);

   } FC_LOG_AND_RETHROW();
BOOST_AUTO_TEST_SUITE_END()
#include <boost/test/unit_test.hpp> /* BOOST_AUTO_TEST_SUITE, etc. */

#include <eosio/testing/tester.hpp>   /* tester */
#include <eosio/chain/exceptions.hpp> /* config_parse_error */
#include <eosio/chain/resource_limits.hpp>
#include <eosio/chain/resource_limits_private.hpp>

#include <contracts.hpp>
#include <test_contracts.hpp>

using namespace eosio;
using namespace eosio::testing;
using namespace eosio::chain::resource_limits;
using mvo = mutable_variant_object;

class txfee_api_tester : public tester
{
public:
   txfee_api_tester() : tester() {}
   txfee_api_tester(setup_policy policy) : tester(policy) {}

   void setup()
   {
      // set parameters intrinsics are priviledged so we need system account here
      set_code(config::system_account_name, test_contracts::txfee_api_test_wasm());
      set_abi(config::system_account_name, test_contracts::txfee_api_test_abi().data());
      produce_block();
   }

   void action(name action_name, mvo mvo)
   {
      push_action(config::system_account_name, action_name, config::system_account_name, fc::mutable_variant_object()("account", "tester"));
      produce_block();
   }
};

BOOST_AUTO_TEST_SUITE(txfee_api_tests)

BOOST_FIXTURE_TEST_CASE(set_fee_parameters_api_test, txfee_api_tester)
{
   push_action(config::system_account_name, "setparams"_n, config::system_account_name, fc::mutable_variant_object()
                  ("cpu_fee_scaler", 1)
                  ("free_block_cpu_threshold", 2)
                  ("net_fee_scaler", 3)
                  ("free_block_net_threshold", 4)
   );
   produce_block();
   auto return_fee_params = control->db().get<resource_limits::fee_params_object>();
   BOOST_CHECK_EQUAL(return_fee_params.cpu_fee_scaler, 1);
   BOOST_CHECK_EQUAL(return_fee_params.free_block_cpu_threshold, 2);
   BOOST_CHECK_EQUAL(return_fee_params.net_fee_scaler, 3);
   BOOST_CHECK_EQUAL(return_fee_params.free_block_net_threshold, 4);
}

BOOST_FIXTURE_TEST_CASE(config_fee_limits_api_test, txfee_api_tester)
{
   push_action(config::system_account_name, "configfees"_n, config::system_account_name, fc::mutable_variant_object()
                  ("account", "tester")
                  ("tx_fee_limit", -1)
                  ("account_fee_limit", -1)
   );
   produce_block();
   auto return_fee_limits = control->db().get<resource_limits::fee_limits_object, resource_limits::by_owner>("tester"_n);
   BOOST_CHECK_EQUAL(return_fee_limits.tx_fee_limit, -1);
   BOOST_CHECK_EQUAL(return_fee_limits.account_fee_limit, -1);
   BOOST_CHECK_EQUAL(return_fee_limits.net_weight_limit, 0);
   BOOST_CHECK_EQUAL(return_fee_limits.cpu_weight_limit, 0);
   BOOST_CHECK_EQUAL(return_fee_limits.net_weight_consumption, 0);
   BOOST_CHECK_EQUAL(return_fee_limits.cpu_weight_consumption, 0);
}

BOOST_FIXTURE_TEST_CASE(set_fee_limits_api_test, txfee_api_tester)
{
   push_action(config::system_account_name, "setfees"_n, config::system_account_name, fc::mutable_variant_object()
                  ("account", "tester")
                  ("net_weight_limit", 1)
                  ("cpu_weight_limit", 2)
   );
   produce_block();
   auto return_fee_limits = control->db().get<resource_limits::fee_limits_object, resource_limits::by_owner>("tester"_n);
   BOOST_CHECK_EQUAL(return_fee_limits.tx_fee_limit, -1);
   BOOST_CHECK_EQUAL(return_fee_limits.account_fee_limit, 0);
   BOOST_CHECK_EQUAL(return_fee_limits.net_weight_limit, 1);
   BOOST_CHECK_EQUAL(return_fee_limits.cpu_weight_limit, 2);
   BOOST_CHECK_EQUAL(return_fee_limits.net_weight_consumption, 0);
   BOOST_CHECK_EQUAL(return_fee_limits.cpu_weight_consumption, 0);
}

BOOST_FIXTURE_TEST_CASE(get_fee_consumption_api_test, txfee_api_tester)
{
   push_action(config::system_account_name, "getfees"_n, config::system_account_name, fc::mutable_variant_object()
              ("account", "tester")
              ("expected_net_pending_weight", 0)
              ("expected_cpu_consumed_weight", 0)
   );
   produce_block();
}
BOOST_AUTO_TEST_SUITE_END()
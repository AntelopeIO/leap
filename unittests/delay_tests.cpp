#include <eosio/chain/generated_transaction_object.hpp>
#include <eosio/chain/global_property_object.hpp>
#include <eosio/testing/tester_network.hpp>
#include <eosio/testing/tester.hpp>

#include <boost/test/unit_test.hpp>

#include <contracts.hpp>
#include <test_contracts.hpp>

using namespace eosio;
using namespace eosio::chain;
using namespace eosio::testing;

static asset get_currency_balance(const validating_tester& chain, account_name account) {
   return chain.get_currency_balance("eosio.token"_n, symbol(SY(4,CUR)), account);
}

const std::string eosio_token = name("eosio.token"_n).to_string();

BOOST_AUTO_TEST_SUITE(delay_tests)

// Delayed trxs are blocked.
BOOST_FIXTURE_TEST_CASE( delayed_trx_blocked, validating_tester ) { try {
   produce_blocks(2);
   signed_transaction trx;

   account_name a = "newco"_n;
   account_name creator = config::system_account_name;

   auto owner_auth =  authority( get_public_key( a, "owner" ) );
   trx.actions.emplace_back( vector<permission_level>{{creator,config::active_name}},
                             newaccount{
                                .creator  = creator,
                                .name     = a,
                                .owner    = owner_auth,
                                .active   = authority( get_public_key( a, "active" ) )
                             });
   set_transaction_headers(trx);
   trx.delay_sec = 3;
   trx.sign( get_private_key( creator, "active" ), control->get_chain_id()  );

   // delayed trx is blocked
   BOOST_CHECK_EXCEPTION(push_transaction( trx ), fc::exception,
      [&](const fc::exception &e) {
         // any incoming trx is blocked
         return expect_assert_message(e, "transaction cannot be delayed");
      });

} FC_LOG_AND_RETHROW() }/// delayed_trx_blocked

// Delayed actions are blocked.
BOOST_AUTO_TEST_CASE( delayed_action_blocked ) { try {
   validating_tester chain;
   const auto& tester_account = "tester"_n;

   chain.create_account("tester"_n);
   chain.produce_blocks();

   BOOST_CHECK_EXCEPTION(
      chain.push_action(config::system_account_name, updateauth::get_name(), tester_account, fc::mutable_variant_object()
           ("account", "tester")
           ("permission", "first")
           ("parent", "active")
           ("auth",  authority(chain.get_public_key(tester_account, "first"))),
           20, 10),
      fc::exception,
      [&](const fc::exception &e) {
         // any delayed incoming trx is blocked
         return expect_assert_message(e, "transaction cannot be delayed");
      });
} FC_LOG_AND_RETHROW() }/// delayed_action_blocked

BOOST_AUTO_TEST_CASE( test_blockchain_params_enabled ) { try {
   //since validation_tester activates all features here we will test how setparams works without
   //blockchain_parameters enabled
   tester chain( setup_policy::preactivate_feature_and_new_bios );

   //change max_transaction_delay to 60 sec
   auto params = chain.control->get_global_properties().configuration;
   params.max_transaction_delay = 60;
   chain.push_action(config::system_account_name, 
                     "setparams"_n,
                     config::system_account_name, 
                     mutable_variant_object()("params", params) );
   
   BOOST_CHECK_EQUAL(chain.control->get_global_properties().configuration.max_transaction_delay, 60u);

   chain.produce_blocks();

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()

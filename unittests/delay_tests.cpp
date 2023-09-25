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

using mvo = fc::mutable_variant_object;

const std::string eosio_token = name("eosio.token"_n).to_string();

static void create_accounts(validating_tester& chain) {
   chain.produce_blocks();
   chain.create_accounts({"eosio.msig"_n, "eosio.token"_n});
   chain.produce_blocks(10);

   chain.push_action(config::system_account_name,
      "setpriv"_n,
      config::system_account_name,
      mvo()
         ("account", "eosio.msig")
         ("is_priv", 1) );

   chain.set_code("eosio.token"_n, test_contracts::eosio_token_wasm());
   chain.set_abi("eosio.token"_n, test_contracts::eosio_token_abi());
   chain.set_code("eosio.msig"_n, test_contracts::eosio_msig_wasm());
   chain.set_abi("eosio.msig"_n, test_contracts::eosio_msig_abi());

   chain.produce_blocks();
   chain.create_account("tester"_n);
   chain.create_account("tester2"_n);
   chain.produce_blocks(10);
}

static void propose_approve_msig_token_transfer_trx(validating_tester& chain, name proposal_name, const vector<permission_level>& perm, uint32_t delay_sec, const std::string& quantity) {
   vector<permission_level> transfer_perm = {{ "tester"_n, config::active_name }};
   fc::variant pretty_trx = fc::mutable_variant_object()
      ("expiration", "2020-01-01T00:30")
      ("ref_block_num", 2)
      ("ref_block_prefix", 3)
      ("max_net_usage_words", 0)
      ("max_cpu_usage_ms", 0)
      ("delay_sec", delay_sec)
      ("actions", fc::variants({
         mvo()
            ("account", name("eosio.token"_n))
            ("name", "transfer")
            ("authorization", transfer_perm)
            ("data", fc::mutable_variant_object()
               ("from", name("tester"_n))
               ("to", name("tester2"_n))
               ("quantity", quantity)
               ("memo", "hi" )
            )
      })
   );

   transaction trx;
   abi_serializer::from_variant(pretty_trx, trx, chain.get_resolver(), abi_serializer::create_yield_function(chain.abi_serializer_max_time));

   vector<permission_level> requested_perm = {{ "tester"_n, config::active_name }};
   chain.push_action("eosio.msig"_n, "propose"_n, perm,
      mvo()
         ("proposer",      "tester")
         ("proposal_name", proposal_name)
         ("trx",           trx)
         ("requested",     requested_perm)
   );

   chain.push_action("eosio.msig"_n, "approve"_n, "tester"_n,
      mvo()
         ("proposer",      "tester")
         ("proposal_name", proposal_name)
         ("level",         permission_level{ "tester"_n, config::active_name })
   );
}

static void propose_approve_msig_updateauth_trx(validating_tester& chain, name proposal_name, const vector<permission_level>& perm, uint32_t delay_sec) {
   vector<permission_level> transfer_perm = {{ "tester"_n, config::active_name }};
   fc::variant pretty_trx = fc::mutable_variant_object()
      ("expiration", "2020-01-01T00:30")
      ("ref_block_num", 2)
      ("ref_block_prefix", 3)
      ("max_net_usage_words", 0)
      ("max_cpu_usage_ms", 0)
      ("delay_sec", delay_sec)
      ("actions", fc::variants({
         mvo()
            ("account", config::system_account_name)
            ("name", updateauth::get_name())
            ("authorization", transfer_perm)
            ("data", fc::mutable_variant_object()
               ("account", "tester")
               ("permission", "first")
               ("parent", "active")
               ("auth",  authority(chain.get_public_key("tester"_n, "first")))
            )
      })
   );

   transaction trx;
   abi_serializer::from_variant(pretty_trx, trx, chain.get_resolver(), abi_serializer::create_yield_function(chain.abi_serializer_max_time));

   vector<permission_level> requested_perm = {{ "tester"_n, config::active_name }};
   chain.push_action("eosio.msig"_n, "propose"_n, perm,
      mvo()
         ("proposer",      "tester")
         ("proposal_name", proposal_name)
         ("trx",           trx)
         ("requested",     requested_perm)
   );

   chain.push_action("eosio.msig"_n, "approve"_n, "tester"_n,
      mvo()
         ("proposer",      "tester")
         ("proposal_name", proposal_name)
         ("level",         permission_level{ "tester"_n, config::active_name })
   );
}

static void propose_approve_msig_linkauth_trx(validating_tester& chain, name proposal_name, const vector<permission_level>& perm, uint32_t delay_sec) {
   vector<permission_level> transfer_perm = {{ "tester"_n, config::active_name }};
   fc::variant pretty_trx = fc::mutable_variant_object()
      ("expiration", "2020-01-01T00:30")
      ("ref_block_num", 2)
      ("ref_block_prefix", 3)
      ("max_net_usage_words", 0)
      ("max_cpu_usage_ms", 0)
      ("delay_sec", delay_sec)
      ("actions", fc::variants({
         mvo()
            ("account", config::system_account_name)
            ("name", linkauth::get_name())
            ("authorization", transfer_perm)
            ("data", fc::mutable_variant_object()
               ("account", "tester")
               ("code", eosio_token)
               ("type", "transfer")
               ("requirement", "second")
            )
      })
   );

   transaction trx;
   abi_serializer::from_variant(pretty_trx, trx, chain.get_resolver(), abi_serializer::create_yield_function(chain.abi_serializer_max_time));

   vector<permission_level> requested_perm = {{ "tester"_n, config::active_name }};
   chain.push_action("eosio.msig"_n, "propose"_n, perm,
      mvo()
         ("proposer",      "tester")
         ("proposal_name", proposal_name)
         ("trx",           trx)
         ("requested",     requested_perm)
   );

   chain.push_action("eosio.msig"_n, "approve"_n, "tester"_n,
      mvo()
         ("proposer",      "tester")
         ("proposal_name", proposal_name)
         ("level",         permission_level{ "tester"_n, config::active_name })
   );
}

static void exec_msig_trx(validating_tester& chain, name proposal_name) {
   chain.push_action("eosio.msig"_n, "exec"_n, "tester"_n,
      mvo()
         ("proposer",      "tester")
         ("proposal_name", proposal_name)
         ("executer",      "tester")
   );
}

static asset get_currency_balance(const validating_tester& chain, account_name account) {
   return chain.get_currency_balance("eosio.token"_n, symbol(SY(4,CUR)), account);
}


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

   // no deferred trx was generated
   auto gen_size = control->db().get_index<generated_transaction_multi_index,by_trx_id>().size();
   BOOST_REQUIRE_EQUAL(0u, gen_size);
} FC_LOG_AND_RETHROW() }/// delayed_trx_blocked

// Delayed actions are blocked.
BOOST_AUTO_TEST_CASE( delayed_action_blocked ) { try {
   validating_tester chain;
   const auto& tester_account = "tester"_n;

   chain.create_account("tester"_n);
   chain.produce_blocks();

   // delayed action is blocked
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

   // no deferred trx was generated
   auto gen_size = chain.control->db().get_index<generated_transaction_multi_index,by_trx_id>().size();
   BOOST_REQUIRE_EQUAL(0u, gen_size);
} FC_LOG_AND_RETHROW() }/// delayed_action_blocked

// test link to permission with delay directly on it
BOOST_AUTO_TEST_CASE( link_delay_direct_test ) { try {
   validating_tester chain;
   const auto& tester_account = "tester"_n;

   create_accounts(chain);

   chain.push_action(config::system_account_name, updateauth::get_name(), tester_account, fc::mutable_variant_object()
           ("account", "tester")
           ("permission", "first")
           ("parent", "active")
           ("auth",  authority(chain.get_public_key(tester_account, "first")))
   );
   chain.push_action(config::system_account_name, linkauth::get_name(), tester_account, fc::mutable_variant_object()
           ("account", "tester")
           ("code", eosio_token)
           ("type", "transfer")
           ("requirement", "first"));
   chain.produce_blocks();
   chain.push_action("eosio.token"_n, "create"_n, "eosio.token"_n, mutable_variant_object()
           ("issuer", eosio_token)
           ("maximum_supply", "9000000.0000 CUR")
   );

   chain.push_action("eosio.token"_n, name("issue"), "eosio.token"_n, fc::mutable_variant_object()
           ("to",       eosio_token)
           ("quantity", "1000000.0000 CUR")
           ("memo", "for stuff")
   );

   auto trace = chain.push_action("eosio.token"_n, name("transfer"), "eosio.token"_n, fc::mutable_variant_object()
       ("from", eosio_token)
       ("to", "tester")
       ("quantity", "100.0000 CUR")
       ("memo", "hi" )
   );
   BOOST_REQUIRE_EQUAL(transaction_receipt::executed, trace->receipt->status);

   chain.produce_blocks();

   auto liquid_balance = get_currency_balance(chain, "eosio.token"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("999900.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("100.0000 CUR"), liquid_balance);

   trace = chain.push_action("eosio.token"_n, name("transfer"), "tester"_n, fc::mutable_variant_object()
       ("from", "tester")
       ("to", "tester2")
       ("quantity", "1.0000 CUR")
       ("memo", "hi" )
   );

   BOOST_REQUIRE_EQUAL(transaction_receipt::executed, trace->receipt->status);

   chain.produce_blocks();

   liquid_balance = get_currency_balance(chain, "eosio.token"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("999900.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("99.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester2"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("1.0000 CUR"), liquid_balance);

   trace = chain.push_action(config::system_account_name, updateauth::get_name(), tester_account, fc::mutable_variant_object()
           ("account", "tester")
           ("permission", "first")
           ("parent", "active")
           ("auth",  authority(chain.get_public_key(tester_account, "first"), 10))
   );
   BOOST_REQUIRE_EQUAL(transaction_receipt::executed, trace->receipt->status);

   chain.produce_blocks();

   // propose and approve an msig trx that transfers "quantity" tokens
   // from tester to tester2 with a delay of "delay_seconds"
   constexpr name proposal_name     = "prop1"_n;
   constexpr uint32_t delay_seconds = 10;
   constexpr auto quantity          = "3.0000 CUR";
   propose_approve_msig_token_transfer_trx(chain, proposal_name, {{ "tester"_n, config::active_name }}, delay_seconds, quantity);

   chain.produce_blocks();

   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("99.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester2"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("1.0000 CUR"), liquid_balance);

   chain.produce_blocks(18);

   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("99.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester2"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("1.0000 CUR"), liquid_balance);

   chain.produce_blocks();

   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("99.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester2"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("1.0000 CUR"), liquid_balance);

   // executue after delay of 10 seconds
   exec_msig_trx(chain, proposal_name);

   chain.produce_blocks();

   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("96.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester2"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("4.0000 CUR"), liquid_balance);

} FC_LOG_AND_RETHROW() }/// link_delay_direct_test

// test link to permission with delay on permission which is parent of min permission (special logic in permission_object::satisfies)
BOOST_AUTO_TEST_CASE( link_delay_direct_parent_permission_test ) { try {
   validating_tester chain;

   const auto& tester_account = "tester"_n;

   create_accounts(chain);

   chain.push_action(config::system_account_name, updateauth::get_name(), tester_account, fc::mutable_variant_object()
           ("account", "tester")
           ("permission", "first")
           ("parent", "active")
           ("auth",  authority(chain.get_public_key(tester_account, "first")))
   );
   chain.push_action(config::system_account_name, linkauth::get_name(), tester_account, fc::mutable_variant_object()
           ("account", "tester")
           ("code", eosio_token)
           ("type", "transfer")
           ("requirement", "first"));

   chain.produce_blocks();
   chain.push_action("eosio.token"_n, "create"_n, "eosio.token"_n, mutable_variant_object()
           ("issuer", eosio_token)
           ("maximum_supply", "9000000.0000 CUR")
   );

   chain.push_action("eosio.token"_n, name("issue"), "eosio.token"_n, fc::mutable_variant_object()
           ("to",       eosio_token)
           ("quantity", "1000000.0000 CUR")
           ("memo", "for stuff")
   );

   auto trace = chain.push_action("eosio.token"_n, name("transfer"), "eosio.token"_n, fc::mutable_variant_object()
       ("from", eosio_token)
       ("to", "tester")
       ("quantity", "100.0000 CUR")
       ("memo", "hi" )
   );
   BOOST_REQUIRE_EQUAL(transaction_receipt::executed, trace->receipt->status);

   chain.produce_blocks();

   auto liquid_balance = get_currency_balance(chain, "eosio.token"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("999900.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("100.0000 CUR"), liquid_balance);

   trace = chain.push_action("eosio.token"_n, name("transfer"), "tester"_n, fc::mutable_variant_object()
       ("from", "tester")
       ("to", "tester2")
       ("quantity", "1.0000 CUR")
       ("memo", "hi" )
   );

   BOOST_REQUIRE_EQUAL(transaction_receipt::executed, trace->receipt->status);

   chain.produce_blocks();

   // Propose and approve an msig trx that transfers "quantity" tokens
   // from tester to tester2 with a delay of "delay_seconds"
   constexpr name proposal_name     = "prop1"_n;
   constexpr uint32_t delay_seconds = 15;
   constexpr auto quantity          = "3.0000 CUR";
   propose_approve_msig_token_transfer_trx(chain, proposal_name, {{ "tester"_n, config::owner_name }}, delay_seconds, quantity);

   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("99.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester2"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("1.0000 CUR"), liquid_balance);

   chain.produce_blocks();

   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("99.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester2"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("1.0000 CUR"), liquid_balance);

   chain.produce_blocks(28);

   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("99.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester2"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("1.0000 CUR"), liquid_balance);

   chain.produce_blocks();

   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("99.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester2"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("1.0000 CUR"), liquid_balance);

   chain.produce_blocks();

   // executue the msig trx
   exec_msig_trx(chain, proposal_name);

   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("96.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester2"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("4.0000 CUR"), liquid_balance);

} FC_LOG_AND_RETHROW() }/// link_delay_direct_parent_permission_test

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

// test link to permission with delay on permission between min permission and authorizing permission it
BOOST_AUTO_TEST_CASE( link_delay_direct_walk_parent_permissions_test ) { try {
   validating_tester chain;
   const auto& tester_account = "tester"_n;

   create_accounts(chain);

   chain.push_action(config::system_account_name, updateauth::get_name(), tester_account, fc::mutable_variant_object()
           ("account", "tester")
           ("permission", "first")
           ("parent", "active")
           ("auth",  authority(chain.get_public_key(tester_account, "first")))
   );
   chain.push_action(config::system_account_name, updateauth::get_name(), tester_account, fc::mutable_variant_object()
           ("account", "tester")
           ("permission", "second")
           ("parent", "first")
           ("auth",  authority(chain.get_public_key(tester_account, "second")))
   );
   chain.push_action(config::system_account_name, linkauth::get_name(), tester_account, fc::mutable_variant_object()
           ("account", "tester")
           ("code", eosio_token)
           ("type", "transfer")
           ("requirement", "second"));

   chain.produce_blocks();
   chain.push_action("eosio.token"_n, "create"_n, "eosio.token"_n, mutable_variant_object()
           ("issuer", eosio_token)
           ("maximum_supply", "9000000.0000 CUR")
   );

   chain.push_action("eosio.token"_n, name("issue"), "eosio.token"_n, fc::mutable_variant_object()
           ("to",       eosio_token)
           ("quantity", "1000000.0000 CUR")
           ("memo", "for stuff")
   );

   auto trace = chain.push_action("eosio.token"_n, name("transfer"), "eosio.token"_n, fc::mutable_variant_object()
       ("from", eosio_token)
       ("to", "tester")
       ("quantity", "100.0000 CUR")
       ("memo", "hi" )
   );
   BOOST_REQUIRE_EQUAL(transaction_receipt::executed, trace->receipt->status);

   chain.produce_blocks();

   auto liquid_balance = get_currency_balance(chain, "eosio.token"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("999900.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("100.0000 CUR"), liquid_balance);

   trace = chain.push_action("eosio.token"_n, name("transfer"), "tester"_n, fc::mutable_variant_object()
       ("from", "tester")
       ("to", "tester2")
       ("quantity", "1.0000 CUR")
       ("memo", "hi" )
   );

   BOOST_REQUIRE_EQUAL(transaction_receipt::executed, trace->receipt->status);

   chain.produce_blocks();

   liquid_balance = get_currency_balance(chain, "eosio.token"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("999900.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("99.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester2"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("1.0000 CUR"), liquid_balance);

   trace = chain.push_action(config::system_account_name, updateauth::get_name(), tester_account, fc::mutable_variant_object()
           ("account", "tester")
           ("permission", "first")
           ("parent", "active")
           ("auth",  authority(chain.get_public_key(tester_account, "first"), 20))
   );
   BOOST_REQUIRE_EQUAL(transaction_receipt::executed, trace->receipt->status);

   chain.produce_blocks();


   // propose and approve an msig trx that transfers "quantity" tokens
   // from tester to tester2 with a delay of "delay_seconds"
   constexpr name proposal_name     = "prop1"_n;
   constexpr uint32_t delay_seconds = 20;
   constexpr auto quantity          = "3.0000 CUR";
   propose_approve_msig_token_transfer_trx(chain, proposal_name, {{ "tester"_n, config::active_name }}, delay_seconds, quantity);

   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("99.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester2"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("1.0000 CUR"), liquid_balance);

   chain.produce_blocks();

   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("99.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester2"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("1.0000 CUR"), liquid_balance);

   chain.produce_blocks(38);

   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("99.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester2"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("1.0000 CUR"), liquid_balance);

   chain.produce_blocks();

   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("99.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester2"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("1.0000 CUR"), liquid_balance);

   // executue after delay
   exec_msig_trx(chain, proposal_name);

   chain.produce_blocks();

   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("96.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester2"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("4.0000 CUR"), liquid_balance);

} FC_LOG_AND_RETHROW() }/// link_delay_direct_walk_parent_permissions_test

// test removing delay on permission
BOOST_AUTO_TEST_CASE( link_delay_permission_change_test ) { try {
   validating_tester chain;

   const auto& tester_account = "tester"_n;

   create_accounts(chain);

   chain.push_action(config::system_account_name, updateauth::get_name(), tester_account, fc::mutable_variant_object()
           ("account", "tester")
           ("permission", "first")
           ("parent", "active")
           ("auth",  authority(chain.get_public_key(tester_account, "first"), 10))
   );
   chain.push_action(config::system_account_name, linkauth::get_name(), tester_account, fc::mutable_variant_object()
           ("account", "tester")
           ("code", eosio_token)
           ("type", "transfer")
           ("requirement", "first"));

   chain.produce_blocks();
   chain.push_action("eosio.token"_n, "create"_n, "eosio.token"_n, mutable_variant_object()
           ("issuer", eosio_token )
           ("maximum_supply", "9000000.0000 CUR" )
   );

   chain.push_action("eosio.token"_n, name("issue"), "eosio.token"_n, fc::mutable_variant_object()
           ("to",       eosio_token)
           ("quantity", "1000000.0000 CUR")
           ("memo", "for stuff")
   );

   auto trace = chain.push_action("eosio.token"_n, name("transfer"), "eosio.token"_n, fc::mutable_variant_object()
       ("from", eosio_token)
       ("to", "tester")
       ("quantity", "100.0000 CUR")
       ("memo", "hi" )
   );
   BOOST_REQUIRE_EQUAL(transaction_receipt::executed, trace->receipt->status);

   chain.produce_blocks();

   auto liquid_balance = get_currency_balance(chain, "eosio.token"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("999900.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("100.0000 CUR"), liquid_balance);

   // this transaction will be delayed 20 blocks
   constexpr name proposal_1_name     = "prop1"_n;
   constexpr uint32_t delay_seconds = 10;
   constexpr auto quantity          = "1.0000 CUR";
   propose_approve_msig_token_transfer_trx(chain, proposal_1_name, {{ "tester"_n, config::active_name }}, delay_seconds, quantity);

   chain.produce_blocks();

   liquid_balance = get_currency_balance(chain, "eosio.token"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("999900.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("100.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester2"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("0.0000 CUR"), liquid_balance);

   // this transaction will be delayed 20 blocks
   constexpr name proposal_2_name     = "prop2"_n;
   constexpr uint32_t delay_seconds_2 = 10;
   propose_approve_msig_updateauth_trx(chain, proposal_2_name, {{ "tester"_n, config::active_name }}, delay_seconds_2);

   chain.produce_blocks();

   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("100.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester2"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("0.0000 CUR"), liquid_balance);

   chain.produce_blocks(16);

   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("100.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester2"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("0.0000 CUR"), liquid_balance);

   // this transaction will be delayed 20 blocks
   constexpr name proposal_3_name     = "prop3"_n;
   constexpr uint32_t delay_seconds_3 = 10;
   constexpr auto quantity_3          = "5.0000 CUR";
   propose_approve_msig_token_transfer_trx(chain, proposal_3_name, {{ "tester"_n, config::active_name }}, delay_seconds_3, quantity_3);

   chain.produce_blocks();

   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("100.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester2"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("0.0000 CUR"), liquid_balance);

   chain.produce_blocks();

   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("100.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester2"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("0.0000 CUR"), liquid_balance);

   // first transfer will finally be performed
   exec_msig_trx(chain, proposal_1_name);
   chain.produce_blocks();

   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("99.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester2"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("1.0000 CUR"), liquid_balance);

   // delayed update auth removing the delay will finally execute
   exec_msig_trx(chain, proposal_2_name);
   chain.produce_blocks();

   // this transfer is performed right away since delay is removed
   trace = chain.push_action("eosio.token"_n, name("transfer"), "tester"_n, fc::mutable_variant_object()
       ("from", "tester")
       ("to", "tester2")
       ("quantity", "10.0000 CUR")
       ("memo", "hi" )
   );
   BOOST_REQUIRE_EQUAL(transaction_receipt::executed, trace->receipt->status);

   chain.produce_blocks();

   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("89.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester2"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("11.0000 CUR"), liquid_balance);

   chain.produce_blocks(15);

   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("89.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester2"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("11.0000 CUR"), liquid_balance);

   // second transfer finally is performed
   exec_msig_trx(chain, proposal_3_name);
   chain.produce_blocks();

   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("84.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester2"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("16.0000 CUR"), liquid_balance);

} FC_LOG_AND_RETHROW() }/// link_delay_permission_change_test

// test removing delay on permission based on heirarchy delay
BOOST_AUTO_TEST_CASE( link_delay_permission_change_with_delay_heirarchy_test ) { try {
   validating_tester chain;

   const auto& tester_account = "tester"_n;

   create_accounts(chain);

   chain.push_action(config::system_account_name, updateauth::get_name(), tester_account, fc::mutable_variant_object()
           ("account", "tester")
           ("permission", "first")
           ("parent", "active")
           ("auth",  authority(chain.get_public_key(tester_account, "first"), 10))
   );
   chain.push_action(config::system_account_name, updateauth::get_name(), tester_account, fc::mutable_variant_object()
           ("account", "tester")
           ("permission", "second")
           ("parent", "first")
           ("auth",  authority(chain.get_public_key(tester_account, "second")))
   );
   chain.push_action(config::system_account_name, linkauth::get_name(), tester_account, fc::mutable_variant_object()
           ("account", "tester")
           ("code", eosio_token)
           ("type", "transfer")
           ("requirement", "second"));

   chain.produce_blocks();
   chain.push_action("eosio.token"_n, "create"_n, "eosio.token"_n, mutable_variant_object()
           ("issuer", eosio_token)
           ("maximum_supply", "9000000.0000 CUR" )
   );

   chain.push_action("eosio.token"_n, name("issue"), "eosio.token"_n, fc::mutable_variant_object()
           ("to",       eosio_token)
           ("quantity", "1000000.0000 CUR")
           ("memo", "for stuff")
   );

   auto trace = chain.push_action("eosio.token"_n, name("transfer"), "eosio.token"_n, fc::mutable_variant_object()
       ("from", eosio_token)
       ("to", "tester")
       ("quantity", "100.0000 CUR")
       ("memo", "hi" )
   );
   BOOST_REQUIRE_EQUAL(transaction_receipt::executed, trace->receipt->status);

   chain.produce_blocks();

   auto liquid_balance = get_currency_balance(chain, "eosio.token"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("999900.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("100.0000 CUR"), liquid_balance);

   // this transaction will be delayed 20 blocks
   constexpr name proposal_1_name     = "prop1"_n;
   constexpr uint32_t delay_seconds = 10;
   constexpr auto quantity          = "1.0000 CUR";
   propose_approve_msig_token_transfer_trx(chain, proposal_1_name, {{ "tester"_n, config::active_name }}, delay_seconds, quantity);

   chain.produce_blocks();

   liquid_balance = get_currency_balance(chain, "eosio.token"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("999900.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("100.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester2"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("0.0000 CUR"), liquid_balance);

   // this transaction will be delayed 20 blocks
   constexpr name proposal_2_name     = "prop2"_n;
   constexpr uint32_t delay_seconds_2 = 10;
   propose_approve_msig_updateauth_trx(chain, proposal_2_name, {{ "tester"_n, config::active_name }}, delay_seconds_2);

   chain.produce_blocks();

   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("100.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester2"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("0.0000 CUR"), liquid_balance);

   chain.produce_blocks(16);

   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("100.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester2"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("0.0000 CUR"), liquid_balance);

   // this transaction will be delayed 20 blocks
   constexpr name proposal_3_name     = "prop3"_n;
   constexpr uint32_t delay_seconds_3 = 10;
   constexpr auto quantity_3          = "5.0000 CUR";
   propose_approve_msig_token_transfer_trx(chain, proposal_3_name, {{ "tester"_n, config::active_name }}, delay_seconds_3, quantity_3);

   chain.produce_blocks();

   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("100.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester2"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("0.0000 CUR"), liquid_balance);

   chain.produce_blocks();

   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("100.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester2"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("0.0000 CUR"), liquid_balance);

   // first transfer will finally be performed
   exec_msig_trx(chain, proposal_1_name);
   chain.produce_blocks();

   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("99.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester2"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("1.0000 CUR"), liquid_balance);

   // delayed update auth removing the delay will finally execute
   exec_msig_trx(chain, proposal_2_name);
   chain.produce_blocks();

   // this transfer is performed right away since delay is removed
   trace = chain.push_action("eosio.token"_n, name("transfer"), "tester"_n, fc::mutable_variant_object()
       ("from", "tester")
       ("to", "tester2")
       ("quantity", "10.0000 CUR")
       ("memo", "hi" )
   );

   BOOST_REQUIRE_EQUAL(transaction_receipt::executed, trace->receipt->status);

   chain.produce_blocks();

   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("89.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester2"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("11.0000 CUR"), liquid_balance);

   chain.produce_blocks(14);

   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("89.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester2"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("11.0000 CUR"), liquid_balance);

   chain.produce_blocks();

   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("89.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester2"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("11.0000 CUR"), liquid_balance);

   // second transfer finally is performed
   exec_msig_trx(chain, proposal_3_name);
   chain.produce_blocks();

   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("84.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester2"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("16.0000 CUR"), liquid_balance);

} FC_LOG_AND_RETHROW() }/// link_delay_permission_change_with_delay_heirarchy_test

// test moving link with delay on permission
BOOST_AUTO_TEST_CASE( link_delay_link_change_test ) { try {
   validating_tester chain;

   const auto& tester_account = "tester"_n;

   create_accounts(chain);

   chain.push_action(config::system_account_name, updateauth::get_name(), tester_account, fc::mutable_variant_object()
           ("account", "tester")
           ("permission", "first")
           ("parent", "active")
           ("auth",  authority(chain.get_public_key(tester_account, "first"), 10))
   );
   chain.push_action(config::system_account_name, linkauth::get_name(), tester_account, fc::mutable_variant_object()
           ("account", "tester")
           ("code", eosio_token)
           ("type", "transfer")
           ("requirement", "first"));
   chain.push_action(config::system_account_name, updateauth::get_name(), tester_account, fc::mutable_variant_object()
           ("account", "tester")
           ("permission", "second")
           ("parent", "active")
           ("auth",  authority(chain.get_public_key(tester_account, "second")))
   );

   chain.produce_blocks();
   chain.push_action("eosio.token"_n, "create"_n, "eosio.token"_n, mutable_variant_object()
           ("issuer", eosio_token)
           ("maximum_supply", "9000000.0000 CUR" )
   );

   chain.push_action("eosio.token"_n, name("issue"), "eosio.token"_n, fc::mutable_variant_object()
           ("to",       eosio_token)
           ("quantity", "1000000.0000 CUR")
           ("memo", "for stuff")
   );

   auto trace = chain.push_action("eosio.token"_n, name("transfer"), "eosio.token"_n, fc::mutable_variant_object()
       ("from", eosio_token)
       ("to", "tester")
       ("quantity", "100.0000 CUR")
       ("memo", "hi" )
   );
   BOOST_REQUIRE_EQUAL(transaction_receipt::executed, trace->receipt->status);

   chain.produce_blocks();

   auto liquid_balance = get_currency_balance(chain, "eosio.token"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("999900.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("100.0000 CUR"), liquid_balance);

   // this transaction will be delayed 20 blocks
   constexpr name proposal_1_name     = "prop1"_n;
   constexpr uint32_t delay_seconds = 10;
   constexpr auto quantity          = "1.0000 CUR";
   propose_approve_msig_token_transfer_trx(chain, proposal_1_name, {{ "tester"_n, config::active_name }}, delay_seconds, quantity);

   chain.produce_blocks();

   liquid_balance = get_currency_balance(chain, "eosio.token"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("999900.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("100.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester2"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("0.0000 CUR"), liquid_balance);

   BOOST_REQUIRE_EXCEPTION(
      chain.push_action( config::system_account_name, linkauth::get_name(),
                         vector<permission_level>{permission_level{tester_account, "first"_n}},
                         fc::mutable_variant_object()
      ("account", "tester")
      ("code", eosio_token)
      ("type", "transfer")
      ("requirement", "second"),
      30, 0),
      unsatisfied_authorization,
      fc_exception_message_starts_with("transaction declares authority")
   );

   // this transaction will be delayed 20 blocks
   constexpr name proposal_2_name     = "prop2"_n;
   constexpr uint32_t delay_seconds_2 = 10;
   propose_approve_msig_linkauth_trx(chain, proposal_2_name, {{ "tester"_n, config::active_name }}, delay_seconds_2);

   chain.produce_blocks();

   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("100.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester2"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("0.0000 CUR"), liquid_balance);

   chain.produce_blocks(16);

   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("100.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester2"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("0.0000 CUR"), liquid_balance);

   // this transaction will be delayed 20 blocks
   constexpr name proposal_3_name     = "prop3"_n;
   constexpr uint32_t delay_seconds_3 = 10;
   constexpr auto quantity_3          = "5.0000 CUR";
   propose_approve_msig_token_transfer_trx(chain, proposal_3_name, {{ "tester"_n, config::active_name }}, delay_seconds_3, quantity_3);

   chain.produce_blocks();

   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("100.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester2"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("0.0000 CUR"), liquid_balance);

   chain.produce_blocks();

   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("100.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester2"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("0.0000 CUR"), liquid_balance);

   // first transfer will finally be performed
   exec_msig_trx(chain, proposal_1_name);
   chain.produce_blocks();

   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("99.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester2"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("1.0000 CUR"), liquid_balance);

   // delay on minimum permission of transfer is finally removed
   exec_msig_trx(chain, proposal_2_name);
   chain.produce_blocks();

   // this transfer is performed right away since delay is removed
   trace = chain.push_action("eosio.token"_n, name("transfer"), "tester"_n, fc::mutable_variant_object()
       ("from", "tester")
       ("to", "tester2")
       ("quantity", "10.0000 CUR")
       ("memo", "hi" )
   );
   BOOST_REQUIRE_EQUAL(transaction_receipt::executed, trace->receipt->status);

   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("89.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester2"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("11.0000 CUR"), liquid_balance);

   chain.produce_blocks(16);

   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("89.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester2"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("11.0000 CUR"), liquid_balance);

   // second transfer finally is performed
   exec_msig_trx(chain, proposal_3_name);
   chain.produce_blocks();

   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("84.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester2"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("16.0000 CUR"), liquid_balance);

} FC_LOG_AND_RETHROW() }/// link_delay_link_change_test

BOOST_AUTO_TEST_SUITE_END()

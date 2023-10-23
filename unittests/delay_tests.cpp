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

static void propose_approve_msig_trx(validating_tester& chain, const name& proposal_name, const permission_level& perm, const fc::variant& pretty_trx) {
   vector<permission_level> requested_perm = { perm };
   transaction trx;
   abi_serializer::from_variant(pretty_trx, trx, chain.get_resolver(), abi_serializer::create_yield_function(chain.abi_serializer_max_time));

   chain.push_action("eosio.msig"_n, "propose"_n, vector<permission_level>{perm},
      mvo()
         ("proposer",      "tester")
         ("proposal_name", proposal_name)
         ("trx",           trx)
         ("requested",     requested_perm)
   );
   chain.push_action("eosio.msig"_n, "approve"_n, vector<permission_level>{perm},
      mvo()
         ("proposer",      "tester")
         ("proposal_name", proposal_name)
         ("level",         perm)
   );
}

static void propose_approve_msig_token_transfer_trx(validating_tester& chain, const name& proposal_name, const permission_level& perm, uint32_t delay_sec, const std::string& quantity) {
   fc::variant pretty_trx = mvo()
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
            ("authorization", vector<permission_level>{perm})
            ("data", fc::mutable_variant_object()
               ("from", name("tester"_n))
               ("to", name("tester2"_n))
               ("quantity", quantity)
               ("memo", "hi" )
            )
      })
   );

   propose_approve_msig_trx(chain, proposal_name, perm, pretty_trx);
}

static void propose_approve_msig_updateauth_trx(validating_tester& chain, const name& proposal_name, const permission_level& perm, uint32_t delay_sec) {
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
            ("authorization", vector<permission_level> {{ "tester"_n, config::active_name }})
            ("data", fc::mutable_variant_object()
               ("account", "tester")
               ("permission", "first")
               ("parent", "active")
               ("auth",  authority(chain.get_public_key("tester"_n, "first")))
            )
      })
   );

   propose_approve_msig_trx(chain, proposal_name, perm, pretty_trx);
}

static void propose_approve_msig_linkauth_trx(validating_tester& chain, const name& proposal_name, const name& requirement, const permission_level& perm, uint32_t delay_sec) {
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
            ("authorization", vector<permission_level>{{ "tester"_n, config::active_name }})
            ("data", fc::mutable_variant_object()
               ("account", "tester")
               ("code", eosio_token)
               ("type", "transfer")
               ("requirement", requirement)
            )
      })
   );

   propose_approve_msig_trx(chain, proposal_name, perm, pretty_trx);
}

static void propose_approve_msig_unlinkauth_trx(validating_tester& chain, const name& proposal_name, const permission_level& perm, uint32_t delay_sec) {
   fc::variant pretty_trx = mvo()
      ("expiration", "2020-01-01T00:30")
      ("ref_block_num", 2)
      ("ref_block_prefix", 3)
      ("max_net_usage_words", 0)
      ("max_cpu_usage_ms", 0)
      ("delay_sec", delay_sec)
      ("actions", fc::variants({
         mvo()
            ("account", config::system_account_name)
            ("name", unlinkauth::get_name())
            ("authorization", vector<permission_level>{{ "tester"_n, config::active_name}})
            ("data", fc::mutable_variant_object()
               ("account", "tester")
               ("code", eosio_token)
               ("type", "transfer")
            )
      })
   );

   propose_approve_msig_trx(chain, proposal_name, perm, pretty_trx);
}

static void exec_msig_trx(validating_tester& chain, name proposal_name, const vector<permission_level>& perm) {
   chain.push_action("eosio.msig"_n, "exec"_n, perm,
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

BOOST_FIXTURE_TEST_CASE( delay_error_create_account, validating_tester_no_disable_deferred_trx) { try {

   produce_blocks(2);
   signed_transaction trx;

   account_name a = "newco"_n;
   account_name creator = config::system_account_name;

   auto owner_auth =  authority( get_public_key( a, "owner" ) );
   trx.actions.emplace_back( vector<permission_level>{{creator,config::active_name}},
                             newaccount{
                                .creator  = "bad"_n, /// a does not exist, this should error when execute
                                .name     = a,
                                .owner    = owner_auth,
                                .active   = authority( get_public_key( a, "active" ) )
                             });
   set_transaction_headers(trx);
   trx.delay_sec = 3;
   trx.sign( get_private_key( creator, "active" ), control->get_chain_id()  );

   ilog( fc::json::to_pretty_string(trx) );
   auto trace = push_transaction( trx );
   edump((*trace));

   produce_blocks(6);

   auto scheduled_trxs = get_scheduled_transactions();
   BOOST_REQUIRE_EQUAL(scheduled_trxs.size(), 1u);

   auto billed_cpu_time_us = control->get_global_properties().configuration.min_transaction_cpu_usage;
   auto dtrace = control->push_scheduled_transaction(scheduled_trxs.front(), fc::time_point::maximum(), fc::microseconds::maximum(), billed_cpu_time_us, true);
   BOOST_REQUIRE_EQUAL(dtrace->except.has_value(), true);
   BOOST_REQUIRE_EQUAL(dtrace->except->code(), missing_auth_exception::code_value);

} FC_LOG_AND_RETHROW() }

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
      ("requirement", "first")
   );
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
   constexpr name proposal_name = "prop1"_n;
   propose_approve_msig_token_transfer_trx(chain, proposal_name, { "tester"_n, config::active_name }, 10, "3.0000 CUR");

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
   exec_msig_trx(chain, proposal_name, {{ "tester"_n, config::active_name }});

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

   liquid_balance = get_currency_balance(chain, "eosio.token"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("999900.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("99.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester2"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("1.0000 CUR"), liquid_balance);

   trace = chain.push_action(config::system_account_name, updateauth::get_name(), tester_account, fc::mutable_variant_object()
           ("account", "tester")
           ("permission", "active")
           ("parent", "owner")
           ("auth",  authority(chain.get_public_key(tester_account, "active"), 15))
   );
   BOOST_REQUIRE_EQUAL(transaction_receipt::executed, trace->receipt->status);

   chain.produce_blocks();
   chain.produce_blocks();

   // Propose and approve an msig trx that transfers "quantity" tokens
   // from tester to tester2 with a delay of "delay_seconds"
   constexpr name proposal_name = "prop1"_n;
   propose_approve_msig_token_transfer_trx(chain, proposal_name, { "tester"_n, config::owner_name }, 15, "3.0000 CUR");

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
   exec_msig_trx(chain, proposal_name, {{ "tester"_n, config::owner_name }});

   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("96.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester2"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("4.0000 CUR"), liquid_balance);

} FC_LOG_AND_RETHROW() }/// link_delay_direct_parent_permission_test

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
   constexpr name proposal_name = "prop1"_n;
   propose_approve_msig_token_transfer_trx(chain, proposal_name, { "tester"_n, config::active_name }, 20, "3.0000 CUR");

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
   exec_msig_trx(chain, proposal_name, {{ "tester"_n, config::active_name }});

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
   propose_approve_msig_token_transfer_trx(chain, proposal_1_name, { "tester"_n, config::active_name }, delay_seconds, quantity);

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
   propose_approve_msig_updateauth_trx(chain, proposal_2_name, { "tester"_n, config::active_name }, delay_seconds_2);

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
   propose_approve_msig_token_transfer_trx(chain, proposal_3_name, { "tester"_n, config::active_name }, delay_seconds_3, quantity_3);

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
   exec_msig_trx(chain, proposal_1_name, {{ "tester"_n, config::active_name }});
   chain.produce_blocks();

   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("99.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester2"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("1.0000 CUR"), liquid_balance);

   // delayed update auth removing the delay will finally execute
   exec_msig_trx(chain, proposal_2_name, {{ "tester"_n, config::active_name }});
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
   exec_msig_trx(chain, proposal_3_name, {{ "tester"_n, config::active_name }});
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
   propose_approve_msig_token_transfer_trx(chain, proposal_1_name, { "tester"_n, config::active_name }, delay_seconds, quantity);

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
   propose_approve_msig_updateauth_trx(chain, proposal_2_name, { "tester"_n, config::active_name }, delay_seconds_2);

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
   propose_approve_msig_token_transfer_trx(chain, proposal_3_name, { "tester"_n, config::active_name }, delay_seconds_3, quantity_3);

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
   exec_msig_trx(chain, proposal_1_name, {{ "tester"_n, config::active_name }});
   chain.produce_blocks();

   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("99.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester2"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("1.0000 CUR"), liquid_balance);

   // delayed update auth removing the delay will finally execute
   exec_msig_trx(chain, proposal_2_name, {{ "tester"_n, config::active_name }});
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
   exec_msig_trx(chain, proposal_3_name, {{ "tester"_n, config::active_name }});
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
   propose_approve_msig_token_transfer_trx(chain, proposal_1_name, { "tester"_n, config::active_name }, delay_seconds, quantity);

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
   propose_approve_msig_linkauth_trx(chain, proposal_2_name, "second"_n, { "tester"_n, config::active_name }, delay_seconds_2);

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
   propose_approve_msig_token_transfer_trx(chain, proposal_3_name, { "tester"_n, config::active_name }, delay_seconds_3, quantity_3);

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
   exec_msig_trx(chain, proposal_1_name, {{ "tester"_n, config::active_name }});
   chain.produce_blocks();

   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("99.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester2"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("1.0000 CUR"), liquid_balance);

   // delay on minimum permission of transfer is finally removed
   exec_msig_trx(chain, proposal_2_name, {{ "tester"_n, config::active_name }});
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
   exec_msig_trx(chain, proposal_3_name, {{ "tester"_n, config::active_name }});
   chain.produce_blocks();

   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("84.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester2"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("16.0000 CUR"), liquid_balance);

} FC_LOG_AND_RETHROW() }/// link_delay_link_change_test

// test link with unlink
BOOST_AUTO_TEST_CASE( link_delay_unlink_test ) { try {
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
   constexpr name first_trnsfr_propsal_name   = "prop1"_n;
   constexpr uint32_t first_trnsfr_delay_seconds = 10;
   constexpr auto first_trnsfr_quantity          = "1.0000 CUR";
   propose_approve_msig_token_transfer_trx(chain, first_trnsfr_propsal_name, { "tester"_n, config::active_name }, first_trnsfr_delay_seconds, first_trnsfr_quantity);

   chain.produce_blocks();

   liquid_balance = get_currency_balance(chain, "eosio.token"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("999900.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("100.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester2"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("0.0000 CUR"), liquid_balance);

   BOOST_REQUIRE_EXCEPTION(
      chain.push_action( config::system_account_name, unlinkauth::get_name(),
                         vector<permission_level>{{tester_account, "first"_n}},
                         fc::mutable_variant_object()
         ("account", "tester")
         ("code", eosio_token)
         ("type", "transfer"),
         30, 0
      ),
      unsatisfied_authorization,
      fc_exception_message_starts_with("transaction declares authority")
   );

   // this transaction will be delayed 20 blocks
   constexpr name unlinkauth_proposal_name = "prop2"_n;
   propose_approve_msig_unlinkauth_trx(chain, unlinkauth_proposal_name, { "tester"_n, config::active_name }, 10);

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
   constexpr name second_trnfr_propsal_name      = "prop3"_n;
   constexpr uint32_t second_trnfr_delay_seconds = 10;
   constexpr auto second_trnfr_quantity          = "5.0000 CUR";
   propose_approve_msig_token_transfer_trx(chain, second_trnfr_propsal_name, { "tester"_n, config::active_name }, second_trnfr_delay_seconds, second_trnfr_quantity);

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
   exec_msig_trx(chain, first_trnsfr_propsal_name, {{ "tester"_n, config::active_name }});
   chain.produce_blocks();

   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("99.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester2"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("1.0000 CUR"), liquid_balance);

   // the delayed unlinkauth finally occurs
   exec_msig_trx(chain, unlinkauth_proposal_name, {{ "tester"_n, config::active_name }});
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
   exec_msig_trx(chain, second_trnfr_propsal_name, {{ "tester"_n, config::active_name }});
   chain.produce_blocks();

   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("84.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester2"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("16.0000 CUR"), liquid_balance);

} FC_LOG_AND_RETHROW() }/// link_delay_unlink_test

// test moving link with delay on permission's parent
BOOST_AUTO_TEST_CASE( link_delay_link_change_heirarchy_test ) { try {
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
           ("auth",  authority(chain.get_public_key(tester_account, "first")))
   );
   chain.push_action(config::system_account_name, linkauth::get_name(), tester_account, fc::mutable_variant_object()
           ("account", "tester")
           ("code", eosio_token)
           ("type", "transfer")
           ("requirement", "second"));
   chain.push_action(config::system_account_name, updateauth::get_name(), tester_account, fc::mutable_variant_object()
           ("account", "tester")
           ("permission", "third")
           ("parent", "active")
           ("auth",  authority(chain.get_public_key(tester_account, "third")))
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
   constexpr name first_trnsfr_propsal_name   = "prop1"_n;
   propose_approve_msig_token_transfer_trx(chain, first_trnsfr_propsal_name, { "tester"_n, config::active_name }, 10, "1.0000 CUR");

   chain.produce_blocks();

   liquid_balance = get_currency_balance(chain, "eosio.token"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("999900.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("100.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester2"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("0.0000 CUR"), liquid_balance);

   // this transaction will be delayed 20 blocks
   constexpr name linkauth_proposal_name  = "prop2"_n;
   propose_approve_msig_linkauth_trx(chain, linkauth_proposal_name, "third"_n, { "tester"_n, config::active_name }, 10);

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
   constexpr name second_trnsfr_propsal_name      = "prop3"_n;
   propose_approve_msig_token_transfer_trx(chain, second_trnsfr_propsal_name, { "tester"_n, config::active_name }, 10, "5.0000 CUR");

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
   exec_msig_trx(chain, first_trnsfr_propsal_name, {{ "tester"_n, config::active_name }});
   chain.produce_blocks();

   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("99.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester2"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("1.0000 CUR"), liquid_balance);

   // delay on minimum permission of transfer is finally removed
   exec_msig_trx(chain, linkauth_proposal_name, {{ "tester"_n, config::active_name }});
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
   exec_msig_trx(chain, second_trnsfr_propsal_name, {{ "tester"_n, config::active_name }});
   chain.produce_blocks();

   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("84.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester2"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("16.0000 CUR"), liquid_balance);

} FC_LOG_AND_RETHROW() } /// link_delay_link_change_heirarchy_test

// test canceldelay action cancelling a delayed transaction
BOOST_AUTO_TEST_CASE( canceldelay_test ) { try {
   validating_tester_no_disable_deferred_trx chain;
   chain.produce_block();

   const auto& tester_account = "tester"_n;
   std::vector<transaction_id_type> ids;

   chain.produce_blocks();
   chain.create_account("eosio.token"_n);
   chain.produce_blocks(10);

   chain.set_code("eosio.token"_n, test_contracts::eosio_token_wasm());
   chain.set_abi("eosio.token"_n, test_contracts::eosio_token_abi());

   chain.produce_blocks();
   chain.create_account("tester"_n);
   chain.create_account("tester2"_n);
   chain.produce_blocks(10);

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
   auto gen_size = chain.control->db().get_index<generated_transaction_multi_index,by_trx_id>().size();
   BOOST_REQUIRE_EQUAL(0u, gen_size);

   chain.produce_blocks();
   auto liquid_balance = get_currency_balance(chain, "eosio.token"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("999900.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("100.0000 CUR"), liquid_balance);

   // this transaction will be delayed 20 blocks
   trace = chain.push_action("eosio.token"_n, name("transfer"), "tester"_n, fc::mutable_variant_object()
       ("from", "tester")
       ("to", "tester2")
       ("quantity", "1.0000 CUR")
       ("memo", "hi" ),
       30, 10
   );
   //wdump((fc::json::to_pretty_string(trace)));
   ids.push_back(trace->id);
   BOOST_REQUIRE_EQUAL(transaction_receipt::delayed, trace->receipt->status);
   gen_size = chain.control->db().get_index<generated_transaction_multi_index,by_trx_id>().size();
   BOOST_CHECK_EQUAL(1u, gen_size);
   BOOST_CHECK_EQUAL(0u, trace->action_traces.size());

   const auto& idx = chain.control->db().get_index<generated_transaction_multi_index,by_trx_id>();
   auto itr = idx.find( trace->id );
   BOOST_CHECK_EQUAL( (itr != idx.end()), true );

   chain.produce_blocks();

   liquid_balance = get_currency_balance(chain, "eosio.token"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("999900.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("100.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester2"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("0.0000 CUR"), liquid_balance);

   BOOST_REQUIRE_EXCEPTION(
      chain.push_action( config::system_account_name,
                         updateauth::get_name(),
                         vector<permission_level>{{tester_account, "first"_n}},
                         fc::mutable_variant_object()
            ("account", "tester")
            ("permission", "first")
            ("parent", "active")
            ("auth",  authority(chain.get_public_key(tester_account, "first"))),
            30, 7
      ),
      unsatisfied_authorization,
      fc_exception_message_starts_with("transaction declares authority")
   );

   // this transaction will be delayed 20 blocks
   trace = chain.push_action(config::system_account_name, updateauth::get_name(), tester_account, fc::mutable_variant_object()
           ("account", "tester")
           ("permission", "first")
           ("parent", "active")
           ("auth",  authority(chain.get_public_key(tester_account, "first"))),
           30, 10
   );
   //wdump((fc::json::to_pretty_string(trace)));
   ids.push_back(trace->id);
   BOOST_REQUIRE_EQUAL(transaction_receipt::delayed, trace->receipt->status);
   gen_size = chain.control->db().get_index<generated_transaction_multi_index,by_trx_id>().size();
   BOOST_CHECK_EQUAL(2u, gen_size);
   BOOST_CHECK_EQUAL(0u, trace->action_traces.size());

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
   trace = chain.push_action("eosio.token"_n, name("transfer"), "tester"_n, fc::mutable_variant_object()
       ("from", "tester")
       ("to", "tester2")
       ("quantity", "5.0000 CUR")
       ("memo", "hi" ),
       30, 10
   );
   //wdump((fc::json::to_pretty_string(trace)));
   ids.push_back(trace->id);
   BOOST_REQUIRE_EQUAL(transaction_receipt::delayed, trace->receipt->status);
   gen_size = chain.control->db().get_index<generated_transaction_multi_index,by_trx_id>().size();
   BOOST_CHECK_EQUAL(3u, gen_size);
   BOOST_CHECK_EQUAL(0u, trace->action_traces.size());

   chain.produce_blocks();

   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("100.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester2"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("0.0000 CUR"), liquid_balance);

   // send canceldelay for first delayed transaction
   signed_transaction trx;
   trx.actions.emplace_back(vector<permission_level>{{"tester"_n, config::active_name}},
                            chain::canceldelay{{"tester"_n, config::active_name}, ids[0]});

   chain.set_transaction_headers(trx);
   trx.sign(chain.get_private_key("tester"_n, "active"), chain.control->get_chain_id());
   // first push as a dry_run trx
   trace = chain.push_transaction(trx, fc::time_point::maximum(), base_tester::DEFAULT_BILLED_CPU_TIME_US, false, transaction_metadata::trx_type::dry_run);
   BOOST_REQUIRE_EQUAL(transaction_receipt::executed, trace->receipt->status);
   // now push for real
   trace = chain.push_transaction(trx);
   BOOST_REQUIRE_EQUAL(transaction_receipt::executed, trace->receipt->status);
   gen_size = chain.control->db().get_index<generated_transaction_multi_index,by_trx_id>().size();
   BOOST_CHECK_EQUAL(2u, gen_size);

   const auto& cidx = chain.control->db().get_index<generated_transaction_multi_index,by_trx_id>();
   auto citr = cidx.find( ids[0] );
   BOOST_CHECK_EQUAL( (citr == cidx.end()), true );

   chain.produce_blocks();

   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("100.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester2"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("0.0000 CUR"), liquid_balance);

   gen_size = chain.control->db().get_index<generated_transaction_multi_index,by_trx_id>().size();
   BOOST_CHECK_EQUAL(2u, gen_size);

   chain.produce_blocks();

   gen_size = chain.control->db().get_index<generated_transaction_multi_index,by_trx_id>().size();
   BOOST_CHECK_EQUAL(2u, gen_size);

   chain.produce_blocks();
   // update auth will finally be performed

   gen_size = chain.control->db().get_index<generated_transaction_multi_index,by_trx_id>().size();
   BOOST_CHECK_EQUAL(1u, gen_size);

   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("100.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester2"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("0.0000 CUR"), liquid_balance);

   // this transfer is performed right away since delay is removed
   trace = chain.push_action("eosio.token"_n, name("transfer"), "tester"_n, fc::mutable_variant_object()
       ("from", "tester")
       ("to", "tester2")
       ("quantity", "10.0000 CUR")
       ("memo", "hi" )
   );
   //wdump((fc::json::to_pretty_string(trace)));
   BOOST_REQUIRE_EQUAL(transaction_receipt::executed, trace->receipt->status);

   gen_size = chain.control->db().get_index<generated_transaction_multi_index,by_trx_id>().size();
   BOOST_CHECK_EQUAL(1u, gen_size);

   chain.produce_blocks();

   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("90.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester2"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("10.0000 CUR"), liquid_balance);

   chain.produce_blocks(15);

   gen_size = chain.control->db().get_index<generated_transaction_multi_index,by_trx_id>().size();
   BOOST_CHECK_EQUAL(1u, gen_size);

   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("90.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester2"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("10.0000 CUR"), liquid_balance);

   // second transfer finally is performed
   chain.produce_blocks();

   gen_size = chain.control->db().get_index<generated_transaction_multi_index,by_trx_id>().size();
   BOOST_CHECK_EQUAL(0u, gen_size);

   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("85.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester2"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("15.0000 CUR"), liquid_balance);
} FC_LOG_AND_RETHROW() } /// canceldelay_test

// test canceldelay action under different permission levels
BOOST_AUTO_TEST_CASE( canceldelay_test2 ) { try {
   validating_tester_no_disable_deferred_trx chain;
   chain.produce_block();

   const auto& tester_account = "tester"_n;

   chain.produce_blocks();
   chain.create_account("eosio.token"_n);
   chain.produce_blocks();

   chain.set_code("eosio.token"_n, test_contracts::eosio_token_wasm());
   chain.set_abi("eosio.token"_n, test_contracts::eosio_token_abi());

   chain.produce_blocks();
   chain.create_account("tester"_n);
   chain.create_account("tester2"_n);
   chain.produce_blocks();

   chain.push_action(config::system_account_name, updateauth::get_name(), tester_account, fc::mutable_variant_object()
           ("account", "tester")
           ("permission", "first")
           ("parent", "active")
           ("auth",  authority(chain.get_public_key(tester_account, "first"), 5))
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
   auto gen_size = chain.control->db().get_index<generated_transaction_multi_index,by_trx_id>().size();
   BOOST_REQUIRE_EQUAL(0u, gen_size);

   chain.produce_blocks();
   auto liquid_balance = get_currency_balance(chain, "eosio.token"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("999900.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("100.0000 CUR"), liquid_balance);

   ilog("attempting first delayed transfer");

   {
      // this transaction will be delayed 10 blocks
      trace = chain.push_action("eosio.token"_n, name("transfer"), vector<permission_level>{{"tester"_n, "first"_n}}, fc::mutable_variant_object()
          ("from", "tester")
          ("to", "tester2")
          ("quantity", "1.0000 CUR")
          ("memo", "hi" ),
          30, 5
      );
      auto trx_id = trace->id;
      BOOST_REQUIRE_EQUAL(transaction_receipt::delayed, trace->receipt->status);
      gen_size = chain.control->db().get_index<generated_transaction_multi_index,by_trx_id>().size();
      BOOST_REQUIRE_EQUAL(1u, gen_size);
      BOOST_REQUIRE_EQUAL(0u, trace->action_traces.size());

      const auto& idx = chain.control->db().get_index<generated_transaction_multi_index,by_trx_id>();
      auto itr = idx.find( trx_id );
      BOOST_CHECK_EQUAL( (itr != idx.end()), true );

      chain.produce_blocks();

      liquid_balance = get_currency_balance(chain, "tester"_n);
      BOOST_REQUIRE_EQUAL(asset::from_string("100.0000 CUR"), liquid_balance);
      liquid_balance = get_currency_balance(chain, "tester2"_n);
      BOOST_REQUIRE_EQUAL(asset::from_string("0.0000 CUR"), liquid_balance);

      // attempt canceldelay with wrong canceling_auth for delayed transfer of 1.0000 CUR
      {
         signed_transaction trx;
         trx.actions.emplace_back(vector<permission_level>{{"tester"_n, config::active_name}},
                                  chain::canceldelay{{"tester"_n, config::active_name}, trx_id});
         chain.set_transaction_headers(trx);
         trx.sign(chain.get_private_key("tester"_n, "active"), chain.control->get_chain_id());
         BOOST_REQUIRE_EXCEPTION( chain.push_transaction(trx), action_validate_exception,
                                  fc_exception_message_is("canceling_auth in canceldelay action was not found as authorization in the original delayed transaction") );
      }

      // attempt canceldelay with "second" permission for delayed transfer of 1.0000 CUR
      {
         signed_transaction trx;
         trx.actions.emplace_back(vector<permission_level>{{"tester"_n, "second"_n}},
                                  chain::canceldelay{{"tester"_n, "first"_n}, trx_id});
         chain.set_transaction_headers(trx);
         trx.sign(chain.get_private_key("tester"_n, "second"), chain.control->get_chain_id());
         BOOST_REQUIRE_THROW( chain.push_transaction(trx), irrelevant_auth_exception );
         BOOST_REQUIRE_EXCEPTION( chain.push_transaction(trx), irrelevant_auth_exception,
                                  fc_exception_message_starts_with("canceldelay action declares irrelevant authority") );
      }

      // canceldelay with "active" permission for delayed transfer of 1.0000 CUR
      signed_transaction trx;
      trx.actions.emplace_back(vector<permission_level>{{"tester"_n, config::active_name}},
                               chain::canceldelay{{"tester"_n, "first"_n}, trx_id});
      chain.set_transaction_headers(trx);
      trx.sign(chain.get_private_key("tester"_n, "active"), chain.control->get_chain_id());
      trace = chain.push_transaction(trx);

      BOOST_REQUIRE_EQUAL(transaction_receipt::executed, trace->receipt->status);
      gen_size = chain.control->db().get_index<generated_transaction_multi_index,by_trx_id>().size();
      BOOST_REQUIRE_EQUAL(0u, gen_size);

      const auto& cidx = chain.control->db().get_index<generated_transaction_multi_index,by_trx_id>();
      auto citr = cidx.find( trx_id );
      BOOST_REQUIRE_EQUAL( (citr == cidx.end()), true );

      chain.produce_blocks(10);

      liquid_balance = get_currency_balance(chain, "tester"_n);
      BOOST_REQUIRE_EQUAL(asset::from_string("100.0000 CUR"), liquid_balance);
      liquid_balance = get_currency_balance(chain, "tester2"_n);
      BOOST_REQUIRE_EQUAL(asset::from_string("0.0000 CUR"), liquid_balance);
   }

   ilog("reset minimum permission of transfer to second permission");

   chain.push_action(config::system_account_name, linkauth::get_name(), tester_account, fc::mutable_variant_object()
           ("account", "tester")
           ("code", eosio_token)
           ("type", "transfer")
           ("requirement", "second"),
           30, 5
   );

   chain.produce_blocks(11);


   ilog("attempting second delayed transfer");
   {
      // this transaction will be delayed 10 blocks
      trace = chain.push_action("eosio.token"_n, name("transfer"), vector<permission_level>{{"tester"_n, "second"_n}}, fc::mutable_variant_object()
          ("from", "tester")
          ("to", "tester2")
          ("quantity", "5.0000 CUR")
          ("memo", "hi" ),
          30, 5
      );
      auto trx_id = trace->id;
      BOOST_REQUIRE_EQUAL(transaction_receipt::delayed, trace->receipt->status);
      auto gen_size = chain.control->db().get_index<generated_transaction_multi_index,by_trx_id>().size();
      BOOST_CHECK_EQUAL(1u, gen_size);
      BOOST_CHECK_EQUAL(0u, trace->action_traces.size());

      const auto& idx = chain.control->db().get_index<generated_transaction_multi_index,by_trx_id>();
      auto itr = idx.find( trx_id );
      BOOST_CHECK_EQUAL( (itr != idx.end()), true );

      chain.produce_blocks();

      liquid_balance = get_currency_balance(chain, "tester"_n);
      BOOST_REQUIRE_EQUAL(asset::from_string("100.0000 CUR"), liquid_balance);
      liquid_balance = get_currency_balance(chain, "tester2"_n);
      BOOST_REQUIRE_EQUAL(asset::from_string("0.0000 CUR"), liquid_balance);

      // canceldelay with "first" permission for delayed transfer of 5.0000 CUR
      signed_transaction trx;
      trx.actions.emplace_back(vector<permission_level>{{"tester"_n, "first"_n}},
                               chain::canceldelay{{"tester"_n, "second"_n}, trx_id});
      chain.set_transaction_headers(trx);
      trx.sign(chain.get_private_key("tester"_n, "first"), chain.control->get_chain_id());
      trace = chain.push_transaction(trx);

      BOOST_REQUIRE_EQUAL(transaction_receipt::executed, trace->receipt->status);
      gen_size = chain.control->db().get_index<generated_transaction_multi_index,by_trx_id>().size();
      BOOST_REQUIRE_EQUAL(0u, gen_size);

      const auto& cidx = chain.control->db().get_index<generated_transaction_multi_index,by_trx_id>();
      auto citr = cidx.find( trx_id );
      BOOST_REQUIRE_EQUAL( (citr == cidx.end()), true );

      chain.produce_blocks(10);

      liquid_balance = get_currency_balance(chain, "tester"_n);
      BOOST_REQUIRE_EQUAL(asset::from_string("100.0000 CUR"), liquid_balance);
      liquid_balance = get_currency_balance(chain, "tester2"_n);
      BOOST_REQUIRE_EQUAL(asset::from_string("0.0000 CUR"), liquid_balance);
   }

   ilog("attempting third delayed transfer");

   {
      // this transaction will be delayed 10 blocks
      trace = chain.push_action("eosio.token"_n, name("transfer"), vector<permission_level>{{"tester"_n, config::owner_name}}, fc::mutable_variant_object()
          ("from", "tester")
          ("to", "tester2")
          ("quantity", "10.0000 CUR")
          ("memo", "hi" ),
          30, 5
      );
      auto trx_id = trace->id;
      BOOST_REQUIRE_EQUAL(transaction_receipt::delayed, trace->receipt->status);
      gen_size = chain.control->db().get_index<generated_transaction_multi_index,by_trx_id>().size();
      BOOST_REQUIRE_EQUAL(1u, gen_size);
      BOOST_REQUIRE_EQUAL(0u, trace->action_traces.size());

      const auto& idx = chain.control->db().get_index<generated_transaction_multi_index,by_trx_id>();
      auto itr = idx.find( trx_id );
      BOOST_CHECK_EQUAL( (itr != idx.end()), true );

      chain.produce_blocks();

      liquid_balance = get_currency_balance(chain, "tester"_n);
      BOOST_REQUIRE_EQUAL(asset::from_string("100.0000 CUR"), liquid_balance);
      liquid_balance = get_currency_balance(chain, "tester2"_n);
      BOOST_REQUIRE_EQUAL(asset::from_string("0.0000 CUR"), liquid_balance);

      // attempt canceldelay with "active" permission for delayed transfer of 10.0000 CUR
      {
         signed_transaction trx;
         trx.actions.emplace_back(vector<permission_level>{{"tester"_n, "active"_n}},
                                  chain::canceldelay{{"tester"_n, config::owner_name}, trx_id});
         chain.set_transaction_headers(trx);
         trx.sign(chain.get_private_key("tester"_n, "active"), chain.control->get_chain_id());
         BOOST_REQUIRE_THROW( chain.push_transaction(trx), irrelevant_auth_exception );
      }

      // canceldelay with "owner" permission for delayed transfer of 10.0000 CUR
      signed_transaction trx;
      trx.actions.emplace_back(vector<permission_level>{{"tester"_n, config::owner_name}},
                               chain::canceldelay{{"tester"_n, config::owner_name}, trx_id});
      chain.set_transaction_headers(trx);
      trx.sign(chain.get_private_key("tester"_n, "owner"), chain.control->get_chain_id());
      trace = chain.push_transaction(trx);

      BOOST_REQUIRE_EQUAL(transaction_receipt::executed, trace->receipt->status);
      gen_size = chain.control->db().get_index<generated_transaction_multi_index,by_trx_id>().size();
      BOOST_REQUIRE_EQUAL(0u, gen_size);

      const auto& cidx = chain.control->db().get_index<generated_transaction_multi_index,by_trx_id>();
      auto citr = cidx.find( trx_id );
      BOOST_REQUIRE_EQUAL( (citr == cidx.end()), true );

      chain.produce_blocks(10);

      liquid_balance = get_currency_balance(chain, "tester"_n);
      BOOST_REQUIRE_EQUAL(asset::from_string("100.0000 CUR"), liquid_balance);
      liquid_balance = get_currency_balance(chain, "tester2"_n);
      BOOST_REQUIRE_EQUAL(asset::from_string("0.0000 CUR"), liquid_balance);
   }
} FC_LOG_AND_RETHROW() } /// canceldelay_test2

BOOST_AUTO_TEST_CASE( max_transaction_delay_create ) { try {
   //assuming max transaction delay is 45 days (default in config.hpp)
   validating_tester chain;

   const auto& tester_account = "tester"_n;

   chain.produce_blocks();
   chain.create_account("tester"_n);
   chain.produce_blocks(10);

   BOOST_REQUIRE_EXCEPTION(
      chain.push_action(config::system_account_name, updateauth::get_name(), tester_account, fc::mutable_variant_object()
                        ("account", "tester")
                        ("permission", "first")
                        ("parent", "active")
                        ("auth",  authority(chain.get_public_key(tester_account, "first"), 50*86400)) ), // 50 days delay
      action_validate_exception,
      fc_exception_message_starts_with("Cannot set delay longer than max_transacton_delay")
   );
} FC_LOG_AND_RETHROW() } /// max_transaction_delay_create


BOOST_AUTO_TEST_CASE( max_transaction_delay_execute ) { try {
   //assuming max transaction delay is 45 days (default in config.hpp)
   validating_tester chain;
   const auto& tester_account = "tester"_n;

   create_accounts(chain);

   chain.push_action("eosio.token"_n, "create"_n, "eosio.token"_n, mutable_variant_object()
           ("issuer", "eosio.token" )
           ("maximum_supply", "9000000.0000 CUR" )
   );
   chain.push_action("eosio.token"_n, name("issue"), "eosio.token"_n, fc::mutable_variant_object()
           ("to",       "tester")
           ("quantity", "100.0000 CUR")
           ("memo", "for stuff")
   );

   //create a permission level with delay 30 days and associate it with token transfer
   auto trace = chain.push_action(config::system_account_name, updateauth::get_name(), tester_account, fc::mutable_variant_object()
                     ("account", "tester")
                     ("permission", "first")
                     ("parent", "active")
                     ("auth",  authority(chain.get_public_key(tester_account, "first"), 30*86400)) // 30 days delay
   );
   BOOST_REQUIRE_EQUAL(transaction_receipt::executed, trace->receipt->status);

   trace = chain.push_action(config::system_account_name, linkauth::get_name(), tester_account, fc::mutable_variant_object()
                     ("account", "tester")
                     ("code", "eosio.token")
                     ("type", "transfer")
                     ("requirement", "first"));
   BOOST_REQUIRE_EQUAL(transaction_receipt::executed, trace->receipt->status);

   chain.produce_blocks();

   //change max_transaction_delay to 60 sec
   auto params = chain.control->get_global_properties().configuration;
   params.max_transaction_delay = 60;
   chain.push_action( config::system_account_name, "setparams"_n, config::system_account_name, mutable_variant_object()
                        ("params", params) );

   chain.produce_blocks();
   //should be able to create a msig transaction with delay 60 sec, despite permission delay being 30 days, because max_transaction_delay is 60 sec
   constexpr name proposal_name = "prop1"_n;
   propose_approve_msig_token_transfer_trx(chain, proposal_name, { "tester"_n, config::active_name }, 60, "9.0000 CUR");

   //check that the delayed msig transaction can be executed after after 60 sec
   chain.produce_blocks(120);
   exec_msig_trx(chain, proposal_name, {{ "tester"_n, config::active_name }});

   //check that the transfer really happened
   auto liquid_balance = get_currency_balance(chain, "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("91.0000 CUR"), liquid_balance);

} FC_LOG_AND_RETHROW() } /// max_transaction_delay_execute

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

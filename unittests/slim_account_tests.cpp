#include <eosio/chain/controller.hpp>
#include <eosio/chain/global_property_object.hpp>
#include <eosio/chain/permission_object.hpp>
#include <eosio/chain/resource_limits.hpp>
#include <eosio/chain/transaction.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/iostreams/device/mapped_file.hpp>
#include <eosio/testing/tester.hpp>
#include <eosio/chain/exceptions.hpp> /* config_parse_error */
#include <test_contracts.hpp>
#include <eosio/chain/resource_limits.hpp>
#include <eosio/chain/resource_limits_private.hpp>

#include "fork_test_utilities.hpp"
#include "test_cfd_transaction.hpp"

using namespace eosio;
using namespace eosio::chain;
using namespace eosio::testing;

class slim_account_tester : public validating_tester
{
public:
   slim_account_tester() : validating_tester() {
   }

   void setup()
   {
      set_code( config::system_account_name, test_contracts::create_slim_account_test_wasm() );
      set_abi( config::system_account_name, test_contracts::create_slim_account_test_abi().data());

      produce_block();
   }

   transaction_trace_ptr create_slim_account(account_name a, name creator = config::system_account_name, bool include_code = true )
   {
      authority active_auth( get_public_key( a, "active" ) );

      auto sort_permissions = []( authority& auth ) {
         std::sort( auth.accounts.begin(), auth.accounts.end(),
                    []( const permission_level_weight& lhs, const permission_level_weight& rhs ) {
                        return lhs.permission < rhs.permission;
                    }
                  );
      };

      if( include_code ) {
         FC_ASSERT( active_auth.threshold <= std::numeric_limits<weight_type>::max(), "threshold is too high" );
         active_auth.accounts.push_back( permission_level_weight{ {a, config::eosio_code_name},
                                          static_cast<weight_type>(active_auth.threshold) } );
         sort_permissions(active_auth);
      }
      return push_action( config::system_account_name, "testcreate"_n, creator, fc::mutable_variant_object()
         ("creator", creator.to_string())
         ("account", a)
         ("active_auth", active_auth)
      );
   }
};

BOOST_AUTO_TEST_SUITE(slim_account_tests)

BOOST_FIXTURE_TEST_CASE(newslimacc_test, slim_account_tester)
{
   try
   {
      create_slim_account(name("joe"));
      produce_block();
      // Verify account created properly
      {
         // Verify account object
         const auto &accnt = control->db().get<account_object, by_name>(name("joe"));
         BOOST_TEST(accnt.name == "joe"_n);
         BOOST_CHECK_EQUAL(accnt.recv_sequence, 0u);
         BOOST_CHECK_EQUAL(accnt.auth_sequence, 0u);

         auto account_metadata = control->db().find<account_metadata_object, by_name>(name("joe"));
         BOOST_TEST(account_metadata == nullptr);
         // Verify account object
         const auto &joe_active_authority = get<permission_object, by_owner>(boost::make_tuple(name("joe"), name("active")));
         BOOST_TEST(joe_active_authority.auth.threshold == 1u);
         BOOST_TEST(joe_active_authority.auth.accounts.size() == 1u);
         BOOST_TEST(joe_active_authority.auth.keys.size() == 1u);
         BOOST_TEST(joe_active_authority.auth.keys[0].key.to_string({}) == get_public_key(name("joe"), "active").to_string({}));
         BOOST_TEST(joe_active_authority.auth.keys[0].weight == 1u);

         const auto &joe_owner_authority = find<permission_object, by_owner>(boost::make_tuple(name("joe"), name("owner")));
         BOOST_TEST(joe_owner_authority == nullptr);

         // Verify resource limit object
         using resource_limits_object = eosio::chain::resource_limits::resource_limits_object;
         using by_owner = eosio::chain::resource_limits::by_owner;
         const auto &limits = get<resource_limits_object, by_owner>(name("joe"));
         BOOST_TEST(limits.net_weight == -1);
         BOOST_TEST(limits.cpu_weight == -1);
         BOOST_TEST(limits.ram_bytes == -1);
         BOOST_TEST(limits.cpu_usage.average() == 0U);
         BOOST_TEST(limits.net_usage.average() == 0U);
         BOOST_TEST(limits.ram_usage > 0U);
      }

      // Creating new slim account from a slim account creator
      create_slim_account(name("alice"), name("joe"));

      // Verify account created properly
      const auto &alice_owner_authority = find<permission_object, by_owner>(boost::make_tuple(name("alice"), name("owner")));
      BOOST_TEST(alice_owner_authority == nullptr);
      const auto &alice_active_authority = find<permission_object, by_owner>(boost::make_tuple(name("alice"), name("active")));
      BOOST_TEST(alice_active_authority != nullptr);

      // Create duplicate name
      BOOST_CHECK_EXCEPTION(create_account(name("joe")), action_validate_exception,
                            fc_exception_message_is("Cannot create account named joe, as that name is already taken"));
      // Create duplicate name
      BOOST_CHECK_EXCEPTION(create_slim_account(name("joe")), action_validate_exception,
                            fc_exception_message_is("Cannot create account named joe, as that name is already taken"));

      // Creating account with name more than 12 chars
      BOOST_CHECK_EXCEPTION(create_slim_account(name("aaaaaaaaaaaaa"), name("joe")), action_validate_exception,
                            fc_exception_message_is("account names can only be 12 chars long"));

      // Creating account with eosio. prefix, should fail
      BOOST_CHECK_EXCEPTION(create_slim_account(name("eosio.test1")), action_validate_exception,
                            fc_exception_message_is("only newaccount action can create account with name start with 'eosio.'"));
   }
   FC_LOG_AND_RETHROW()
}
BOOST_FIXTURE_TEST_CASE(updateaut_test, slim_account_tester)
{
   try
   {
      create_slim_account(name("alice"));
      create_slim_account(name("bob"));
      // Deleting active should fail
      BOOST_CHECK_THROW(delete_authority(name("alice"), name("owner")), transaction_exception);
      BOOST_CHECK_THROW(delete_authority(name("alice"), name("active")), transaction_exception);
      const auto alice_active_priv_key = get_private_key(name("alice"), "active");
      const auto alice_active_pub_key = alice_active_priv_key.get_public_key();

      // Deleting owner should fail
      BOOST_CHECK_THROW(delete_authority(name("alice"), name("owner"), {permission_level{name("alice"), name("active")}}, {alice_active_priv_key}), permission_query_exception);
      BOOST_CHECK_THROW(delete_authority(name("alice"), name("active"), {permission_level{name("alice"), name("active")}}, {alice_active_priv_key}), action_validate_exception);

      // Change owner permission
      const auto alice_owner_priv_key = get_private_key(name("alice"), "new_owner");
      const auto alice_owner_pub_key = alice_owner_priv_key.get_public_key();
      BOOST_CHECK_THROW(set_authority(name("alice"), name("owner"), authority(alice_owner_pub_key), {}, {permission_level{name("alice"), name("active")}}, {alice_active_priv_key}), invalid_permission);
      produce_blocks();

      // Ensure there is no owner permission
      {
         auto obj = find<permission_object, by_owner>(boost::make_tuple(name("alice"), name("owner")));
         BOOST_TEST(obj == nullptr);
      }

      // set active permission, remember that the owner key has been changed
      const auto new_active_priv_key = get_private_key(name("alice"), "new_active");
      const auto new_active_pub_key = new_active_priv_key.get_public_key();
      set_authority(name("alice"), name("active"), authority(new_active_pub_key), {},
                          {permission_level{name("alice"), name("active")}}, {alice_active_priv_key});
      produce_blocks();

      {
         auto obj = find<permission_object, by_owner>(boost::make_tuple(name("alice"), name("active")));
         BOOST_TEST(obj != nullptr);
         BOOST_TEST(obj->owner == name("alice"));
         BOOST_TEST(obj->name == name("active"));
         // BOOST_TEST(obj->parent == name(""));
         auto auth = obj->auth.to_authority();
         BOOST_TEST(auth.threshold == 1u);
         BOOST_TEST(auth.keys.size() == 1u);
         BOOST_TEST(auth.accounts.size() == 0u);
         BOOST_TEST(auth.keys[0].key == new_active_pub_key);
         BOOST_TEST(auth.keys[0].weight == 1u);
      }

      auto spending_priv_key = get_private_key(name("alice"), "spending");
      auto spending_pub_key = spending_priv_key.get_public_key();
      auto trading_priv_key = get_private_key(name("alice"), "trading");
      auto trading_pub_key = trading_priv_key.get_public_key();

      // add bob active permission
      const auto bob_active_priv_key = get_private_key(name("bob"), "active");
      const auto bob_active_pub_key = bob_active_priv_key.get_public_key();
      set_authority(name("bob"), name("active"), authority(bob_active_pub_key), name(""),
                          {permission_level{name("bob"), name("active")}}, {bob_active_priv_key});
      // Bob attempts to create new spending auth for Alice
      BOOST_CHECK_THROW(set_authority(name("alice"), name("spending"), authority(spending_pub_key), name("active"),
                                            {permission_level{name("bob"), name("active")}},
                                            {bob_active_priv_key}),
                        irrelevant_auth_exception);

      // Create new spending auth with parent is empty, should fail
      BOOST_CHECK_THROW(set_authority(name("alice"), name("spending"), authority(spending_pub_key), name(""),
                                            {permission_level{name("alice"), name("active")}}, {new_active_priv_key}),
                        invalid_permission);
      // Create new spending auth
      set_authority(name("alice"), name("spending"), authority(spending_pub_key), name("active"),
                          {permission_level{name("alice"), name("active")}}, {new_active_priv_key});
      produce_blocks();
      {
         auto obj = find<permission_object, by_owner>(boost::make_tuple(name("alice"), name("spending")));
         BOOST_TEST(obj != nullptr);
         BOOST_TEST(obj->owner == name("alice"));
         BOOST_TEST(obj->name == name("spending"));
         BOOST_TEST(get<permission_object>(obj->parent).owner == name("alice"));
         BOOST_TEST(get<permission_object>(obj->parent).name == name("active"));
      }

      // Update spending auth parent to be its own, should fail
      BOOST_CHECK_THROW(set_authority(name("alice"), name("spending"), authority{spending_pub_key}, name("spending"),
                                            {permission_level{name("alice"), name("spending")}}, {spending_priv_key}),
                        action_validate_exception);
      // Update spending auth parent to be owner, should fail
      BOOST_CHECK_THROW(set_authority(name("alice"), name("spending"), authority{spending_pub_key}, name("owner"),
                                            {permission_level{name("alice"), name("spending")}}, {spending_priv_key}),
                        permission_query_exception);

      // Remove spending auth
      delete_authority(name("alice"), name("spending"), {permission_level{name("alice"), name("active")}}, {new_active_priv_key});
      {
         auto obj = find<permission_object, by_owner>(boost::make_tuple(name("alice"), name("spending")));
         BOOST_TEST(obj == nullptr);
      }
      produce_blocks();

      // Create new trading auth
      set_authority(name("alice"), name("trading"), authority{trading_pub_key}, name("active"),
                          {permission_level{name("alice"), name("active")}}, {new_active_priv_key});
      // Recreate spending auth again, however this time, it's under trading instead of owner
      set_authority(name("alice"), name("spending"), authority{spending_pub_key}, name("trading"),
                          {permission_level{name("alice"), name("trading")}}, {trading_priv_key});
      produce_blocks();

      // Verify correctness of trading and spending
      {
         const auto *trading = find<permission_object, by_owner>(boost::make_tuple(name("alice"), name("trading")));
         const auto *spending = find<permission_object, by_owner>(boost::make_tuple(name("alice"), name("spending")));
         BOOST_TEST(trading != nullptr);
         BOOST_TEST(spending != nullptr);
         BOOST_TEST(trading->owner == name("alice"));
         BOOST_TEST(spending->owner == name("alice"));
         BOOST_TEST(trading->name == name("trading"));
         BOOST_TEST(spending->name == name("spending"));
         BOOST_TEST(spending->parent == trading->id);
         BOOST_TEST(get(trading->parent).owner == name("alice"));
         BOOST_TEST(get(trading->parent).name == name("active"));
      }

      // Delete trading, should fail since it has children (spending)
      BOOST_CHECK_THROW(delete_authority(name("alice"), name("trading"),
                                               {permission_level{name("alice"), name("active")}}, {new_active_priv_key}),
                        action_validate_exception);
      // Update trading parent to be spending, should fail since changing parent authority is not supported
      BOOST_CHECK_THROW(set_authority(name("alice"), name("trading"), authority{trading_pub_key}, name("spending"),
                                            {permission_level{name("alice"), name("trading")}}, {trading_priv_key}),
                        action_validate_exception);

      // Delete spending auth
      delete_authority(name("alice"), name("spending"), {permission_level{name("alice"), name("active")}}, {new_active_priv_key});
      BOOST_TEST((find<permission_object, by_owner>(boost::make_tuple(name("alice"), name("spending")))) == nullptr);
      // Delete trading auth, now it should succeed since it doesn't have any children anymore
      delete_authority(name("alice"), name("trading"), {permission_level{name("alice"), name("active")}}, {new_active_priv_key});
      BOOST_TEST((find<permission_object, by_owner>(boost::make_tuple(name("alice"), name("trading")))) == nullptr);
   }
   FC_LOG_AND_RETHROW()
}

BOOST_FIXTURE_TEST_CASE(deleteauth_test, slim_account_tester)
{
   const auto& tester_account = "tester"_n;

   produce_blocks();
   create_slim_account(name("testcontract"));
   produce_blocks(10);

   set_code(name("testcontract"), test_contracts::eosio_token_wasm());
   set_abi(name("testcontract"), test_contracts::eosio_token_abi());

   produce_blocks();
   create_slim_account("tester"_n);
   create_slim_account("tester2"_n);
   produce_blocks(10);

   transaction_trace_ptr trace;

   // can't delete auth because it doesn't exist
   BOOST_REQUIRE_EXCEPTION(
      delete_authority(tester_account, name("first"), {permission_level{tester_account, name("active")}}, {get_private_key(tester_account, "active")}),
      permission_query_exception,
   [] (const permission_query_exception &e)->bool {
      expect_assert_message(e, "permission_query_exception: Permission Query Exception\nFailed to retrieve permission");
      return true;
   });

   // update auth
   set_authority(name("tester"), name("first"), authority(get_public_key(tester_account, "first")), name("active"),
                          {permission_level{tester_account, name("active")}}, {get_private_key(tester_account, "active")});

   // link auth
   link_authority(name("tester"), name("testcontract"), name("first"), name("transfer"));

   // create CUR token
   produce_blocks();
   push_action(name("testcontract"), "create"_n, name("testcontract"), mutable_variant_object()
           ("issuer", "testcontract" )
           ("maximum_supply", "9000000.0000 CUR" )
   );

   // issue to account "testcontract"
   push_action(name("testcontract"), name("issue"), name("testcontract"), fc::mutable_variant_object()
           ("to",       "testcontract")
           ("quantity", "1000000.0000 CUR")
           ("memo", "for stuff")
   );

   // transfer from testcontract to tester
   trace = push_action(name("testcontract"), name("transfer"), name("testcontract"), fc::mutable_variant_object()
       ("from", "testcontract")
       ("to", "tester")
       ("quantity", "100.0000 CUR")
       ("memo", "hi" )
   );
   BOOST_REQUIRE_EQUAL(transaction_receipt::executed, trace->receipt->status);

   produce_blocks();

   auto liquid_balance = get_currency_balance(name("testcontract"), symbol(SY(4,CUR)), name("testcontract"));
   BOOST_REQUIRE_EQUAL(asset::from_string("999900.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(name("testcontract"), symbol(SY(4,CUR)), "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("100.0000 CUR"), liquid_balance);

   trace = push_action(name("testcontract"), name("transfer"), "tester"_n, fc::mutable_variant_object()
       ("from", "tester")
       ("to", "tester2")
       ("quantity", "1.0000 CUR")
       ("memo", "hi" )
   );

   BOOST_REQUIRE_EQUAL(transaction_receipt::executed, trace->receipt->status);

   liquid_balance = get_currency_balance(name("testcontract"), symbol(SY(4,CUR)), name("testcontract"));
   BOOST_REQUIRE_EQUAL(asset::from_string("999900.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(name("testcontract"), symbol(SY(4,CUR)), "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("99.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(name("testcontract"), symbol(SY(4,CUR)), "tester2"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("1.0000 CUR"), liquid_balance);

   // can't delete auth because it's linked
   
   BOOST_REQUIRE_EXCEPTION(
   delete_authority(tester_account, name("first"), {permission_level{tester_account, name("active")}}, {get_private_key(tester_account, "active")}),
   action_validate_exception,
   [] (const action_validate_exception &e)->bool {
      expect_assert_message(e, "action_validate_exception: message validation exception\nCannot delete a linked authority");
      return true;
   });

   // unlink auth
   unlink_authority(name("tester"), name("testcontract"), name("transfer"));

   // delete auth
   delete_authority(tester_account, name("first"), {permission_level{tester_account, name("active")}}, {get_private_key(tester_account, "active")});
   produce_blocks(1);;

   trace = push_action(name("testcontract"), name("transfer"), "tester"_n, fc::mutable_variant_object()
       ("from", "tester")
       ("to", "tester2")
       ("quantity", "3.0000 CUR")
       ("memo", "hi" )
   );
   BOOST_REQUIRE_EQUAL(transaction_receipt::executed, trace->receipt->status);

   produce_blocks();

   liquid_balance = get_currency_balance(name("testcontract"), symbol(SY(4,CUR)), "tester"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("96.0000 CUR"), liquid_balance);
   liquid_balance = get_currency_balance(name("testcontract"), symbol(SY(4,CUR)), "tester2"_n);
   BOOST_REQUIRE_EQUAL(asset::from_string("4.0000 CUR"), liquid_balance);
}

BOOST_FIXTURE_TEST_CASE(setcode_test, slim_account_tester)
{
   try
   {
      create_slim_account(name("testcontract"));
      auto account_metadata = control->db().find<account_metadata_object, by_name>(name("testcontract"));
      BOOST_TEST(account_metadata == nullptr);
      set_code(name("testcontract"), test_contracts::eosio_token_wasm());
      digest_type first_code_hash;
      {
         auto account_metadata = control->db().find<account_metadata_object, by_name>(name("testcontract"));
         BOOST_TEST(account_metadata != nullptr);
         BOOST_TEST(account_metadata->name == name("testcontract"));
         BOOST_TEST(account_metadata->code_sequence == 1u);
         BOOST_TEST(account_metadata->abi_sequence == 0u);
         BOOST_TEST(account_metadata->code_hash != digest_type());
         BOOST_TEST(account_metadata->flags == 0u);
         BOOST_TEST(account_metadata->vm_type == 0u);
         BOOST_TEST(account_metadata->vm_version == 0u);
         BOOST_TEST(account_metadata->abi.size() == 0u);
         first_code_hash = account_metadata->code_hash;
      }
      produce_blocks();
      // Deploy the same code, should fail
      BOOST_CHECK_THROW(set_code(name("testcontract"), test_contracts::eosio_token_wasm()),
                        set_exact_code);

      // Deploy a different code
      set_code(name("testcontract"), test_contracts::eosio_msig_wasm());
      {
         auto account_metadata = control->db().find<account_metadata_object, by_name>(name("testcontract"));
         BOOST_TEST(account_metadata != nullptr);
         BOOST_TEST(account_metadata->name == name("testcontract"));
         BOOST_TEST(account_metadata->code_sequence == 2u);
         BOOST_TEST(account_metadata->abi_sequence == 0u);
         BOOST_TEST(account_metadata->code_hash != digest_type());
         BOOST_TEST(account_metadata->code_hash != first_code_hash);
         BOOST_TEST(account_metadata->flags == 0u);
         BOOST_TEST(account_metadata->vm_type == 0u);
         BOOST_TEST(account_metadata->vm_version == 0u);
         BOOST_TEST(account_metadata->abi.size() == 0u);
      }
   }
   FC_LOG_AND_RETHROW()
}

BOOST_FIXTURE_TEST_CASE(setabi_test, slim_account_tester)
{
   try
   {
      create_slim_account(name("testcontract"));
      auto account_metadata = control->db().find<account_metadata_object, by_name>(name("testcontract"));
      BOOST_TEST(account_metadata == nullptr);
      set_abi(name("testcontract"), test_contracts::eosio_token_abi());
      shared_blob first_abi;
      {
         auto account_metadata = control->db().find<account_metadata_object, by_name>(name("testcontract"));
         BOOST_TEST(account_metadata != nullptr);
         BOOST_TEST(account_metadata->name == name("testcontract"));
         BOOST_TEST(account_metadata->code_sequence == 0u);
         BOOST_TEST(account_metadata->abi_sequence == 1u);
         BOOST_TEST(account_metadata->code_hash == digest_type());
         BOOST_TEST(account_metadata->flags == 0u);
         BOOST_TEST(account_metadata->vm_type == 0u);
         BOOST_TEST(account_metadata->vm_version == 0u);
         BOOST_TEST(account_metadata->abi.size() != 0u);
         first_abi = account_metadata->abi;
      }
      produce_blocks();
      // Deploy the same abi, should pass
      set_abi(name("testcontract"), test_contracts::eosio_token_abi());

      // Deploy a different abi
      set_abi(name("testcontract"), test_contracts::eosio_msig_abi());
      {
         auto account_metadata = control->db().find<account_metadata_object, by_name>(name("testcontract"));
         BOOST_TEST(account_metadata != nullptr);
         BOOST_TEST(account_metadata->name == name("testcontract"));
         BOOST_TEST(account_metadata->code_sequence == 0u);
         BOOST_TEST(account_metadata->abi_sequence == 3u);
         BOOST_TEST(account_metadata->code_hash == digest_type());
         BOOST_TEST(account_metadata->flags == 0u);
         BOOST_TEST(account_metadata->vm_type == 0u);
         BOOST_TEST(account_metadata->vm_version == 0u);
         BOOST_TEST(account_metadata->abi.size() != 0u);
         BOOST_TEST(account_metadata->abi != first_abi);
      }
   }
   FC_LOG_AND_RETHROW()
}

BOOST_AUTO_TEST_SUITE_END()
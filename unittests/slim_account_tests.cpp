#include <eosio/chain/controller.hpp>
#include <eosio/chain/global_property_object.hpp>
#include <eosio/chain/permission_object.hpp>
#include <eosio/chain/resource_limits.hpp>
#include <eosio/chain/transaction.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/iostreams/device/mapped_file.hpp>
#include <eosio/testing/tester.hpp>
#include <eosio/chain/exceptions.hpp>     /* config_parse_error */
#include <test_contracts.hpp>

#include "fork_test_utilities.hpp"
#include "test_cfd_transaction.hpp"

using namespace eosio;
using namespace eosio::chain;
using namespace eosio::testing;

BOOST_AUTO_TEST_SUITE(slim_account_tests)

BOOST_AUTO_TEST_CASE(create_slim_account)
try
{
   tester chain(setup_policy::full);
   chain.create_slim_account({"slimacc"_n});
   const auto& slim_accnt = chain.control->db().get<account_object,by_name>( "slimacc"_n );
   BOOST_TEST(slim_accnt.name == "slimacc"_n);

   auto account_metadata_itr = chain.control->db().find<account_metadata_object,by_name>( "slimacc"_n );
   BOOST_TEST(account_metadata_itr == nullptr);
   auto owner_permission_itr = chain.control->db().find<permission_object, by_owner>(boost::make_tuple(name("slimacc"), name("owner")));
   BOOST_TEST(owner_permission_itr == nullptr);
   auto active_permission_itr = chain.control->db().find<permission_object, by_owner>(boost::make_tuple(name("slimacc"), name("active")));
   BOOST_TEST(active_permission_itr != nullptr);
   BOOST_TEST(active_permission_itr->owner == "slimacc"_n);
}
FC_LOG_AND_RETHROW()


BOOST_AUTO_TEST_CASE(set_contract_with_slim_account)
try
{
   tester chain(setup_policy::full);
   chain.create_slim_account({"slimacc"_n});
   chain.produce_blocks();

   chain.set_code("slimacc"_n, test_contracts::eosio_token_wasm());
   chain.set_abi("slimacc"_n, test_contracts::eosio_token_abi());

   const auto& slim_accnt = chain.control->db().get<account_object,by_name>( "slimacc"_n );
   BOOST_TEST(slim_accnt.abi.size() != size_t(0));

   auto account_metadata_itr = chain.control->db().find<account_metadata_object,by_name>( "slimacc"_n );
   BOOST_TEST(account_metadata_itr != nullptr);
   BOOST_TEST(account_metadata_itr->code_hash != digest_type());
}
FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_SUITE_END()

#include <eosio/chain/controller.hpp>
#include <eosio/chain/global_property_object.hpp>
#include <eosio/chain/permission_object.hpp>
#include <eosio/chain/resource_limits.hpp>
#include <eosio/chain/transaction.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/iostreams/device/mapped_file.hpp>
#include <eosio/testing/tester.hpp>
#include <eosio/chain/exceptions.hpp>     /* config_parse_error */

#include "fork_test_utilities.hpp"
#include "test_cfd_transaction.hpp"

using namespace eosio;
using namespace eosio::chain;
using namespace eosio::testing;

BOOST_AUTO_TEST_SUITE(slim_account_tests)

BOOST_AUTO_TEST_CASE(create_native_account)
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

BOOST_AUTO_TEST_SUITE_END()

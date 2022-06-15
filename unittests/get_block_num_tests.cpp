#include <eosio/chain/abi_serializer.hpp>
#include <eosio/chain/resource_limits.hpp>
#include <eosio/chain/generated_transaction_object.hpp>
#include <eosio/testing/tester.hpp>

#include <Runtime/Runtime.h>

#include <fc/variant_object.hpp>

#include <boost/test/unit_test.hpp>

#include <contracts.hpp>

#include "fork_test_utilities.hpp"

using namespace eosio::chain;
using namespace eosio::testing;
using namespace std::literals;

BOOST_AUTO_TEST_SUITE(get_block_num_tests)

BOOST_AUTO_TEST_CASE( get_block_num ) { try {
   tester c( setup_policy::preactivate_feature_and_new_bios );

   const auto& tester1_account = account_name("tester1");
   c.create_accounts( {tester1_account} );
   c.produce_block();

   const auto& pfm = c.control->get_protocol_feature_manager();
   const auto& d = pfm.get_builtin_digest( builtin_protocol_feature_t::get_block_num );
   BOOST_REQUIRE( d );

   c.preactivate_protocol_features( {*d} );
   c.produce_block();

   c.set_code( tester1_account, contracts::get_block_num_test_wasm() );
   c.set_abi( tester1_account, contracts::get_block_num_test_abi().data() );
   c.produce_block();

   c.push_action( tester1_account, "testblock"_n, tester1_account, mutable_variant_object()
      ("expected_result", c.control->head_block_num()+1)
   );

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()

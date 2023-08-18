#include <boost/test/unit_test.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include <eosio/testing/tester.hpp>
#include <eosio/chain/abi_serializer.hpp>
#include <eosio/chain/wasm_eosio_constraints.hpp>
#include <eosio/chain/resource_limits.hpp>
#include <eosio/chain/exceptions.hpp>
#include <eosio/chain/wast_to_wasm.hpp>
#include <eosio/chain_plugin/chain_plugin.hpp>
#include <eosio/hotstuff/chain_pacemaker.hpp>

#include <test_contracts.hpp>

#include <fc/io/fstream.hpp>

#include <fc/variant_object.hpp>
#include <fc/io/json.hpp>

#include <array>
#include <utility>

using namespace eosio;
using namespace eosio::chain;
using namespace eosio::testing;
using namespace fc;

static auto get_table_rows_full = [](chain_apis::read_only& plugin,
                                     chain_apis::read_only::get_table_rows_params& params,
                                     const fc::time_point& deadline) -> chain_apis::read_only::get_table_rows_result {   
   auto res_nm_v =  plugin.get_table_rows(params, deadline)();
   BOOST_REQUIRE(!std::holds_alternative<fc::exception_ptr>(res_nm_v));
   return std::get<chain_apis::read_only::get_table_rows_result>(std::move(res_nm_v));
};
   

BOOST_AUTO_TEST_SUITE(get_table_seckey_tests)

BOOST_FIXTURE_TEST_CASE( get_table_next_key_test, validating_tester ) try {
   create_account("test"_n);

   // setup contract and abi
   set_code( "test"_n, test_contracts::get_table_seckey_test_wasm() );
   set_abi( "test"_n, test_contracts::get_table_seckey_test_abi() );
   produce_block();

   chain_apis::read_only plugin(*(this->control), {}, {}, fc::microseconds::maximum(), fc::microseconds::maximum(), {});
   chain_apis::read_only::get_table_rows_params params = []{
      chain_apis::read_only::get_table_rows_params params{};
      params.json=true;
      params.code="test"_n;
      params.scope="test";
      params.limit=1;
      return params;
   }();

   params.table = "numobjs"_n;


   // name secondary key type
   push_action("test"_n, "addnumobj"_n, "test"_n, mutable_variant_object()("input", 2)("nm", "a"));
   push_action("test"_n, "addnumobj"_n, "test"_n, mutable_variant_object()("input", 5)("nm", "b"));
   push_action("test"_n, "addnumobj"_n, "test"_n, mutable_variant_object()("input", 7)("nm", "c"));

   params.table = "numobjs"_n;
   params.key_type = "name";
   params.limit = 10;
   params.index_position = "6";
   params.lower_bound = "a";
   params.upper_bound = "a";
   
   auto res_nm = get_table_rows_full(plugin, params, fc::time_point::maximum());
   
   BOOST_REQUIRE(res_nm.rows.size() == 1);

   params.lower_bound = "a";
   params.upper_bound = "b";
   res_nm = get_table_rows_full(plugin, params, fc::time_point::maximum());
   BOOST_REQUIRE(res_nm.rows.size() == 2);

   params.lower_bound = "a";
   params.upper_bound = "c";
   res_nm = get_table_rows_full(plugin, params, fc::time_point::maximum());
   BOOST_REQUIRE(res_nm.rows.size() == 3);

   push_action("test"_n, "addnumobj"_n, "test"_n, mutable_variant_object()("input", 8)("nm", "1111"));
   push_action("test"_n, "addnumobj"_n, "test"_n, mutable_variant_object()("input", 9)("nm", "2222"));
   push_action("test"_n, "addnumobj"_n, "test"_n, mutable_variant_object()("input", 10)("nm", "3333"));

   params.lower_bound = "1111";
   params.upper_bound = "3333";
   res_nm = get_table_rows_full(plugin, params, fc::time_point::maximum());
   BOOST_REQUIRE(res_nm.rows.size() == 3);

   params.lower_bound = "2222";
   params.upper_bound = "3333";
   res_nm = get_table_rows_full(plugin, params, fc::time_point::maximum());
   BOOST_REQUIRE(res_nm.rows.size() == 2);

} FC_LOG_AND_RETHROW() /// get_table_next_key_test

BOOST_AUTO_TEST_SUITE_END()

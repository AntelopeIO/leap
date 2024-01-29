#include <boost/test/unit_test.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include <eosio/testing/tester.hpp>
#include <eosio/chain/abi_serializer.hpp>
#include <eosio/chain/wasm_eosio_constraints.hpp>
#include <eosio/chain/resource_limits.hpp>
#include <eosio/chain/exceptions.hpp>
#include <eosio/chain/wast_to_wasm.hpp>
#include <eosio/chain/global_property_object.hpp>
#include <eosio/chain_plugin/chain_plugin.hpp>

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

static auto get_account_full = [](chain_apis::read_only& plugin,
                                  chain_apis::read_only::get_account_params& params,
                                  const fc::time_point& deadline) -> chain_apis::read_only::get_account_results {   
   auto res =  plugin.get_account(params, deadline)();
   BOOST_REQUIRE(!std::holds_alternative<fc::exception_ptr>(res));
   return std::get<chain_apis::read_only::get_account_results>(std::move(res));
};

BOOST_AUTO_TEST_SUITE(chain_plugin_tests)

BOOST_FIXTURE_TEST_CASE( get_block_with_invalid_abi, validating_tester ) try {
   produce_blocks(2);

   create_accounts( {"asserter"_n} );
   produce_block();

   // setup contract and abi
   set_code( "asserter"_n, test_contracts::asserter_wasm() );
   set_abi( "asserter"_n, test_contracts::asserter_abi() );
   produce_blocks(1);

   auto resolver = [&,this]( const account_name& name ) -> std::optional<abi_serializer> {
      try {
         const auto& accnt  = this->control->db().get<account_object,by_name>( name );
         if (abi_def abi; abi_serializer::to_abi(accnt.abi, abi)) {
            return abi_serializer(std::move(abi), abi_serializer::create_yield_function( abi_serializer_max_time ));
         }
         return std::optional<abi_serializer>();
      } FC_RETHROW_EXCEPTIONS(error, "resolver failed at chain_plugin_tests::abi_invalid_type");
   };

   // abi should be resolved
   BOOST_REQUIRE_EQUAL(true, resolver("asserter"_n).has_value());

   // make an action using the valid contract & abi
   fc::variant pretty_trx = mutable_variant_object()
      ("actions", variants({
         mutable_variant_object()
            ("account", "asserter")
            ("name", "procassert")
            ("authorization", variants({
               mutable_variant_object()
                  ("actor", "asserter")
                  ("permission", name(config::active_name).to_string())
            }))
            ("data", mutable_variant_object()
               ("condition", 1)
               ("message", "Should Not Assert!")
            )
         })
      );
   signed_transaction trx;
   abi_serializer::from_variant(pretty_trx, trx, resolver, abi_serializer::create_yield_function( abi_serializer_max_time ));
   set_transaction_headers(trx);
   trx.sign( get_private_key( "asserter"_n, "active" ), control->get_chain_id() );
   push_transaction( trx );
   produce_blocks(1);

   // retrieve block num
   uint32_t headnum = this->control->head_block_num();

   char headnumstr[20];
   sprintf(headnumstr, "%d", headnum);
   chain_apis::read_only::get_raw_block_params param{headnumstr};
   chain_apis::read_only plugin(*(this->control), {}, fc::microseconds::maximum(), fc::microseconds::maximum(), {});

   // block should be decoded successfully
   auto block = plugin.get_raw_block(param, fc::time_point::maximum());
   auto abi_cache = plugin.get_block_serializers(block, fc::microseconds::maximum());
   std::string block_str = json::to_pretty_string(plugin.convert_block(block, abi_cache));
   BOOST_TEST(block_str.find("procassert") != std::string::npos);
   BOOST_TEST(block_str.find("condition") != std::string::npos);
   BOOST_TEST(block_str.find("Should Not Assert!") != std::string::npos);
   BOOST_TEST(block_str.find("011253686f756c64204e6f742041737365727421") != std::string::npos); //action data

   // set an invalid abi (int8->xxxx)
   std::string abi2 = test_contracts::asserter_abi();
   auto pos = abi2.find("int8");
   BOOST_TEST(pos != std::string::npos);
   abi2.replace(pos, 4, "xxxx");
   set_abi("asserter"_n, abi2.c_str());
   produce_blocks(1);

   // resolving the invalid abi result in exception
   BOOST_CHECK_THROW(resolver("asserter"_n), invalid_type_inside_abi);

   // get the same block as string, results in decode failed(invalid abi) but not exception
   auto block2 = plugin.get_raw_block(param, fc::time_point::maximum());
   auto abi_cache2 = plugin.get_block_serializers(block2, fc::microseconds::maximum());
   std::string block_str2 = json::to_pretty_string(plugin.convert_block(block2, abi_cache2));
   BOOST_TEST(block_str2.find("procassert") != std::string::npos);
   BOOST_TEST(block_str2.find("condition") == std::string::npos); // decode failed
   BOOST_TEST(block_str2.find("Should Not Assert!") == std::string::npos); // decode failed
   BOOST_TEST(block_str2.find("011253686f756c64204e6f742041737365727421") != std::string::npos); //action data


   chain_apis::read_only::get_block_header_params bh_param{headnumstr, false};
   auto get_bh_result = plugin.get_block_header(bh_param, fc::time_point::maximum());

   BOOST_TEST(get_bh_result.id == block->calculate_id());
   BOOST_TEST(json::to_string(get_bh_result.signed_block_header, fc::time_point::maximum()) ==
              json::to_string(fc::variant{static_cast<signed_block_header&>(*block)}, fc::time_point::maximum()));

} FC_LOG_AND_RETHROW() /// get_block_with_invalid_abi

BOOST_AUTO_TEST_CASE( get_consensus_parameters ) try {
   tester t{setup_policy::old_wasm_parser};
   t.produce_blocks(1);

   chain_apis::read_only plugin(*(t.control), {}, fc::microseconds::maximum(), fc::microseconds::maximum(), nullptr);

   auto parms = plugin.get_consensus_parameters({}, fc::time_point::maximum());

   // verifying chain_config
   BOOST_TEST(parms.chain_config.max_block_cpu_usage == t.control->get_global_properties().configuration.max_block_cpu_usage);
   BOOST_TEST(parms.chain_config.target_block_net_usage_pct == t.control->get_global_properties().configuration.target_block_net_usage_pct);
   BOOST_TEST(parms.chain_config.max_transaction_net_usage == t.control->get_global_properties().configuration.max_transaction_net_usage);
   BOOST_TEST(parms.chain_config.base_per_transaction_net_usage == t.control->get_global_properties().configuration.base_per_transaction_net_usage);
   BOOST_TEST(parms.chain_config.net_usage_leeway == t.control->get_global_properties().configuration.net_usage_leeway);
   BOOST_TEST(parms.chain_config.context_free_discount_net_usage_num == t.control->get_global_properties().configuration.context_free_discount_net_usage_num);
   BOOST_TEST(parms.chain_config.context_free_discount_net_usage_den == t.control->get_global_properties().configuration.context_free_discount_net_usage_den);
   BOOST_TEST(parms.chain_config.max_block_cpu_usage == t.control->get_global_properties().configuration.max_block_cpu_usage);
   BOOST_TEST(parms.chain_config.target_block_cpu_usage_pct == t.control->get_global_properties().configuration.target_block_cpu_usage_pct);
   BOOST_TEST(parms.chain_config.max_transaction_cpu_usage == t.control->get_global_properties().configuration.max_transaction_cpu_usage);
   BOOST_TEST(parms.chain_config.min_transaction_cpu_usage == t.control->get_global_properties().configuration.min_transaction_cpu_usage);
   BOOST_TEST(parms.chain_config.max_transaction_lifetime == t.control->get_global_properties().configuration.max_transaction_lifetime);
   BOOST_TEST(parms.chain_config.deferred_trx_expiration_window == t.control->get_global_properties().configuration.deferred_trx_expiration_window);
   BOOST_TEST(parms.chain_config.max_transaction_delay == t.control->get_global_properties().configuration.max_transaction_delay);
   BOOST_TEST(parms.chain_config.max_inline_action_size == t.control->get_global_properties().configuration.max_inline_action_size);
   BOOST_TEST(parms.chain_config.max_inline_action_depth == t.control->get_global_properties().configuration.max_inline_action_depth);
   BOOST_TEST(parms.chain_config.max_authority_depth == t.control->get_global_properties().configuration.max_authority_depth);
   BOOST_TEST(parms.chain_config.max_action_return_value_size == t.control->get_global_properties().configuration.max_action_return_value_size);

   BOOST_TEST(!parms.wasm_config);

   t.preactivate_all_builtin_protocol_features();
   t.produce_block();

   parms = plugin.get_consensus_parameters({}, fc::time_point::maximum());

   BOOST_REQUIRE(!!parms.wasm_config);

   // verifying wasm_config
   BOOST_TEST(parms.wasm_config->max_mutable_global_bytes == t.control->get_global_properties().wasm_configuration.max_mutable_global_bytes);
   BOOST_TEST(parms.wasm_config->max_table_elements == t.control->get_global_properties().wasm_configuration.max_table_elements);
   BOOST_TEST(parms.wasm_config->max_section_elements == t.control->get_global_properties().wasm_configuration.max_section_elements);
   BOOST_TEST(parms.wasm_config->max_linear_memory_init == t.control->get_global_properties().wasm_configuration.max_linear_memory_init);
   BOOST_TEST(parms.wasm_config->max_func_local_bytes == t.control->get_global_properties().wasm_configuration.max_func_local_bytes);
   BOOST_TEST(parms.wasm_config->max_nested_structures == t.control->get_global_properties().wasm_configuration.max_nested_structures);
   BOOST_TEST(parms.wasm_config->max_symbol_bytes == t.control->get_global_properties().wasm_configuration.max_symbol_bytes);
   BOOST_TEST(parms.wasm_config->max_module_bytes == t.control->get_global_properties().wasm_configuration.max_module_bytes);
   BOOST_TEST(parms.wasm_config->max_code_bytes == t.control->get_global_properties().wasm_configuration.max_code_bytes);
   BOOST_TEST(parms.wasm_config->max_pages == t.control->get_global_properties().wasm_configuration.max_pages);
   BOOST_TEST(parms.wasm_config->max_call_depth == t.control->get_global_properties().wasm_configuration.max_call_depth);

} FC_LOG_AND_RETHROW() //get_consensus_parameters

BOOST_FIXTURE_TEST_CASE( get_account, validating_tester ) try {
   produce_blocks(2);

   std::vector<account_name> accs{{ "alice"_n, "bob"_n, "cindy"_n}};
   create_accounts(accs, false, false);

   produce_block();

   chain_apis::read_only plugin(*(this->control), {}, fc::microseconds::maximum(), fc::microseconds::maximum(), nullptr);

   chain_apis::read_only::get_account_params p{"alice"_n};

   chain_apis::read_only::get_account_results result = get_account_full(plugin, p, fc::time_point::maximum());

   auto check_result_basic = [](chain_apis::read_only::get_account_results result, eosio::name nm, bool isPriv) {
      BOOST_REQUIRE_EQUAL(nm, result.account_name);
      BOOST_REQUIRE_EQUAL(isPriv, result.privileged);

      BOOST_REQUIRE_EQUAL(2u, result.permissions.size());
      if (result.permissions.size() > 1u) {
         auto perm = result.permissions[0];
         BOOST_REQUIRE_EQUAL(name("active"_n), perm.perm_name);
         BOOST_REQUIRE_EQUAL(name("owner"_n), perm.parent);
         auto auth = perm.required_auth;
         BOOST_REQUIRE_EQUAL(1u, auth.threshold);
         BOOST_REQUIRE_EQUAL(1u, auth.keys.size());
         BOOST_REQUIRE_EQUAL(0u, auth.accounts.size());
         BOOST_REQUIRE_EQUAL(0u, auth.waits.size());

         perm = result.permissions[1];
         BOOST_REQUIRE_EQUAL(name("owner"_n), perm.perm_name);
         BOOST_REQUIRE_EQUAL(name(""_n), perm.parent);
         auth = perm.required_auth;
         BOOST_REQUIRE_EQUAL(1u, auth.threshold);
         BOOST_REQUIRE_EQUAL(1u, auth.keys.size());
         BOOST_REQUIRE_EQUAL(0u, auth.accounts.size());
         BOOST_REQUIRE_EQUAL(0u, auth.waits.size());
      }
   };

   check_result_basic(result, name("alice"_n), false);

   for (auto perm : result.permissions) {
      BOOST_REQUIRE_EQUAL(true, perm.linked_actions.has_value());
      if (perm.linked_actions.has_value())
         BOOST_REQUIRE_EQUAL(0u, perm.linked_actions->size());
   }
   BOOST_REQUIRE_EQUAL(0u, result.eosio_any_linked_actions.size());

   // test link authority
   link_authority(name("alice"_n), name("bob"_n), name("active"_n), name("foo"_n));
   produce_block();
   result = get_account_full(plugin, p, fc::time_point::maximum());

   check_result_basic(result, name("alice"_n), false);
   auto perm = result.permissions[0];
   BOOST_REQUIRE_EQUAL(1u, perm.linked_actions->size());
   if (perm.linked_actions->size() >= 1u) {
      auto la = (*perm.linked_actions)[0];
      BOOST_REQUIRE_EQUAL(name("bob"_n), la.account);
      BOOST_REQUIRE_EQUAL(true, la.action.has_value());
      if(la.action.has_value()) {
         BOOST_REQUIRE_EQUAL(name("foo"_n), la.action.value());
      }
   }
   BOOST_REQUIRE_EQUAL(0u, result.eosio_any_linked_actions.size());

   // test link authority to eosio.any
   link_authority(name("alice"_n), name("bob"_n), name("eosio.any"_n), name("foo"_n));
   produce_block();
   result = get_account_full(plugin, p, fc::time_point::maximum());
   check_result_basic(result, name("alice"_n), false);
   // active permission should no longer have linked auth, as eosio.any replaces it
   perm = result.permissions[0];
   BOOST_REQUIRE_EQUAL(0u, perm.linked_actions->size());

   auto eosio_any_la = result.eosio_any_linked_actions;
   BOOST_REQUIRE_EQUAL(1u, eosio_any_la.size());
   if (eosio_any_la.size() >= 1u) {
      auto la = eosio_any_la[0];
      BOOST_REQUIRE_EQUAL(name("bob"_n), la.account);
      BOOST_REQUIRE_EQUAL(true, la.action.has_value());
      if(la.action.has_value()) {
         BOOST_REQUIRE_EQUAL(name("foo"_n), la.action.value());
      }
   }
} FC_LOG_AND_RETHROW() /// get_account

BOOST_AUTO_TEST_SUITE_END()

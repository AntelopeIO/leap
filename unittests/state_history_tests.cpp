#include <eosio/state_history/serialization.hpp>
#include <eosio/chain/authorization_manager.hpp>
#include <boost/test/unit_test.hpp>
#include <contracts.hpp>
#include <test_contracts.hpp>
#include <eosio/state_history/create_deltas.hpp>
#include <eosio/state_history/log.hpp>
#include <eosio/state_history/trace_converter.hpp>
#include <eosio/testing/tester.hpp>
#include <fc/io/json.hpp>
#include <eosio/chain/global_property_object.hpp>

#include "test_cfd_transaction.hpp"
#include <boost/filesystem.hpp>

#include <eosio/stream.hpp>
#include <eosio/ship_protocol.hpp>
#include <boost/iostreams/device/back_inserter.hpp>
#include <boost/iostreams/copy.hpp>

using namespace eosio::chain;
using namespace eosio::testing;
using namespace std::literals;

extern const char* const state_history_plugin_abi;

bool operator==(const eosio::checksum256& lhs, const transaction_id_type& rhs) {
   return memcmp(lhs.extract_as_byte_array().data(), rhs.data(), rhs.data_size()) == 0;
}

namespace eosio::state_history {

template <typename ST, typename T>
datastream<ST>& operator>>(datastream<ST>& ds, eosio::state_history::big_vector_wrapper<T>& obj) {
   fc::unsigned_int sz;
   fc::raw::unpack(ds, sz);
   obj.obj.resize(sz);
   for (auto& x : obj.obj)
      fc::raw::unpack(ds, x);
   return ds;
}

std::vector<table_delta> create_deltas(const chainbase::database& db, bool full_snapshot) {
   namespace bio = boost::iostreams;
   std::vector<char> buf;
   bio::filtering_ostreambuf obuf;
   obuf.push(bio::back_inserter(buf));
   pack_deltas(obuf, db, full_snapshot);

   fc::datastream<const char*> is{buf.data(), buf.size()};
   std::vector<table_delta> result;
   fc::raw::unpack(is, result);
   return result;
}
}

BOOST_AUTO_TEST_SUITE(test_state_history)

class table_deltas_tester : public tester {
public:
   using tester::tester;
   using deltas_vector = vector<eosio::state_history::table_delta>;

   pair<bool, deltas_vector::iterator> find_table_delta(const std::string &name, bool full_snapshot = false) {
      v = eosio::state_history::create_deltas(control->db(), full_snapshot);;

      auto find_by_name = [&name](const auto& x) {
         return x.name == name;
      };

      auto it = std::find_if(v.begin(), v.end(), find_by_name);

      return make_pair(it != v.end(), it);
   }

   template <typename A, typename B>
   vector<A> deserialize_data(deltas_vector::iterator &it) {
      vector<A> result;
      for(size_t i=0; i < it->rows.obj.size(); i++) {
         eosio::input_stream stream{it->rows.obj[i].second.data(), it->rows.obj[i].second.size()};
         result.push_back(std::get<A>(eosio::from_bin<B>(stream)));
      }
      return result;
   }

private:
   deltas_vector v;
};

BOOST_AUTO_TEST_CASE(test_deltas_not_empty) {
   table_deltas_tester chain;

   auto deltas = eosio::state_history::create_deltas(chain.control->db(), false);

   for(const auto &delta: deltas) {
      BOOST_REQUIRE(!delta.rows.obj.empty());
   }
}

BOOST_AUTO_TEST_CASE(test_deltas_account_creation) {
   table_deltas_tester chain;
   chain.produce_block();

   // Check that no account table deltas are present
   BOOST_REQUIRE_EQUAL(chain.find_table_delta("account").first, false);

   // Create new account
   chain.create_account("newacc"_n);

   // Verify that a new record for the new account in the state delta of the block
   auto result = chain.find_table_delta("account");
   BOOST_REQUIRE(result.first);
   auto &it_account = result.second;
   BOOST_REQUIRE_EQUAL(it_account->rows.obj.size(), 1);

   auto accounts = chain.deserialize_data<eosio::ship_protocol::account_v0, eosio::ship_protocol::account>(it_account);
   BOOST_REQUIRE_EQUAL(accounts[0].name.to_string(), "newacc");

}

BOOST_AUTO_TEST_CASE(test_deltas_account_metadata) {
   table_deltas_tester chain;
   chain.produce_block();

   chain.create_account("newacc"_n);

   // Spot onto account metadata
   auto result = chain.find_table_delta("account_metadata");
   BOOST_REQUIRE(result.first);
   auto &it_account_metadata = result.second;
   BOOST_REQUIRE_EQUAL(it_account_metadata->rows.obj.size(), 1);

   auto accounts_metadata = chain.deserialize_data<eosio::ship_protocol::account_metadata_v0, eosio::ship_protocol::account_metadata>(it_account_metadata);
   BOOST_REQUIRE_EQUAL(accounts_metadata[0].name.to_string(), "newacc");
   BOOST_REQUIRE_EQUAL(accounts_metadata[0].privileged, false);

}


BOOST_AUTO_TEST_CASE(test_deltas_account_permission) {
   table_deltas_tester chain;
   chain.produce_block();

   chain.create_account("newacc"_n);

   // Check that the permissions of this new account are in the delta
   vector<string> expected_permission_names{ "owner", "active" };
   auto result = chain.find_table_delta("permission");
   BOOST_REQUIRE(result.first);
   auto &it_permission = result.second;
   BOOST_REQUIRE_EQUAL(it_permission->rows.obj.size(), 2);
   auto accounts_permissions = chain.deserialize_data<eosio::ship_protocol::permission_v0, eosio::ship_protocol::permission>(it_permission);
   for(size_t i = 0; i < accounts_permissions.size(); i++)
   {
      BOOST_REQUIRE_EQUAL(it_permission->rows.obj[i].first, true);
      BOOST_REQUIRE_EQUAL(accounts_permissions[i].owner.to_string(), "newacc");
      BOOST_REQUIRE_EQUAL(accounts_permissions[i].name.to_string(), expected_permission_names[i]);
   }
}


BOOST_AUTO_TEST_CASE(test_deltas_account_permission_creation_and_deletion) {
   table_deltas_tester chain;
   chain.produce_block();

   chain.create_account("newacc"_n);

   auto& authorization_manager = chain.control->get_authorization_manager();
   const permission_object* ptr = authorization_manager.find_permission( {"newacc"_n, "active"_n} );
   BOOST_REQUIRE(ptr != nullptr);

   // Create new permission
   chain.set_authority("newacc"_n, "mypermission"_n, ptr->auth,  "active"_n);

   const permission_object* ptr_sub = authorization_manager.find_permission( {"newacc"_n, "mypermission"_n} );
   BOOST_REQUIRE(ptr_sub != nullptr);

   // Verify that the new permission is present in the state delta
   std::vector<std::string> expected_permission_names{ "owner", "active", "mypermission" };
   auto result = chain.find_table_delta("permission");
   BOOST_REQUIRE(result.first);
   auto &it_permission = result.second;
   BOOST_REQUIRE_EQUAL(it_permission->rows.obj.size(), 3);
   BOOST_REQUIRE_EQUAL(it_permission->rows.obj[2].first, true);
   auto accounts_permissions = chain.deserialize_data<eosio::ship_protocol::permission_v0, eosio::ship_protocol::permission>(it_permission);
   BOOST_REQUIRE_EQUAL(accounts_permissions[2].owner.to_string(), "newacc");
   BOOST_REQUIRE_EQUAL(accounts_permissions[2].name.to_string(), "mypermission");
   BOOST_REQUIRE_EQUAL(accounts_permissions[2].parent.to_string(), "active");

   chain.produce_block();

   // Delete the permission
   chain.delete_authority("newacc"_n, "mypermission"_n);

   result = chain.find_table_delta("permission");
   BOOST_REQUIRE(result.first);
   auto &it_permission_del = result.second;
   BOOST_REQUIRE_EQUAL(it_permission_del->rows.obj.size(), 1);
   BOOST_REQUIRE_EQUAL(it_permission_del->rows.obj[0].first, false);
   accounts_permissions = chain.deserialize_data<eosio::ship_protocol::permission_v0, eosio::ship_protocol::permission>(it_permission_del);
   BOOST_REQUIRE_EQUAL(accounts_permissions[0].owner.to_string(), "newacc");
   BOOST_REQUIRE_EQUAL(accounts_permissions[0].name.to_string(), "mypermission");
   BOOST_REQUIRE_EQUAL(accounts_permissions[0].parent.to_string(), "active");
}


BOOST_AUTO_TEST_CASE(test_deltas_account_permission_modification) {
   table_deltas_tester chain;
   chain.produce_block();

   chain.create_account("newacc"_n);
   chain.produce_block();
   public_key_type keys[] = {
         public_key_type("PUB_WA_WdCPfafVNxVMiW5ybdNs83oWjenQXvSt1F49fg9mv7qrCiRwHj5b38U3ponCFWxQTkDsMC"s), // Test for correct serialization of WA key, see issue #9087
         public_key_type("PUB_K1_12wkBET2rRgE8pahuaczxKbmv7ciehqsne57F9gtzf1PVb7Rf7o"s),
         public_key_type("PUB_R1_6FPFZqw5ahYrR9jD96yDbbDNTdKtNqRbze6oTDLntrsANgQKZu"s)};
   const int K1_storage_type_which_value = 0;

   for(auto &key: keys) {
      // Modify the permission authority
      auto wa_authority = authority(1, {key_weight{key, 1}}, {});
      chain.set_authority("newacc"_n, "active"_n, wa_authority, "owner"_n);

      auto result = chain.find_table_delta("permission");
      BOOST_REQUIRE(result.first);

      auto &it_permission = result.second;
      BOOST_REQUIRE_EQUAL(it_permission->rows.obj.size(), 1);
      auto accounts_permissions = chain.deserialize_data<eosio::ship_protocol::permission_v0, eosio::ship_protocol::permission>(it_permission);
      BOOST_REQUIRE_EQUAL(accounts_permissions[0].owner.to_string(), "newacc");
      BOOST_REQUIRE_EQUAL(accounts_permissions[0].name.to_string(), "active");
      BOOST_REQUIRE_EQUAL(accounts_permissions[0].auth.keys.size(), 1);
      if(key.which() != K1_storage_type_which_value)
         BOOST_REQUIRE_EQUAL(public_key_to_string(accounts_permissions[0].auth.keys[0].key), key.to_string());
      else
         BOOST_REQUIRE_EQUAL(public_key_to_string(accounts_permissions[0].auth.keys[0].key), "PUB_K1_12wkBET2rRgE8pahuaczxKbmv7ciehqsne57F9gtzf1PVb7Rf7o");

      chain.produce_block();
   }
}


BOOST_AUTO_TEST_CASE(test_deltas_permission_link) {
   table_deltas_tester chain;
   chain.produce_block();

   chain.create_account("newacc"_n);

   // Spot onto permission_link
   const auto spending_priv_key = chain.get_private_key("newacc"_n, "spending");
   const auto spending_pub_key = spending_priv_key.get_public_key();

   chain.set_authority("newacc"_n, "spending"_n, spending_pub_key, "active"_n);
   chain.link_authority("newacc"_n, "eosio"_n, "spending"_n, "reqauth"_n);
   chain.push_reqauth("newacc"_n, { permission_level{"newacc"_n, "spending"_n} }, { spending_priv_key });


   auto result = chain.find_table_delta("permission_link");
   BOOST_REQUIRE(result.first);
   auto &it_permission_link = result.second;
   BOOST_REQUIRE_EQUAL(it_permission_link->rows.obj.size(), 1);
   auto permission_links = chain.deserialize_data<eosio::ship_protocol::permission_link_v0, eosio::ship_protocol::permission_link>(it_permission_link);
   BOOST_REQUIRE_EQUAL(permission_links[0].account.to_string(), "newacc");
   BOOST_REQUIRE_EQUAL(permission_links[0].message_type.to_string(), "reqauth");
   BOOST_REQUIRE_EQUAL(permission_links[0].required_permission.to_string(), "spending");
}


BOOST_AUTO_TEST_CASE(test_deltas_global_property_history) {
   // Assuming max transaction delay is 45 days (default in config.hpp)
   table_deltas_tester chain;

   // Change max_transaction_delay to 60 sec
   auto params = chain.control->get_global_properties().configuration;
   params.max_transaction_delay = 60;
   chain.push_action( config::system_account_name, "setparams"_n, config::system_account_name,
                             mutable_variant_object()
                             ("params", params) );

   // Deserialize and spot onto some data
   auto result = chain.find_table_delta("global_property");
   BOOST_REQUIRE(result.first);
   auto &it_global_property = result.second;
   BOOST_REQUIRE_EQUAL(it_global_property->rows.obj.size(), 1);
   auto global_properties = chain.deserialize_data<eosio::ship_protocol::global_property_v1, eosio::ship_protocol::global_property>(it_global_property);
   auto configuration = std::get<eosio::ship_protocol::chain_config_v1>(global_properties[0].configuration);
   BOOST_REQUIRE_EQUAL(configuration.max_transaction_delay, 60);
}


BOOST_AUTO_TEST_CASE(test_deltas_protocol_feature_history) {
   table_deltas_tester chain(setup_policy::none);
   const auto &pfm = chain.control->get_protocol_feature_manager();

   chain.produce_block();

   auto d = pfm.get_builtin_digest(builtin_protocol_feature_t::preactivate_feature);
   BOOST_REQUIRE(d);

   // Activate PREACTIVATE_FEATURE.
   chain.schedule_protocol_features_wo_preactivation({*d});

   chain.produce_block();

   // Now the latest bios contract can be set.
   chain.set_before_producer_authority_bios_contract();

   // Spot onto some data of the protocol state table delta
   auto result = chain.find_table_delta("protocol_state");
   BOOST_REQUIRE(result.first);
   auto &it_protocol_state = result.second;
   BOOST_REQUIRE_EQUAL(it_protocol_state->rows.obj.size(), 1);
   auto protocol_states = chain.deserialize_data<eosio::ship_protocol::protocol_state_v0, eosio::ship_protocol::protocol_state>(it_protocol_state);
   auto protocol_feature = std::get<eosio::ship_protocol::activated_protocol_feature_v0>(protocol_states[0].activated_protocol_features[0]);

   auto digest_byte_array = protocol_feature.feature_digest.extract_as_byte_array();
   char digest_array[digest_byte_array.size()];
   for(size_t i=0; i < digest_byte_array.size(); i++) digest_array[i] = digest_byte_array[i];
   eosio::chain::digest_type digest_in_delta(digest_array, digest_byte_array.size());

   BOOST_REQUIRE_EQUAL(digest_in_delta, *d);
}


BOOST_AUTO_TEST_CASE(test_deltas_contract) {
   table_deltas_tester chain;
   chain.produce_block();

   chain.create_account("tester"_n);

   chain.set_code("tester"_n, test_contracts::get_table_test_wasm());
   chain.set_abi("tester"_n, test_contracts::get_table_test_abi().data());

   chain.produce_block();

   auto trace = chain.push_action("tester"_n, "addhashobj"_n, "tester"_n, mutable_variant_object()("hashinput", "hello" ));

   BOOST_REQUIRE_EQUAL(transaction_receipt::executed, trace->receipt->status);

   trace = chain.push_action("tester"_n, "addnumobj"_n, "tester"_n, mutable_variant_object()("input", 2));

   BOOST_REQUIRE_EQUAL(transaction_receipt::executed, trace->receipt->status);

   // Spot onto contract_table
   auto result = chain.find_table_delta("contract_table");
   BOOST_REQUIRE(result.first);
   auto &it_contract_table = result.second;
   BOOST_REQUIRE_EQUAL(it_contract_table->rows.obj.size(), 6);
   auto contract_tables = chain.deserialize_data<eosio::ship_protocol::contract_table_v0, eosio::ship_protocol::contract_table>(it_contract_table);
   BOOST_REQUIRE_EQUAL(contract_tables[0].table.to_string(), "hashobjs");
   BOOST_REQUIRE_EQUAL(contract_tables[1].table.to_string(), "hashobjs....1");
   BOOST_REQUIRE_EQUAL(contract_tables[2].table.to_string(), "numobjs");
   BOOST_REQUIRE_EQUAL(contract_tables[3].table.to_string(), "numobjs.....1");
   BOOST_REQUIRE_EQUAL(contract_tables[4].table.to_string(), "numobjs.....2");
   BOOST_REQUIRE_EQUAL(contract_tables[5].table.to_string(), "numobjs.....3");

   // Spot onto contract_row
   result = chain.find_table_delta("contract_row");
   BOOST_REQUIRE(result.first);
   auto &it_contract_row = result.second;
   BOOST_REQUIRE_EQUAL(it_contract_row->rows.obj.size(), 2);
   auto contract_rows = chain.deserialize_data<eosio::ship_protocol::contract_row_v0, eosio::ship_protocol::contract_row>(it_contract_row);
   BOOST_REQUIRE_EQUAL(contract_rows[0].table.to_string(), "hashobjs");
   BOOST_REQUIRE_EQUAL(contract_rows[1].table.to_string(), "numobjs");

   // Spot onto contract_index256
   result = chain.find_table_delta("contract_index256");
   BOOST_REQUIRE(result.first);
   auto &it_contract_index256 = result.second;
   BOOST_REQUIRE_EQUAL(it_contract_index256->rows.obj.size(), 2);
   auto contract_indices = chain.deserialize_data<eosio::ship_protocol::contract_index256_v0, eosio::ship_protocol::contract_index256>(it_contract_index256);
   BOOST_REQUIRE_EQUAL(contract_indices[0].table.to_string(), "hashobjs");
   BOOST_REQUIRE_EQUAL(contract_indices[1].table.to_string(), "hashobjs....1");
}


BOOST_AUTO_TEST_CASE(test_deltas_resources_history) {
   table_deltas_tester chain;
   chain.produce_block();

   chain.create_accounts({ "eosio.token"_n, "eosio.ram"_n, "eosio.ramfee"_n, "eosio.stake"_n, "eosio.rex"_n});

   chain.produce_blocks( 100 );

   chain.set_code( "eosio.token"_n, test_contracts::eosio_token_wasm() );
   chain.set_abi( "eosio.token"_n, test_contracts::eosio_token_abi().data() );

   chain.produce_block();

   chain.push_action("eosio.token"_n, "create"_n, "eosio.token"_n, mutable_variant_object()
      ("issuer", "eosio.token" )
      ("maximum_supply", core_from_string("1000000000.0000") )
   );

   chain.push_action("eosio.token"_n, "issue"_n, "eosio.token"_n, fc::mutable_variant_object()
      ("to",       "eosio")
      ("quantity", core_from_string("90.0000"))
      ("memo", "for stuff")
   );

   chain.produce_blocks(10);

   chain.set_code( config::system_account_name, test_contracts::eosio_system_wasm() );
   chain.set_abi( config::system_account_name, test_contracts::eosio_system_abi().data() );

   chain.push_action(config::system_account_name, "init"_n, config::system_account_name,
                        mutable_variant_object()
                        ("version", 0)
                        ("core", symbol(CORE_SYMBOL).to_string()));

   signed_transaction trx;
   chain.set_transaction_headers(trx);

   authority owner_auth;
   owner_auth =  authority( chain.get_public_key( "alice"_n, "owner" ) );

   trx.actions.emplace_back( vector<permission_level>{{config::system_account_name,config::active_name}},
                                newaccount{
                                    .creator  = config::system_account_name,
                                    .name     =  "alice"_n,
                                    .owner    = owner_auth,
                                    .active   = authority( chain.get_public_key( "alice"_n, "active" ) )});

   trx.actions.emplace_back( chain.get_action( config::system_account_name, "buyram"_n, vector<permission_level>{{config::system_account_name,config::active_name}},
                                                  mutable_variant_object()
                                                      ("payer", config::system_account_name)
                                                      ("receiver",  "alice"_n)
                                                      ("quant", core_from_string("1.0000"))));

   trx.actions.emplace_back( chain.get_action( config::system_account_name, "delegatebw"_n, vector<permission_level>{{config::system_account_name,config::active_name}},
                                                  mutable_variant_object()
                                                      ("from", config::system_account_name)
                                                      ("receiver",  "alice"_n)
                                                      ("stake_net_quantity", core_from_string("10.0000") )
                                                      ("stake_cpu_quantity", core_from_string("10.0000") )
                                                      ("transfer", 0 )));

   chain.set_transaction_headers(trx);
   trx.sign( chain.get_private_key( config::system_account_name, "active" ), chain.control->get_chain_id()  );
   chain.push_transaction( trx );
}

   BOOST_AUTO_TEST_CASE(test_deltas) {
      tester main;

      auto v = eosio::state_history::create_deltas(main.control->db(), false);

      std::string name="permission";
      auto find_by_name = [&name](const auto& x) {
         return x.name == name;
      };

      auto it = std::find_if(v.begin(), v.end(), find_by_name);
      BOOST_REQUIRE(it==v.end());

      name="resource_limits";
      it = std::find_if(v.begin(), v.end(), find_by_name);
      BOOST_REQUIRE(it==v.end());

      main.create_account("newacc"_n);

      v = eosio::state_history::create_deltas(main.control->db(), false);

      name="permission";
      it = std::find_if(v.begin(), v.end(), find_by_name);
      BOOST_REQUIRE(it!=v.end());

      name="resource_limits";
      it = std::find_if(v.begin(), v.end(), find_by_name);
      BOOST_REQUIRE(it!=v.end());

      main.produce_block();

      v = eosio::state_history::create_deltas(main.control->db(), false);

      name="permission";
      it = std::find_if(v.begin(), v.end(), find_by_name);
      BOOST_REQUIRE(it==v.end());

      name="resource_limits";
      it = std::find_if(v.begin(), v.end(), find_by_name);
      BOOST_REQUIRE(it==v.end());
   }

   BOOST_AUTO_TEST_CASE(test_deltas_contract_several_rows){
      table_deltas_tester chain(setup_policy::full);

      chain.produce_block();
      chain.create_account("tester"_n);

      chain.set_code("tester"_n, test_contracts::get_table_test_wasm());
      chain.set_abi("tester"_n, test_contracts::get_table_test_abi().data());

      chain.produce_blocks(2);

      auto trace = chain.push_action("tester"_n, "addhashobj"_n, "tester"_n, mutable_variant_object()("hashinput", "hello"));
      BOOST_REQUIRE_EQUAL(transaction_receipt::executed, trace->receipt->status);

      trace = chain.push_action("tester"_n, "addhashobj"_n, "tester"_n, mutable_variant_object()("hashinput", "world"));
      BOOST_REQUIRE_EQUAL(transaction_receipt::executed, trace->receipt->status);

      trace = chain.push_action("tester"_n, "addhashobj"_n, "tester"_n, mutable_variant_object()("hashinput", "!"));
      BOOST_REQUIRE_EQUAL(transaction_receipt::executed, trace->receipt->status);

      trace = chain.push_action("tester"_n, "addnumobj"_n, "tester"_n, mutable_variant_object()("input", 2));
      BOOST_REQUIRE_EQUAL(transaction_receipt::executed, trace->receipt->status);

      trace = chain.push_action("tester"_n, "addnumobj"_n, "tester"_n, mutable_variant_object()("input", 3));
      BOOST_REQUIRE_EQUAL(transaction_receipt::executed, trace->receipt->status);

      trace = chain.push_action("tester"_n, "addnumobj"_n, "tester"_n, mutable_variant_object()("input", 4));
      BOOST_REQUIRE_EQUAL(transaction_receipt::executed, trace->receipt->status);

      // Spot onto contract_row with full snapshot
      auto result = chain.find_table_delta("contract_row", true);
      BOOST_REQUIRE(result.first);
      auto &it_contract_row = result.second;
      BOOST_REQUIRE_EQUAL(it_contract_row->rows.obj.size(), 8);
      auto contract_rows = chain.deserialize_data<eosio::ship_protocol::contract_row_v0, eosio::ship_protocol::contract_row>(it_contract_row);

      std::multiset<std::string> expected_contract_row_table_names {"abihash", "abihash", "hashobjs", "hashobjs", "hashobjs", "numobjs", "numobjs", "numobjs"};

      std::multiset<uint64_t> expected_contract_row_table_primary_keys {6138663577826885632U,14605619288908759040U, 0, 1 ,2, 0, 1, 2};
      std::multiset<std::string> result_contract_row_table_names;
      std::multiset<uint64_t> result_contract_row_table_primary_keys;
      for(auto &contract_row : contract_rows) {
         result_contract_row_table_names.insert(contract_row.table.to_string());
         result_contract_row_table_primary_keys.insert(contract_row.primary_key);
      }
      BOOST_REQUIRE(expected_contract_row_table_names == result_contract_row_table_names);
      BOOST_REQUIRE(expected_contract_row_table_primary_keys == result_contract_row_table_primary_keys);

      chain.produce_block();

      trace = chain.push_action("tester"_n, "erasenumobj"_n, "tester"_n, mutable_variant_object()("id", 1));
      BOOST_REQUIRE_EQUAL(transaction_receipt::executed, trace->receipt->status);

      trace = chain.push_action("tester"_n, "erasenumobj"_n, "tester"_n, mutable_variant_object()("id", 0));
      BOOST_REQUIRE_EQUAL(transaction_receipt::executed, trace->receipt->status);

      result = chain.find_table_delta("contract_row");
      BOOST_REQUIRE(result.first);
      BOOST_REQUIRE_EQUAL(it_contract_row->rows.obj.size(), 2);
      contract_rows = chain.deserialize_data<eosio::ship_protocol::contract_row_v0, eosio::ship_protocol::contract_row>(it_contract_row);

      for(size_t i=0; i < contract_rows.size(); i++) {
         BOOST_REQUIRE_EQUAL(it_contract_row->rows.obj[i].first, 0);
         BOOST_REQUIRE_EQUAL(contract_rows[i].table.to_string(), "numobjs");
      }

      result = chain.find_table_delta("contract_index_double");
      BOOST_REQUIRE(result.first);
      auto &it_contract_index_double = result.second;
      BOOST_REQUIRE_EQUAL(it_contract_index_double->rows.obj.size(), 2);
      auto contract_index_double_elems = chain.deserialize_data<eosio::ship_protocol::contract_index_double_v0, eosio::ship_protocol::contract_index_double>(it_contract_index_double);

      for(size_t i=0; i < contract_index_double_elems.size(); i++) {
         BOOST_REQUIRE_EQUAL(it_contract_index_double->rows.obj[i].first, 0);
         BOOST_REQUIRE_EQUAL(contract_index_double_elems[i].table.to_string(), "numobjs.....2");
      }

   }

   std::vector<shared_ptr<eosio::state_history::partial_transaction>> get_partial_txns(eosio::state_history::trace_converter& log) {
      std::vector<shared_ptr<eosio::state_history::partial_transaction>> partial_txns;

      for (auto ct : log.cached_traces) {
         partial_txns.push_back(std::get<1>(ct).partial);
      }

      return partial_txns;
   }

   BOOST_AUTO_TEST_CASE(test_trace_log_with_transaction_extensions) {
      tester c(setup_policy::full);

      fc::temp_directory state_history_dir;
      eosio::state_history::trace_converter log;

      c.control->applied_transaction.connect(
            [&](std::tuple<const transaction_trace_ptr&, const packed_transaction_ptr&> t) {
               log.add_transaction(std::get<0>(t), std::get<1>(t));
            });

      c.create_accounts({"alice"_n, "test"_n});
      c.set_code("test"_n, test_contracts::deferred_test_wasm());
      c.set_abi("test"_n, test_contracts::deferred_test_abi().data());
      c.produce_block();

      c.push_action("test"_n, "defercall"_n, "alice"_n,
                    fc::mutable_variant_object()("payer", "alice")("sender_id", 1)("contract", "test")("payload", 40));

      auto block  = c.produce_block();
      auto partial_txns = get_partial_txns(log);

      auto contains_transaction_extensions = [](shared_ptr<eosio::state_history::partial_transaction> txn) {
         return txn->transaction_extensions.size() > 0;
      };

      BOOST_CHECK(std::any_of(partial_txns.begin(), partial_txns.end(), contains_transaction_extensions));
   }


struct state_history_tester_logs  {
   state_history_tester_logs(const std::filesystem::path& dir, const eosio::state_history_log_config& config)
      : traces_log("trace_history",dir, config) , chain_state_log("chain_state_history", dir, config) {}

   eosio::state_history_log traces_log;
   eosio::state_history_log chain_state_log;
   eosio::state_history::trace_converter trace_converter;
};

struct state_history_tester : state_history_tester_logs, tester {


   state_history_tester(const std::filesystem::path& dir, const eosio::state_history_log_config& config)
   : state_history_tester_logs(dir, config), tester ([this](eosio::chain::controller& control) {
      control.applied_transaction.connect(
       [&](std::tuple<const transaction_trace_ptr&, const packed_transaction_ptr&> t) {
          trace_converter.add_transaction(std::get<0>(t), std::get<1>(t));
       });

      control.accepted_block.connect([&](const block_state_ptr& block_state) {
         eosio::state_history_log_header header{.magic        = eosio::ship_magic(eosio::ship_current_version, 0),
                                      .block_id     = block_state->id,
                                      .payload_size = 0};

         traces_log.pack_and_write_entry(header, block_state->block->previous, [this, &control, &block_state](auto&& buf) {
            trace_converter.pack(buf, control.db(), false, block_state);
         });

         chain_state_log.pack_and_write_entry(header, block_state->header.previous, [&control](auto&& buf) {
            eosio::state_history::pack_deltas(buf, control.db(), true);
         });
      });
      control.block_start.connect([this](uint32_t block_num) {
         trace_converter.cached_traces.clear();
         trace_converter.onblock_trace.reset();
      });
   }) {}
};

static std::vector<char> get_decompressed_entry(eosio::state_history_log& log, block_num_type block_num) {
   auto result = log.create_locked_decompress_stream();
   log.get_unpacked_entry(block_num, result);
   namespace bio = boost::iostreams;
   return std::visit(eosio::chain::overloaded{ [](std::vector<char>& bytes) {
                                                 return bytes;
                                              },
                                               [](std::unique_ptr<bio::filtering_istreambuf>& strm) {
                                                  std::vector<char> bytes;
                                                  bio::copy(*strm, bio::back_inserter(bytes));
                                                  return bytes;
                                               } },
                     result.buf);
}

static std::vector<eosio::ship_protocol::transaction_trace> get_traces(eosio::state_history_log& log,
                                                                       block_num_type            block_num) {
   auto                                                          entry = get_decompressed_entry(log, block_num);
   std::vector<eosio::ship_protocol::transaction_trace>          traces;

   if (entry.size()) {
      eosio::input_stream traces_bin{ entry.data(), entry.data() + entry.size() };
      BOOST_REQUIRE_NO_THROW(from_bin(traces, traces_bin));
   }
   return traces;
}

BOOST_AUTO_TEST_CASE(test_splitted_log) {

   fc::temp_directory state_history_dir;

   eosio::state_history::partition_config config{
      .retained_dir = "retained",
      .archive_dir = "archive",
      .stride  = 20,
      .max_retained_files = 5
   };

   state_history_tester chain(state_history_dir.path(), config);
   chain.produce_blocks(50);

   deploy_test_api(chain);
   auto cfd_trace = push_test_cfd_transaction(chain);

   chain.produce_blocks(100);

   auto log_dir = state_history_dir.path();
   auto archive_dir  = log_dir / "archive";
   auto retained_dir = log_dir / "retained";

   BOOST_CHECK(std::filesystem::exists( archive_dir / "trace_history-2-20.log" ));
   BOOST_CHECK(std::filesystem::exists( archive_dir / "trace_history-2-20.index" ));
   BOOST_CHECK(std::filesystem::exists( archive_dir / "trace_history-21-40.log" ));
   BOOST_CHECK(std::filesystem::exists( archive_dir / "trace_history-21-40.index" ));

   BOOST_CHECK(std::filesystem::exists( archive_dir / "chain_state_history-2-20.log" ));
   BOOST_CHECK(std::filesystem::exists( archive_dir / "chain_state_history-2-20.index" ));
   BOOST_CHECK(std::filesystem::exists( archive_dir / "chain_state_history-21-40.log" ));
   BOOST_CHECK(std::filesystem::exists( archive_dir / "chain_state_history-21-40.index" ));

   BOOST_CHECK(std::filesystem::exists( retained_dir / "trace_history-41-60.log" ));
   BOOST_CHECK(std::filesystem::exists( retained_dir / "trace_history-41-60.index" ));
   BOOST_CHECK(std::filesystem::exists( retained_dir / "trace_history-61-80.log" ));
   BOOST_CHECK(std::filesystem::exists( retained_dir / "trace_history-61-80.index" ));
   BOOST_CHECK(std::filesystem::exists( retained_dir / "trace_history-81-100.log" ));
   BOOST_CHECK(std::filesystem::exists( retained_dir / "trace_history-81-100.index" ));
   BOOST_CHECK(std::filesystem::exists( retained_dir / "trace_history-101-120.log" ));
   BOOST_CHECK(std::filesystem::exists( retained_dir / "trace_history-101-120.index" ));
   BOOST_CHECK(std::filesystem::exists( retained_dir / "trace_history-121-140.log" ));
   BOOST_CHECK(std::filesystem::exists( retained_dir / "trace_history-121-140.index" ));

   BOOST_CHECK_EQUAL(chain.traces_log.block_range().first, 41);

   BOOST_CHECK(std::filesystem::exists( retained_dir / "chain_state_history-41-60.log" ));
   BOOST_CHECK(std::filesystem::exists( retained_dir / "chain_state_history-41-60.index" ));
   BOOST_CHECK(std::filesystem::exists( retained_dir / "chain_state_history-61-80.log" ));
   BOOST_CHECK(std::filesystem::exists( retained_dir / "chain_state_history-61-80.index" ));
   BOOST_CHECK(std::filesystem::exists( retained_dir / "chain_state_history-81-100.log" ));
   BOOST_CHECK(std::filesystem::exists( retained_dir / "chain_state_history-81-100.index" ));
   BOOST_CHECK(std::filesystem::exists( retained_dir / "chain_state_history-101-120.log" ));
   BOOST_CHECK(std::filesystem::exists( retained_dir / "chain_state_history-101-120.index" ));
   BOOST_CHECK(std::filesystem::exists( retained_dir / "chain_state_history-121-140.log" ));
   BOOST_CHECK(std::filesystem::exists( retained_dir / "chain_state_history-121-140.index" ));

   BOOST_CHECK_EQUAL(chain.chain_state_log.block_range().first, 41);

   BOOST_CHECK(get_traces(chain.traces_log, 10).empty());
   BOOST_CHECK(get_traces(chain.traces_log, 100).size());
   BOOST_CHECK(get_traces(chain.traces_log, 140).size());
   BOOST_CHECK(get_traces(chain.traces_log, 150).size());
   BOOST_CHECK(get_traces(chain.traces_log, 160).empty());

   BOOST_CHECK(get_decompressed_entry(chain.chain_state_log, 10).empty());
   BOOST_CHECK(get_decompressed_entry(chain.chain_state_log, 100).size());
   BOOST_CHECK(get_decompressed_entry(chain.chain_state_log, 140).size());
   BOOST_CHECK(get_decompressed_entry(chain.chain_state_log, 150).size());
   BOOST_CHECK(get_decompressed_entry(chain.chain_state_log, 160).empty());
}

void push_blocks( tester& from, tester& to ) {
   while( to.control->fork_db_head_block_num()
            < from.control->fork_db_head_block_num() )
   {
      auto fb = from.control->fetch_block_by_number( to.control->fork_db_head_block_num()+1 );
      to.push_block( fb );
   }
}

bool test_fork(uint32_t stride, uint32_t max_retained_files) {

   fc::temp_directory state_history_dir;

   eosio::state_history::partition_config config{
      .retained_dir = "retained",
      .archive_dir = "archive",
      .stride  = stride,
      .max_retained_files = max_retained_files
   };

   state_history_tester chain1(state_history_dir.path(), config);
   chain1.produce_blocks(2);

   chain1.create_accounts( {"dan"_n,"sam"_n,"pam"_n} );
   chain1.produce_block();
   chain1.set_producers( {"dan"_n,"sam"_n,"pam"_n} );
   chain1.produce_blocks(30);

   tester chain2(setup_policy::none);
   push_blocks(chain1, chain2);

   auto fork_block_num = chain1.control->head_block_num();

   chain1.produce_blocks(12);
   auto create_account_traces = chain2.create_accounts( {"adam"_n} );
   auto create_account_trace_id = create_account_traces[0]->id;

   auto b = chain2.produce_block();
   chain2.produce_blocks(11+12);

   for( uint32_t start = fork_block_num + 1, end = chain2.control->head_block_num(); start <= end; ++start ) {
      auto fb = chain2.control->fetch_block_by_number( start );
      chain1.push_block( fb );
   }
   auto traces = get_traces(chain1.traces_log, b->block_num());

   bool trace_found = std::find_if(traces.begin(), traces.end(), [create_account_trace_id](const auto& v) {
                         return std::get<eosio::ship_protocol::transaction_trace_v0>(v).id == create_account_trace_id;
                      }) != traces.end();

   return trace_found;
}

BOOST_AUTO_TEST_CASE(test_fork_no_stride) {
   // In this case, the chain fork would NOT trunk the trace log across the stride boundary.
   BOOST_CHECK(test_fork(UINT32_MAX, 10));
}
BOOST_AUTO_TEST_CASE(test_fork_with_stride1) {
   // In this case, the chain fork would trunk the trace log across the stride boundary.
   // However, there are still some traces remains after the truncation.
   BOOST_CHECK(test_fork(10, 10));
}
BOOST_AUTO_TEST_CASE(test_fork_with_stride2) {
   // In this case, the chain fork would trunk the trace log across the stride boundary.
   // However, no existing trace remain after the truncation. Because we only keep a very
   // short history, the create_account_trace is not available to be found. We just need
   // to make sure no exception is throw.
   BOOST_CHECK_NO_THROW(test_fork(5, 1));
}

BOOST_AUTO_TEST_CASE(test_corrupted_log_recovery) {

   fc::temp_directory state_history_dir;

   eosio::state_history::partition_config config{
      .archive_dir = "archive",
      .stride  = 100,
      .max_retained_files = 5
   };

   state_history_tester chain(state_history_dir.path(), config);
   chain.produce_blocks(50);
   chain.close();

   // write a few random bytes to block log indicating the last block entry is incomplete
   fc::cfile logfile;
   logfile.set_file_path(state_history_dir.path() / "trace_history.log");
   logfile.open("ab");
   const char random_data[] = "12345678901231876983271649837";
   logfile.write(random_data, sizeof(random_data));

   std::filesystem::remove_all(chain.get_config().blocks_dir/"reversible");

   state_history_tester new_chain(state_history_dir.path(), config);
   new_chain.produce_blocks(50);

   BOOST_CHECK(get_traces(new_chain.traces_log, 10).size());
   BOOST_CHECK(get_decompressed_entry(new_chain.chain_state_log,10).size());
}

BOOST_AUTO_TEST_SUITE_END()
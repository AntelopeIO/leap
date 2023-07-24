#pragma once

#include <eosio/testing/tester.hpp>
#include <eosio/chain_plugin/chain_plugin.hpp>
#include <eosio/chain/application.hpp>
#include <eosio/chain/types.hpp>
#include <eosio/chain/trace.hpp>
#include <eosio/chain/config.hpp>
#include <eosio/chain/transaction.hpp>
#include <eosio/chain/controller.hpp>

#include <fc/exception/exception.hpp>

#include <contracts.hpp>
#include <exception>
#include <future>
#include <stdexcept>
#include <thread>

namespace eosio::test_utils {

using namespace eosio::chain;
using namespace eosio::chain::literals;

struct testit {
   uint64_t id;
   explicit testit(uint64_t id = 0)
      :id(id){}
   static account_name get_account() {
      return chain::config::system_account_name;
   }
   static action_name get_name() {
      return "testit"_n;
   }
};

// Corresponds to the reqactivated action of the bios contract.
// See libraries/testing/contracts/eosio.bios/eosio.bios.hpp
struct reqactivated {
   chain::digest_type feature_digest;

   explicit reqactivated(const chain::digest_type& fd)
      :feature_digest(fd){};

   static account_name get_account() {
      return chain::config::system_account_name;
   }
   static action_name get_name() {
      return "reqactivated"_n;
   }
};

// Create a read-only trx that works with bios reqactivated action
auto make_bios_ro_trx(eosio::chain::controller& control) {
   const auto& pfm = control.get_protocol_feature_manager();
   static auto feature_digest = pfm.get_builtin_digest(builtin_protocol_feature_t::replace_deferred);

   signed_transaction trx;
   trx.expiration = fc::time_point_sec{fc::time_point::now() + fc::seconds(30)};
   vector<permission_level> no_auth{};
   trx.actions.emplace_back( no_auth, reqactivated{*feature_digest} );
   return std::make_shared<packed_transaction>( std::move(trx) );
}

// Push an input transaction to controller and return trx trace
// If account is eosio then signs with the default private key
auto push_input_trx(appbase::scoped_app& app, eosio::chain::controller& control, account_name account, signed_transaction& trx) {
   trx.expiration = fc::time_point_sec{fc::time_point::now() + fc::seconds(30)};
   trx.set_reference_block( control.head_block_id() );
   if (account == config::system_account_name) {
      auto default_priv_key = private_key_type::regenerate<fc::ecc::private_key_shim>(fc::sha256::hash(std::string("nathan")));
      trx.sign(default_priv_key, control.get_chain_id());
   } else {
      trx.sign(testing::tester::get_private_key(account, "active"), control.get_chain_id());
   }
   auto ptrx = std::make_shared<packed_transaction>( trx, packed_transaction::compression_type::zlib );

   auto trx_promise = std::make_shared<std::promise<transaction_trace_ptr>>();
   std::future<transaction_trace_ptr> trx_future = trx_promise->get_future();

   app->executor().post( priority::low, exec_queue::read_write, [&ptrx, &app, trx_promise]() {
      app->get_method<plugin_interface::incoming::methods::transaction_async>()(ptrx,
                                                                                false, // api_trx
                                                                                transaction_metadata::trx_type::input, // trx_type
                                                                                true, // return_failure_traces
           [trx_promise](const next_function_variant<transaction_trace_ptr>& result) {
              if( std::holds_alternative<fc::exception_ptr>( result ) ) {
                 try {
                    std::get<fc::exception_ptr>(result)->dynamic_rethrow_exception();
                 } catch(...) {
                    trx_promise->set_exception(std::current_exception());
                 }
              } else if ( std::get<chain::transaction_trace_ptr>( result )->except ) {
                 try {
                    std::get<chain::transaction_trace_ptr>(result)->except->dynamic_rethrow_exception();
                 } catch(...) {
                    trx_promise->set_exception(std::current_exception());
                 }
              } else {
                 trx_promise->set_value(std::get<chain::transaction_trace_ptr>(result));
              }
           });
   });

   if (trx_future.wait_for(std::chrono::seconds(5)) == std::future_status::timeout)
      throw std::runtime_error("failed to execute trx: " + ptrx->get_transaction().actions.at(0).name.to_string() + " to account: " + account.to_string());

   return trx_future.get();
}

// Push setcode trx to controller and return trx trace
auto set_code(appbase::scoped_app& app, eosio::chain::controller& control, account_name account, const vector<uint8_t>& wasm) {
   signed_transaction trx;
   trx.actions.emplace_back(std::vector<permission_level>{{account, config::active_name}},
                            chain::setcode{
                               .account = account,
                               .vmtype = 0,
                               .vmversion = 0,
                               .code = bytes(wasm.begin(), wasm.end())
                            });
   return push_input_trx(app, control, account, trx);
}

void activate_protocol_features_set_bios_contract(appbase::scoped_app& app, chain_plugin* chain_plug) {
   using namespace appbase;

   auto feature_set = std::make_shared<std::atomic<bool>>(false);
   // has to execute when pending block is not null
   for (int tries = 0; tries < 100; ++tries) {
      app->executor().post( priority::high, exec_queue::read_write, [&chain_plug=chain_plug, feature_set](){
         try {
            if (!chain_plug->chain().is_building_block() || *feature_set)
               return;
            const auto& pfm = chain_plug->chain().get_protocol_feature_manager();
            auto preactivate_feature_digest = pfm.get_builtin_digest(builtin_protocol_feature_t::preactivate_feature);
            BOOST_CHECK( preactivate_feature_digest );
            chain_plug->chain().preactivate_feature( *preactivate_feature_digest, false );
            std::vector<builtin_protocol_feature_t> pfs{
               builtin_protocol_feature_t::only_link_to_existing_permission,
               builtin_protocol_feature_t::replace_deferred,
               builtin_protocol_feature_t::no_duplicate_deferred_id,
               builtin_protocol_feature_t::fix_linkauth_restriction,
               builtin_protocol_feature_t::disallow_empty_producer_schedule,
               builtin_protocol_feature_t::restrict_action_to_self,
               builtin_protocol_feature_t::only_bill_first_authorizer,
               builtin_protocol_feature_t::forward_setcode,
               builtin_protocol_feature_t::get_sender,
               builtin_protocol_feature_t::ram_restrictions,
               builtin_protocol_feature_t::webauthn_key,
               builtin_protocol_feature_t::wtmsig_block_signatures };
            for (const auto t : pfs) {
               auto feature_digest = pfm.get_builtin_digest(t);
               BOOST_CHECK( feature_digest );
               chain_plug->chain().preactivate_feature( *feature_digest, false );
            }
            *feature_set = true;
            return;
         } FC_LOG_AND_DROP()
         BOOST_CHECK(!"exception setting protocol features");
      });
      if (*feature_set)
         break;
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
   }

   // Wait for next block
   std::this_thread::sleep_for( std::chrono::milliseconds(config::block_interval_ms) );

   auto r = set_code(app, chain_plug->chain(), config::system_account_name, testing::contracts::eosio_bios_wasm());
   BOOST_CHECK(r->receipt && r->receipt->status == transaction_receipt_header::executed);
}


} // namespace eosio::test_utils

FC_REFLECT( eosio::test_utils::testit, (id) )
FC_REFLECT( eosio::test_utils::reqactivated, (feature_digest) )

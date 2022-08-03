#include <eosio/chain_plugin/chain_plugin.hpp>

#include <fc/io/json.hpp>
#include <fc/bitutil.hpp>

#include <contracts.hpp>

#include <iostream>

enum return_codes {
   OTHER_FAIL        = -2,
   INITIALIZE_FAIL   = -1,
   SUCCESS           = 0,
   BAD_ALLOC         = 1,
   DATABASE_DIRTY    = 2,
   FIXED_REVERSIBLE  = SUCCESS,
   EXTRACTED_GENESIS = SUCCESS,
   NODE_MANAGEMENT_SUCCESS = 5
};

uint64_t _total_us = 0;
uint64_t _txcount = 0;
unsigned batch;
uint64_t nonce_prefix;


using namespace eosio::testing;
using namespace eosio::chain;
using namespace eosio;

void push_next_transaction(const std::shared_ptr<std::vector<signed_transaction>>& trxs, const std::function<void(const fc::exception_ptr&)>& next) {
   chain_plugin& cp = app().get_plugin<chain_plugin>();

   for (size_t i = 0; i < trxs->size(); ++i) {
      cp.accept_transaction( std::make_shared<packed_transaction>(trxs->at(i)), [=](const std::variant<fc::exception_ptr, transaction_trace_ptr>& result){

         fc::exception_ptr except_ptr;
         if (std::holds_alternative<fc::exception_ptr>(result)) {
            except_ptr = std::get<fc::exception_ptr>(result);
         } else if (std::get<transaction_trace_ptr>(result)->except) {
            except_ptr = std::get<transaction_trace_ptr>(result)->except->dynamic_copy_exception();
         }

         if (except_ptr) {
            next(std::get<fc::exception_ptr>(result));
         } else {
            if (std::holds_alternative<transaction_trace_ptr>(result) && std::get<transaction_trace_ptr>(result)->receipt) {
               _total_us += std::get<transaction_trace_ptr>(result)->receipt->cpu_usage_us;
               ++_txcount;
            }
         }
      });
   }
}

void push_transactions( std::vector<signed_transaction>&& trxs, const std::function<void(fc::exception_ptr)>& next) {
   auto trxs_copy = std::make_shared<std::decay_t<decltype(trxs)>>(std::move(trxs));
   app().post(priority::low, [trxs_copy, next]() {
      push_next_transaction(trxs_copy, next);
   });
}

void create_test_accounts(const std::string& init_name, const std::string& init_priv_key, name& newaccountT, name& newaccountA, name& newaccountB, const fc::microseconds& abi_serializer_max_time, const chain_id_type& chain_id, const block_id_type& reference_block_id, const std::function<void(const fc::exception_ptr&)>& next) {
   ilog("create_test_accounts");
   std::vector<signed_transaction> trxs;
   trxs.reserve(2);

   try {
      name creator(init_name);

      abi_def currency_abi_def = fc::json::from_string(contracts::eosio_token_abi().data()).as<abi_def>();

      abi_serializer eosio_token_serializer{fc::json::from_string(contracts::eosio_token_abi().data()).as<abi_def>(),
                                             abi_serializer::create_yield_function( abi_serializer_max_time )};

      fc::crypto::private_key txn_test_receiver_A_priv_key = fc::crypto::private_key::regenerate(fc::sha256(std::string(64, 'a')));
      fc::crypto::private_key txn_test_receiver_B_priv_key = fc::crypto::private_key::regenerate(fc::sha256(std::string(64, 'b')));
      fc::crypto::private_key txn_test_receiver_C_priv_key = fc::crypto::private_key::regenerate(fc::sha256(std::string(64, 'c')));
      fc::crypto::public_key  txn_text_receiver_A_pub_key = txn_test_receiver_A_priv_key.get_public_key();
      fc::crypto::public_key  txn_text_receiver_B_pub_key = txn_test_receiver_B_priv_key.get_public_key();
      fc::crypto::public_key  txn_text_receiver_C_pub_key = txn_test_receiver_C_priv_key.get_public_key();
      fc::crypto::private_key creator_priv_key = fc::crypto::private_key(init_priv_key);

      //create some test accounts
      {
         signed_transaction trx;

         //create "A" account
         {
         auto owner_auth   = eosio::chain::authority{1, {{txn_text_receiver_A_pub_key, 1}}, {}};
         auto active_auth  = eosio::chain::authority{1, {{txn_text_receiver_A_pub_key, 1}}, {}};

         trx.actions.emplace_back(vector<chain::permission_level>{{creator,name("active")}}, newaccount{creator, newaccountA, owner_auth, active_auth});
         }
         //create "B" account
         {
         auto owner_auth   = eosio::chain::authority{1, {{txn_text_receiver_B_pub_key, 1}}, {}};
         auto active_auth  = eosio::chain::authority{1, {{txn_text_receiver_B_pub_key, 1}}, {}};

         trx.actions.emplace_back(vector<chain::permission_level>{{creator,name("active")}}, newaccount{creator, newaccountB, owner_auth, active_auth});
         }
         //create "T" account
         {
         auto owner_auth   = eosio::chain::authority{1, {{txn_text_receiver_C_pub_key, 1}}, {}};
         auto active_auth  = eosio::chain::authority{1, {{txn_text_receiver_C_pub_key, 1}}, {}};

         trx.actions.emplace_back(vector<chain::permission_level>{{creator,name("active")}}, newaccount{creator, newaccountT, owner_auth, active_auth});
         }

         // trx.expiration = cc.head_block_time() + fc::seconds(180);
         trx.expiration = fc::time_point::now() + fc::seconds(180);
         trx.set_reference_block(reference_block_id);
         trx.sign(creator_priv_key, chain_id);
         trxs.emplace_back(std::move(trx));
      }

      //set newaccountT contract to eosio.token & initialize it
      {
         signed_transaction trx;

         vector<uint8_t> wasm = contracts::eosio_token_wasm();

         setcode handler;
         handler.account = newaccountT;
         handler.code.assign(wasm.begin(), wasm.end());

         trx.actions.emplace_back( vector<chain::permission_level>{{newaccountT,name("active")}}, handler);

         {
            setabi handler;
            handler.account = newaccountT;
            handler.abi = fc::raw::pack(json::from_string(contracts::eosio_token_abi().data()).as<abi_def>());
            trx.actions.emplace_back( vector<chain::permission_level>{{newaccountT,name("active")}}, handler);
         }

         {
            action act;
            act.account = newaccountT;
            act.name = "create"_n;
            act.authorization = vector<permission_level>{{newaccountT,config::active_name}};
            act.data = eosio_token_serializer.variant_to_binary("create",
                                                                  fc::json::from_string(fc::format_string("{\"issuer\":\"${issuer}\",\"maximum_supply\":\"1000000000.0000 CUR\"}}",
                                                                  fc::mutable_variant_object()("issuer",newaccountT.to_string()))),
                                                                  abi_serializer::create_yield_function( abi_serializer_max_time ));
            trx.actions.push_back(act);
         }
         {
            action act;
            act.account = newaccountT;
            act.name = "issue"_n;
            act.authorization = vector<permission_level>{{newaccountT,config::active_name}};
            act.data = eosio_token_serializer.variant_to_binary("issue",
                                                                  fc::json::from_string(fc::format_string("{\"to\":\"${to}\",\"quantity\":\"60000.0000 CUR\",\"memo\":\"\"}",
                                                                  fc::mutable_variant_object()("to",newaccountT.to_string()))),
                                                                  abi_serializer::create_yield_function( abi_serializer_max_time ));
            trx.actions.push_back(act);
         }
         {
            action act;
            act.account = newaccountT;
            act.name = "transfer"_n;
            act.authorization = vector<permission_level>{{newaccountT,config::active_name}};
            act.data = eosio_token_serializer.variant_to_binary("transfer",
                                                                  fc::json::from_string(fc::format_string("{\"from\":\"${from}\",\"to\":\"${to}\",\"quantity\":\"20000.0000 CUR\",\"memo\":\"\"}",
                                                                  fc::mutable_variant_object()("from",newaccountT.to_string())("to",newaccountA.to_string()))),
                                                                  abi_serializer::create_yield_function( abi_serializer_max_time ));
            trx.actions.push_back(act);
         }
         {
            action act;
            act.account = newaccountT;
            act.name = "transfer"_n;
            act.authorization = vector<permission_level>{{newaccountT,config::active_name}};
            act.data = eosio_token_serializer.variant_to_binary("transfer",
                                                                  fc::json::from_string(fc::format_string("{\"from\":\"${from}\",\"to\":\"${to}\",\"quantity\":\"20000.0000 CUR\",\"memo\":\"\"}",
                                                                  fc::mutable_variant_object()("from",newaccountT.to_string())("to",newaccountB.to_string()))),
                                                                  abi_serializer::create_yield_function( abi_serializer_max_time ));
            trx.actions.push_back(act);
         }

         trx.expiration = fc::time_point::now() + fc::seconds(180);
         trx.set_reference_block(reference_block_id);
         trx.max_net_usage_words = 5000;
         trx.sign(txn_test_receiver_C_priv_key, chain_id);
         trxs.emplace_back(std::move(trx));
      }
   } catch ( const std::bad_alloc& ) {
      throw;
   } catch ( const boost::interprocess::bad_alloc& ) {
      throw;
   } catch (const fc::exception& e) {
      next(e.dynamic_copy_exception());
      return;
   } catch (const std::exception& e) {
      next(fc::std_exception_wrapper::from_current_exception(e).dynamic_copy_exception());
      return;
   }

   push_transactions(std::move(trxs), next);
}

string start_generation(const std::string& salt, const uint64_t& period, const uint64_t& batch_size, const name& newaccountT, const name& newaccountA, const name& newaccountB, action& act_a_to_b, action& act_b_to_a, const fc::microseconds& abi_serializer_max_time) {
   ilog("Starting transaction test plugin");
   if(period < 1 || period > 2500)
      return "period must be between 1 and 2500";
   if(batch_size < 1 || batch_size > 250)
      return "batch_size must be between 1 and 250";
   if(batch_size & 1)
      return "batch_size must be even";
   ilog("Starting transaction test plugin valid");

   abi_serializer eosio_token_serializer{fc::json::from_string(contracts::eosio_token_abi().data()).as<abi_def>(), abi_serializer::create_yield_function( abi_serializer_max_time )};
   //create the actions here
   act_a_to_b.account = newaccountT;
   act_a_to_b.name = "transfer"_n;
   act_a_to_b.authorization = vector<permission_level>{{newaccountA,config::active_name}};
   act_a_to_b.data = eosio_token_serializer.variant_to_binary("transfer",
                                                               fc::json::from_string(fc::format_string("{\"from\":\"${from}\",\"to\":\"${to}\",\"quantity\":\"1.0000 CUR\",\"memo\":\"${l}\"}",
                                                               fc::mutable_variant_object()("from",newaccountA.to_string())("to",newaccountB.to_string())("l", salt))),
                                                               abi_serializer::create_yield_function( abi_serializer_max_time ));

   act_b_to_a.account = newaccountT;
   act_b_to_a.name = "transfer"_n;
   act_b_to_a.authorization = vector<permission_level>{{newaccountB,config::active_name}};
   act_b_to_a.data = eosio_token_serializer.variant_to_binary("transfer",
                                                               fc::json::from_string(fc::format_string("{\"from\":\"${from}\",\"to\":\"${to}\",\"quantity\":\"1.0000 CUR\",\"memo\":\"${l}\"}",
                                                               fc::mutable_variant_object()("from",newaccountB.to_string())("to",newaccountA.to_string())("l", salt))),
                                                               abi_serializer::create_yield_function( abi_serializer_max_time ));

   batch = batch_size/2;
   nonce_prefix = 0;

   return "success";
}

void send_transaction(std::function<void(const fc::exception_ptr&)> next, uint64_t nonce_prefix, const action& act_a_to_b, const action& act_b_to_a, const fc::microseconds& trx_expiration, const chain_id_type& chain_id, const block_id_type& reference_block_id) {
   std::vector<signed_transaction> trxs;
   trxs.reserve(2*batch);

   try {
      static fc::crypto::private_key a_priv_key = fc::crypto::private_key::regenerate(fc::sha256(std::string(64, 'a')));
      static fc::crypto::private_key b_priv_key = fc::crypto::private_key::regenerate(fc::sha256(std::string(64, 'b')));

      static uint64_t nonce = static_cast<uint64_t>(fc::time_point::now().sec_since_epoch()) << 32;

      for(unsigned int i = 0; i < batch; ++i) {
         {
            signed_transaction trx;
            trx.actions.push_back(act_a_to_b);
            trx.context_free_actions.emplace_back(action({}, config::null_account_name, name("nonce"), fc::raw::pack( std::to_string(nonce_prefix)+std::to_string(nonce++) )));
            trx.set_reference_block(reference_block_id);
            trx.expiration = fc::time_point::now() + trx_expiration;
            trx.max_net_usage_words = 100;
            trx.sign(a_priv_key, chain_id);
            trxs.emplace_back(std::move(trx));
         }

         {
            signed_transaction trx;
            trx.actions.push_back(act_b_to_a);
            trx.context_free_actions.emplace_back(action({}, config::null_account_name, name("nonce"), fc::raw::pack( std::to_string(nonce_prefix)+std::to_string(nonce++) )));
            trx.set_reference_block(reference_block_id);
            trx.expiration = fc::time_point::now() + trx_expiration;
            trx.max_net_usage_words = 100;
            trx.sign(b_priv_key, chain_id);
            trxs.emplace_back(std::move(trx));
         }
      }
   } catch ( const std::bad_alloc& ) {
      throw;
   } catch ( const boost::interprocess::bad_alloc& ) {
      throw;
   } catch ( const fc::exception& e ) {
      next(e.dynamic_copy_exception());
   } catch (const std::exception& e) {
      next(fc::std_exception_wrapper::from_current_exception(e).dynamic_copy_exception());
   }

   push_transactions(std::move(trxs), next);
}

void stop_generation() {
   ilog("Stopping transaction generation");

   if (_txcount) {
      ilog("${d} transactions executed, ${t}us / transaction", ("d", _txcount)("t", _total_us / (double)_txcount));
      _txcount = _total_us = 0;
   }
}

chain::block_id_type make_block_id( uint32_t block_num ) {
   chain::block_id_type block_id;
   block_id._hash[0] &= 0xffffffff00000000;
   block_id._hash[0] += fc::endian_reverse_u32(block_num);
   return block_id;
}

int main(int argc, char** argv)
{
   name                                                 newaccountA;
   name                                                 newaccountB;
   name                                                 newaccountT;
   fc::microseconds                                     trx_expiration{3600};

   action act_a_to_b;
   action act_b_to_a;

   const std::string thread_pool_account_prefix = "txngentest";
   const std::string init_name = "eosio";
   const std::string init_priv_key = "5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3";
   const std::string salt = "";
   const uint64_t& period = 20;
   const uint64_t& batch_size = 20;

   const static uint32_t   default_abi_serializer_max_time_us = 15*1000;
   const static fc::microseconds   abi_serializer_max_time = fc::microseconds(default_abi_serializer_max_time_us);
   const chain_id_type             chain_id("cf057bbfb72640471fd910bcb67639c22df9f92470936cddc1ade0e2f2e7dc4f");
   // other chain_id: 60fb0eb4742886af8a0e147f4af6fd363e8e8d8f18bdf73a10ee0134fec1c551

   uint32_t reference_block_num = 0;
   // uint32_t reference_block_num = cc.last_irreversible_block_num();
   // // if (txn_reference_block_lag >= 0) {
   // //    reference_block_num = cc.head_block_num();
   // //    if (reference_block_num <= (uint32_t)txn_reference_block_lag) {
   // //       reference_block_num = 0;
   // //    } else {
   // //       reference_block_num -= (uint32_t)txn_reference_block_lag;
   // //    }
   // // }
   // block_id_type reference_block_id = cc.get_block_id_for_num(reference_block_num);
   block_id_type reference_block_id = make_block_id(reference_block_num);

   try {
      //Initialize
      newaccountA = eosio::chain::name(thread_pool_account_prefix + "a");
      newaccountB = eosio::chain::name(thread_pool_account_prefix + "b");
      newaccountT = eosio::chain::name(thread_pool_account_prefix + "t");
      // EOS_ASSERT(trx_expiration < fc::seconds(3600), chain::plugin_config_exception,
      //            "txn-test-gen-expiration-seconds must be smaller than 3600");

      //Startup
      std::cout << "Create Test Accounts." << std::endl;
      // CALL_ASYNC(txn_test_gen, my, create_test_accounts, INVOKE_ASYNC_R_R(my, create_test_accounts, std::string, std::string), 200),
      create_test_accounts(init_name, init_priv_key, newaccountT, newaccountA, newaccountB, abi_serializer_max_time, chain_id, reference_block_id, [](const fc::exception_ptr& e){
         if (e) {
            elog("create test accounts failed: ${e}", ("e", e->to_detail_string()));
         }
      });

      std::cout << "Stop Generation." << std::endl;
      // CALL(txn_test_gen, my, stop_generation, INVOKE_V_V(my, stop_generation), 200),
      stop_generation();

      std::cout << "Start Generation." << std::endl;
      // CALL(txn_test_gen, my, start_generation, INVOKE_V_R_R_R(my, start_generation, std::string, uint64_t, uint64_t), 200)
      start_generation(salt, period, batch_size, newaccountT, newaccountA, newaccountB, act_a_to_b, act_b_to_a, abi_serializer_max_time);

      std::cout << "Send Transaction." << std::endl;
      send_transaction([](const fc::exception_ptr& e){
         if (e) {
            elog("pushing transaction failed: ${e}", ("e", e->to_detail_string()));
            stop_generation();
         }
      }, nonce_prefix++, act_a_to_b, act_b_to_a, trx_expiration, chain_id, reference_block_id);

      //Stop & Cleanup
      std::cout << "Stop Generation." << std::endl;
      // CALL(txn_test_gen, my, stop_generation, INVOKE_V_V(my, stop_generation), 200),
      stop_generation();

   } catch( const std::exception& e ) {
      elog("${e}", ("e",e.what()));
      return OTHER_FAIL;
   } catch( ... ) {
      elog("unknown exception");
      return OTHER_FAIL;
   }

   return SUCCESS;
}

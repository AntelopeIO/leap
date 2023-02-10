#include <trx_generator.hpp>
#include <eosio/chain_plugin/chain_plugin.hpp>
#include <iostream>
#include <fc/log/logger.hpp>
#include <boost/algorithm/string.hpp>
#include <eosio/chain/chain_id_type.hpp>
#include <eosio/chain/name.hpp>
#include <fc/bitutil.hpp>
#include <fc/io/raw.hpp>
#include <regex>

using namespace std;
using namespace eosio::chain;
using namespace eosio;
using namespace appbase;
namespace bpo=boost::program_options;

namespace eosio::testing {

   void trx_generator_base::set_transaction_headers(transaction& trx, const block_id_type& last_irr_block_id, const fc::microseconds expiration, uint32_t delay_sec) {
      trx.expiration = fc::time_point::now() + expiration;
      trx.set_reference_block(last_irr_block_id);

      trx.max_net_usage_words = 0;// No limit
      trx.max_cpu_usage_ms = 0;   // No limit
      trx.delay_sec = delay_sec;
   }

   signed_transaction_w_signer trx_generator_base::create_trx_w_actions_and_signer(std::vector<action> acts, const fc::crypto::private_key& priv_key, uint64_t& nonce_prefix, uint64_t& nonce, const fc::microseconds& trx_expiration, const chain_id_type& chain_id, const block_id_type& last_irr_block_id) {
      signed_transaction trx;
      set_transaction_headers(trx, last_irr_block_id, trx_expiration);
      for (auto act:acts) {
         trx.actions.push_back(act);
      }
      trx.context_free_actions.emplace_back(action({}, config::null_account_name, name("nonce"),
         fc::raw::pack(std::to_string(nonce_prefix) + ":" + std::to_string(++nonce) + ":" +
         fc::time_point::now().time_since_epoch().count())));

      trx.sign(priv_key, chain_id);
      return signed_transaction_w_signer(trx, priv_key);
   }

   vector<signed_transaction_w_signer> transfer_trx_generator::create_initial_transfer_transactions(const vector<action_pair_w_keys>& action_pairs_vector, uint64_t& nonce_prefix, uint64_t& nonce, const fc::microseconds& trx_expiration, const chain_id_type& chain_id, const block_id_type& last_irr_block_id) {
      std::vector<signed_transaction_w_signer> trxs;
      trxs.reserve(2 * action_pairs_vector.size());

      std::vector<action> act_vec;
      for(action_pair_w_keys ap: action_pairs_vector) {
         act_vec.push_back(ap._first_act);
         trxs.emplace_back(create_trx_w_actions_and_signer(act_vec, ap._first_act_priv_key, nonce_prefix, nonce, trx_expiration, chain_id, last_irr_block_id));
         act_vec.clear();
         act_vec.push_back(ap._second_act);
         trxs.emplace_back(create_trx_w_actions_and_signer(act_vec, ap._second_act_priv_key, nonce_prefix, nonce, trx_expiration, chain_id, last_irr_block_id));
         act_vec.clear();
      }

      return trxs;
   }

   void trx_generator_base::update_resign_transaction(signed_transaction& trx, fc::crypto::private_key priv_key, uint64_t& nonce_prefix, uint64_t& nonce, const fc::microseconds& trx_expiration, const chain_id_type& chain_id, const block_id_type& last_irr_block_id) {
      trx.context_free_actions.clear();
      trx.context_free_actions.emplace_back(action({}, config::null_account_name, name("nonce"), fc::raw::pack(std::to_string(nonce_prefix) + ":" + std::to_string(++nonce) + ":" + fc::time_point::now().time_since_epoch().count())));
      set_transaction_headers(trx, last_irr_block_id, trx_expiration);
      trx.signatures.clear();
      trx.sign(priv_key, chain_id);
   }

   chain::bytes transfer_trx_generator::make_transfer_data(const chain::name& from, const chain::name& to, const chain::asset& quantity, const std::string&& memo) {
      return fc::raw::pack<chain::name>(from, to, quantity, memo);
   }

   auto transfer_trx_generator::make_transfer_action(chain::name account, chain::name from, chain::name to, chain::asset quantity, std::string memo) {
      return chain::action(std::vector<chain::permission_level>{{from, chain::config::active_name}},
                           account, "transfer"_n, make_transfer_data(from, to, quantity, std::move(memo)));
   }

   vector<action_pair_w_keys> transfer_trx_generator::create_initial_transfer_actions(const std::string& salt, const uint64_t& period, const name& contract_owner_account, const vector<name>& accounts, const vector<fc::crypto::private_key>& priv_keys) {
      vector<action_pair_w_keys> actions_pairs_vector;

      for(size_t i = 0; i < accounts.size(); ++i) {
         for(size_t j = i + 1; j < accounts.size(); ++j) {
            //create the actions here
            ilog("create_initial_transfer_actions: creating transfer from ${acctA} to ${acctB}", ("acctA", accounts.at(i))("acctB", accounts.at(j)));
            action act_a_to_b = make_transfer_action(contract_owner_account, accounts.at(i), accounts.at(j), asset::from_string("1.0000 CUR"), salt);

            ilog("create_initial_transfer_actions: creating transfer from ${acctB} to ${acctA}", ("acctB", accounts.at(j))("acctA", accounts.at(i)));
            action act_b_to_a = make_transfer_action(contract_owner_account, accounts.at(j), accounts.at(i), asset::from_string("1.0000 CUR"), salt);

            actions_pairs_vector.push_back(action_pair_w_keys(act_a_to_b, act_b_to_a, priv_keys.at(i), priv_keys.at(j)));
         }
      }
      ilog("create_initial_transfer_actions: total action pairs created: ${pairs}", ("pairs", actions_pairs_vector.size()));
      return actions_pairs_vector;
   }

   trx_generator_base::trx_generator_base(uint16_t generator_id, std::string chain_id_in, std::string contract_owner_account, fc::microseconds trx_expr, std::string lib_id_str, std::string log_dir,
      bool stop_on_trx_failed, const std::string& peer_endpoint, unsigned short port)
    : _provider(peer_endpoint, port), _generator_id(generator_id), _chain_id(chain_id_in), _contract_owner_account(contract_owner_account), _trx_expiration(trx_expr),
      _last_irr_block_id(fc::variant(lib_id_str).as<block_id_type>()), _log_dir(log_dir), _stop_on_trx_failed(stop_on_trx_failed) {}

   transfer_trx_generator::transfer_trx_generator(uint16_t generator_id, std::string chain_id_in, std::string contract_owner_account, const std::vector<std::string>& accts,
         fc::microseconds trx_expr, const std::vector<std::string>& private_keys_str_vector, std::string lib_id_str, std::string log_dir, bool stop_on_trx_failed, const std::string& peer_endpoint, unsigned short port)
       : trx_generator_base(generator_id, chain_id_in, contract_owner_account, trx_expr, lib_id_str, log_dir, stop_on_trx_failed, peer_endpoint, port), _accts(accts), _private_keys_str_vector(private_keys_str_vector) {}

   vector<name> transfer_trx_generator::get_accounts(const vector<string>& account_str_vector) {
      vector<name> acct_name_list;
      for(string account_name: account_str_vector) {
         ilog("get_account about to try to create name for ${acct}", ("acct", account_name));
         acct_name_list.push_back(eosio::chain::name(account_name));
      }
      return acct_name_list;
   }

   vector<fc::crypto::private_key> transfer_trx_generator::get_private_keys(const vector<string>& priv_key_str_vector) {
      vector<fc::crypto::private_key> key_list;
      for(const string& private_key: priv_key_str_vector) {
         ilog("get_private_keys about to try to create private_key for ${key} : gen key ${newKey}", ("key", private_key)("newKey", fc::crypto::private_key(private_key)));
         key_list.push_back(fc::crypto::private_key(private_key));
      }
      return key_list;
   }

   bool transfer_trx_generator::setup() {

      const vector<name> accounts = get_accounts(_accts);
      const vector<fc::crypto::private_key> private_key_vector = get_private_keys(_private_keys_str_vector);

      const std::string salt = std::to_string(getpid());
      const uint64_t &period = 20;
      _nonce_prefix = 0;
      _nonce = static_cast<uint64_t>(fc::time_point::now().sec_since_epoch()) << 32;

      ilog("Create All Initial Transfer Action/Reaction Pairs (acct 1 -> acct 2, acct 2 -> acct 1) between all provided accounts.");
      const auto action_pairs_vector = create_initial_transfer_actions(salt, period, _contract_owner_account, accounts,
                                                                       private_key_vector);

      ilog("Stop Generation (form potential ongoing generation in preparation for starting new generation run).");
      stop_generation();

      ilog("Create All Initial Transfer Transactions (one for each created action).");
      _trxs = create_initial_transfer_transactions(action_pairs_vector,
                                                   ++_nonce_prefix,
                                                   _nonce,
                                                   _trx_expiration,
                                                   _chain_id,
                                                   _last_irr_block_id);

      ilog("Setup p2p transaction provider");

      ilog("Update each trx to qualify as unique and fresh timestamps, re-sign trx, and send each updated transactions via p2p transaction provider");

      _provider.setup();
      return true;
   }

   fc::variant trx_generator::json_from_file_or_string(const string& file_or_str, fc::json::parse_type ptype)
   {
      regex r("^[ \t]*[\{\[]");
      if ( !regex_search(file_or_str, r) && fc::is_regular_file(file_or_str) ) {
         try {
            return fc::json::from_file(file_or_str, ptype);
         } EOS_RETHROW_EXCEPTIONS(json_parse_exception, "Fail to parse JSON from file: ${file}", ("file", file_or_str));

      } else {
         try {
            return fc::json::from_string(file_or_str, ptype);
         } EOS_RETHROW_EXCEPTIONS(json_parse_exception, "Fail to parse JSON from string: ${string}", ("string", file_or_str));
      }
   }

   void trx_generator::locate_key_words_in_action_mvo(std::vector<std::string>& acct_gen_fields_out, fc::mutable_variant_object& action_mvo, const std::string& key_word) {
      for(const mutable_variant_object::entry& e: action_mvo) {
         if(e.value().get_type() == fc::variant::string_type && e.value() == key_word) {
            acct_gen_fields_out.push_back(e.key());
         } else if(e.value().get_type() == fc::variant::object_type) {
            auto inner_mvo = fc::mutable_variant_object(e.value());
            locate_key_words_in_action_mvo(acct_gen_fields_out, inner_mvo, key_word);
         }
      }
   }

   void trx_generator::locate_key_words_in_action_array(std::map<int, std::vector<std::string>>& acct_gen_fields_out, fc::variants& action_array, const std::string& key_word) {
      for(size_t i = 0; i < action_array.size(); ++i) {
         auto action_mvo = fc::mutable_variant_object(action_array[i]);
         locate_key_words_in_action_mvo(acct_gen_fields_out[i], action_mvo, key_word);
      }
   }

   void trx_generator::update_key_word_fields_in_sub_action(std::string key, fc::mutable_variant_object& action_mvo, std::string action_inner_key, const std::string key_word) {
      auto mvo = action_mvo.find(action_inner_key);
      if(mvo != action_mvo.end()) {
         fc::mutable_variant_object inner_mvo = fc::mutable_variant_object(action_mvo[action_inner_key].get_object());
         if (inner_mvo.find(key) != inner_mvo.end()) {
            inner_mvo.set(key, key_word);
            action_mvo.set(action_inner_key, inner_mvo);
         }
      }
   }

   void trx_generator::update_key_word_fields_in_action(std::vector<std::string>& acct_gen_fields, fc::mutable_variant_object& action_mvo, const std::string key_word) {
      for(auto key: acct_gen_fields) {
         auto mvo = action_mvo.find(key);
         if(mvo != action_mvo.end()) {
            action_mvo.set(key, key_word);
         } else {
            for(auto e: action_mvo) {
               if(e.value().get_type() == fc::variant::object_type) {
                  update_key_word_fields_in_sub_action(key, action_mvo, e.key(), key_word);
               }
            }
         }
      }
   }

   void trx_generator::update_resign_transaction(signed_transaction& trx, fc::crypto::private_key priv_key, uint64_t& nonce_prefix, uint64_t& nonce, const fc::microseconds& trx_expiration, const chain_id_type& chain_id, const block_id_type& last_irr_block_id) {
      trx.actions.clear();
      update_actions();
      for(auto act: _actions) {
         trx.actions.push_back(act);
      }
      trx_generator_base::update_resign_transaction(trx, priv_key, nonce_prefix, nonce, trx_expiration, chain_id, last_irr_block_id);
   }

   trx_generator::trx_generator(uint16_t generator_id, std::string chain_id_in, const std::string& abi_data_file, std::string contract_owner_account,
         const std::string& actions_data_json_file_or_str, const std::string& actions_auths_json_file_or_str,
         fc::microseconds trx_expr, std::string lib_id_str, std::string log_dir, bool stop_on_trx_failed,
         const std::string& peer_endpoint, unsigned short port)
       : trx_generator_base(generator_id, chain_id_in, contract_owner_account, trx_expr, lib_id_str, log_dir, stop_on_trx_failed, peer_endpoint, port),
         _abi_data_file_path(abi_data_file),
         _actions_data_json_file_or_str(actions_data_json_file_or_str), _actions_auths_json_file_or_str(actions_auths_json_file_or_str),
         _acct_name_generator() {}

   void trx_generator::update_actions() {
      _actions.clear();

      if (!_acct_gen_fields.empty()) {
         std::string generated_account_name = _acct_name_generator.calcName();
         _acct_name_generator.increment();

         for (auto const& [key, val] : _acct_gen_fields) {
            update_key_word_fields_in_action(_acct_gen_fields.at(key), _unpacked_actions.at(key), generated_account_name);
         }
      }

      for (auto action_mvo : _unpacked_actions) {
         chain::name action_name = chain::name(action_mvo["actionName"].as_string());
         chain::name action_auth_acct = chain::name(action_mvo["actionAuthAcct"].as_string());
         bytes packed_action_data;
         try {
            auto action_type = _abi.get_action_type( action_name );
            FC_ASSERT( !action_type.empty(), "Unknown action ${action} in contract ${contract}", ("action", action_name)( "contract", action_auth_acct ));
            packed_action_data = _abi.variant_to_binary( action_type, action_mvo["actionData"], abi_serializer::create_yield_function( abi_serializer_max_time ) );
         } EOS_RETHROW_EXCEPTIONS(transaction_type_exception, "Fail to parse unpacked action data JSON")

         eosio::chain::action act;
         act.account = _contract_owner_account;
         act.name = action_name;

         chain::name auth_actor = chain::name(action_mvo["authorization"].get_object()["actor"].as_string());
         chain::name auth_perm = chain::name(action_mvo["authorization"].get_object()["permission"].as_string());

         act.authorization = vector<permission_level>{{auth_actor, auth_perm}};
         act.data = std::move(packed_action_data);
         _actions.push_back(act);
      }
   }

   bool trx_generator::setup() {
      _nonce_prefix = 0;
      _nonce = static_cast<uint64_t>(fc::time_point::now().sec_since_epoch()) << 32;

      ilog("Stop Generation (form potential ongoing generation in preparation for starting new generation run).");
      stop_generation();

      ilog("Create Initial Transaction with action data.");
      _abi = abi_serializer(fc::json::from_file(_abi_data_file_path).as<abi_def>(), abi_serializer::create_yield_function( abi_serializer_max_time ));
      fc::variant unpacked_actions_data_json = json_from_file_or_string(_actions_data_json_file_or_str);
      fc::variant unpacked_actions_auths_data_json = json_from_file_or_string(_actions_auths_json_file_or_str);
      ilog("Loaded actions data: ${data}", ("data", fc::json::to_pretty_string(unpacked_actions_data_json)));
      ilog("Loaded actions auths data: ${auths}", ("auths", fc::json::to_pretty_string(unpacked_actions_auths_data_json)));

      const std::string gen_acct_name_per_trx("ACCT_PER_TRX");

      auto action_array = unpacked_actions_data_json.get_array();
      for (size_t i =0; i < action_array.size(); ++i ) {
         _unpacked_actions.push_back(fc::mutable_variant_object(action_array[i]));
      }
      locate_key_words_in_action_array(_acct_gen_fields, action_array, gen_acct_name_per_trx);

      if(!_acct_gen_fields.empty()) {
         ilog("Located the following account names that need to be generated and populted in each transaction:");
         for(auto e: _acct_gen_fields) {
            ilog("acct_gen_fields entry: ${value}", ("value", e));
         }
         ilog("Priming name generator for trx generator prefix.");
         _acct_name_generator.setPrefix(_generator_id);
      }

      ilog("Setting up transaction signer.");
      fc::crypto::private_key signer_key;
      signer_key = fc::crypto::private_key(unpacked_actions_auths_data_json.get_object()[_unpacked_actions.at(0)["actionAuthAcct"].as_string()].as_string());

      ilog("Setting up initial transaction actions.");
      update_actions();
      ilog("Initial actions (${count}):", ("count", _unpacked_actions.size()));
      for (size_t i = 0; i < _unpacked_actions.size(); ++i) {
         ilog("Initial action ${index}: ${act}", ("index", i)("act", fc::json::to_pretty_string(_unpacked_actions.at(i))));
         ilog("Initial action packed data ${index}: ${packed_data}", ("packed_data", fc::to_hex(_actions.at(i).data.data(), _actions.at(i).data.size())));
      }

      ilog("Populate initial transaction.");
      _trxs.emplace_back(create_trx_w_actions_and_signer(_actions, signer_key, ++_nonce_prefix, _nonce, _trx_expiration, _chain_id, _last_irr_block_id));

      ilog("Setup p2p transaction provider");

      ilog("Update each trx to qualify as unique and fresh timestamps and update each action with unique generated account name if necessary, re-sign trx, and send each updated transactions via p2p transaction provider");

      _provider.setup();
      return true;
   }

   bool trx_generator_base::tear_down() {
      _provider.log_trxs(_log_dir);
      _provider.teardown();

      ilog("Sent transactions: ${cnt}", ("cnt", _txcount));
      ilog("Tear down p2p transaction provider");

      //Stop & Cleanup
      ilog("Stop Generation.");
      stop_generation();
      return true;
   }

   bool trx_generator_base::generate_and_send() {
      try {
         if (_trxs.size()) {
            size_t index_to_send = _txcount % _trxs.size();
            push_transaction(_provider, _trxs.at(index_to_send), ++_nonce_prefix, _nonce, _trx_expiration, _chain_id,
                             _last_irr_block_id);
            ++_txcount;
         } else {
            elog("no transactions available to send");
            return false;
         }
      } catch (const std::exception &e) {
         elog("${e}", ("e", e.what()));
         return false;
      } catch (...) {
         elog("unknown exception");
         return false;
      }

      return true;
   }

   void trx_generator_base::log_first_trx(const std::string& log_dir, const chain::signed_transaction& trx) {
      std::ostringstream fileName;
      fileName << log_dir << "/first_trx_" << getpid() << ".txt";
      std::ofstream out(fileName.str());

      out << fc::string(trx.id()) << "\n";
      out.close();
   }

   void trx_generator_base::push_transaction(p2p_trx_provider& provider, signed_transaction_w_signer& trx, uint64_t& nonce_prefix, uint64_t& nonce, const fc::microseconds& trx_expiration, const chain_id_type& chain_id, const block_id_type& last_irr_block_id) {
      update_resign_transaction(trx._trx, trx._signer, ++nonce_prefix, nonce, trx_expiration, chain_id, last_irr_block_id);
      if (_txcount == 0) {
         log_first_trx(_log_dir, trx._trx);
      }
      provider.send(trx._trx);
   }

   void trx_generator_base::stop_generation() {
      ilog("Stopping transaction generation");

      if(_txcount) {
         ilog("${d} transactions executed, ${t}us / transaction", ("d", _txcount)("t", _total_us / (double) _txcount));
         _txcount = _total_us = 0;
      }
   }

   bool trx_generator_base::stop_on_trx_fail() {
      return _stop_on_trx_failed;
   }
}

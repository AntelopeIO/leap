#include <trx_generator.hpp>
#include <eosio/chain_plugin/chain_plugin.hpp>
#include <iostream>
#include <fc/log/logger.hpp>
#include <boost/algorithm/string.hpp>
#include <eosio/chain/chain_id_type.hpp>
#include <eosio/chain/name.hpp>
#include <eosio/chain/asset.hpp>
#include <fc/bitutil.hpp>
#include <fc/io/raw.hpp>

using namespace eosio::chain;
using namespace eosio;
using namespace appbase;
namespace bpo=boost::program_options;

namespace eosio::testing {
   struct action_pair_w_keys {
      action_pair_w_keys(eosio::chain::action first_action, eosio::chain::action second_action, fc::crypto::private_key first_act_signer, fc::crypto::private_key second_act_signer)
            : _first_act(first_action), _second_act(second_action), _first_act_priv_key(first_act_signer), _second_act_priv_key(second_act_signer) {}

      eosio::chain::action _first_act;
      eosio::chain::action _second_act;
      fc::crypto::private_key _first_act_priv_key;
      fc::crypto::private_key _second_act_priv_key;
   };

   signed_transaction_w_signer create_transfer_trx_w_signer(const action& act, const fc::crypto::private_key& priv_key, uint64_t& nonce_prefix, uint64_t& nonce, const fc::microseconds& trx_expiration, const chain_id_type& chain_id, const block_id_type& last_irr_block_id) {
      signed_transaction trx;
      trx.actions.push_back(act);
      trx.context_free_actions.emplace_back(action({}, config::null_account_name, name("nonce"),
         fc::raw::pack(std::to_string(nonce_prefix) + ":" + std::to_string(++nonce) + ":" +
         fc::time_point::now().time_since_epoch().count())));

      trx.set_reference_block(last_irr_block_id);
      trx.expiration = fc::time_point::now() + trx_expiration;
      trx.max_net_usage_words = 100;
      trx.sign(priv_key, chain_id);
      return signed_transaction_w_signer(trx, priv_key);
   }

   vector<signed_transaction_w_signer> create_initial_transfer_transactions(const vector<action_pair_w_keys>& action_pairs_vector, uint64_t& nonce_prefix, uint64_t& nonce, const fc::microseconds& trx_expiration, const chain_id_type& chain_id, const block_id_type& last_irr_block_id) {
      std::vector<signed_transaction_w_signer> trxs;
      trxs.reserve(2 * action_pairs_vector.size());

      for(action_pair_w_keys ap: action_pairs_vector) {
         trxs.emplace_back(create_transfer_trx_w_signer(ap._first_act, ap._first_act_priv_key, nonce_prefix, nonce, trx_expiration, chain_id, last_irr_block_id));
         trxs.emplace_back(create_transfer_trx_w_signer(ap._second_act, ap._second_act_priv_key, nonce_prefix, nonce, trx_expiration, chain_id, last_irr_block_id));
      }

      return trxs;
   }

   void update_resign_transaction(signed_transaction& trx, fc::crypto::private_key priv_key, uint64_t& nonce_prefix, uint64_t& nonce, const fc::microseconds& trx_expiration, const chain_id_type& chain_id, const block_id_type& last_irr_block_id) {
      trx.context_free_actions.clear();
      trx.context_free_actions.emplace_back(action({}, config::null_account_name, name("nonce"), fc::raw::pack(std::to_string(nonce_prefix) + ":" + std::to_string(++nonce) + ":" + fc::time_point::now().time_since_epoch().count())));
      trx.set_reference_block(last_irr_block_id);
      trx.expiration = fc::time_point::now() + trx_expiration;
      trx.signatures.clear();
      trx.sign(priv_key, chain_id);
   }

   chain::bytes make_transfer_data(const chain::name& from, const chain::name& to, const chain::asset& quantity, const std::string&& memo) {
      return fc::raw::pack<chain::name>(from, to, quantity, memo);
   }

   auto make_transfer_action(chain::name account, chain::name from, chain::name to, chain::asset quantity, std::string memo) {
      return chain::action(std::vector<chain::permission_level>{{from, chain::config::active_name}},
                           account, "transfer"_n, make_transfer_data(from, to, quantity, std::move(memo)));
   }

   vector<action_pair_w_keys> create_initial_transfer_actions(const std::string& salt, const uint64_t& period, const name& handler_acct, const vector<name>& accounts, const vector<fc::crypto::private_key>& priv_keys) {
      vector<action_pair_w_keys> actions_pairs_vector;

      for(size_t i = 0; i < accounts.size(); ++i) {
         for(size_t j = i + 1; j < accounts.size(); ++j) {
            //create the actions here
            ilog("create_initial_transfer_actions: creating transfer from ${acctA} to ${acctB}", ("acctA", accounts.at(i))("acctB", accounts.at(j)));
            action act_a_to_b = make_transfer_action(handler_acct, accounts.at(i), accounts.at(j), asset::from_string("1.0000 CUR"), salt);

            ilog("create_initial_transfer_actions: creating transfer from ${acctB} to ${acctA}", ("acctB", accounts.at(j))("acctA", accounts.at(i)));
            action act_b_to_a = make_transfer_action(handler_acct, accounts.at(j), accounts.at(i), asset::from_string("1.0000 CUR"), salt);

            actions_pairs_vector.push_back(action_pair_w_keys(act_a_to_b, act_b_to_a, priv_keys.at(i), priv_keys.at(j)));
         }
      }
      ilog("create_initial_transfer_actions: total action pairs created: ${pairs}", ("pairs", actions_pairs_vector.size()));
      return actions_pairs_vector;
   }

   transfer_trx_generator::transfer_trx_generator(std::string chain_id_in, std::string handler_acct,
      const std::vector<std::string>& accts, int64_t trx_expr, const std::vector<std::string>& private_keys_str_vector,
      std::string lib_id_str, std::string log_dir, bool stop_on_trx_failed, const std::string& peer_endpoint, unsigned short port) :
      _provider(peer_endpoint, port), _chain_id(chain_id_in), _handler_acct(handler_acct), _accts(accts),
      _trx_expiration(trx_expr*1000000), _private_keys_str_vector(private_keys_str_vector),
      _last_irr_block_id(fc::variant(lib_id_str).as<block_id_type>()), _log_dir(log_dir),
      _stop_on_trx_failed(stop_on_trx_failed) {
   }

   void log_first_trx(const std::string& log_dir, const chain::signed_transaction& trx) {
      std::ostringstream fileName;
      fileName << log_dir << "/first_trx_" << getpid() << ".txt";
      std::ofstream out(fileName.str());

      out << fc::string(trx.id()) << "\n";
      out.close();
   }

   void transfer_trx_generator::push_transaction(p2p_trx_provider& provider, signed_transaction_w_signer& trx, uint64_t& nonce_prefix, uint64_t& nonce, const fc::microseconds& trx_expiration, const chain_id_type& chain_id, const block_id_type& last_irr_block_id) {
      update_resign_transaction(trx._trx, trx._signer, ++nonce_prefix, nonce, trx_expiration, chain_id, last_irr_block_id);
      if (_txcount == 0) {
         log_first_trx(_log_dir, trx._trx);
      }
      provider.send(trx._trx);
   }

   void transfer_trx_generator::stop_generation() {
      ilog("Stopping transaction generation");

      if(_txcount) {
         ilog("${d} transactions executed, ${t}us / transaction", ("d", _txcount)("t", _total_us / (double) _txcount));
         _txcount = _total_us = 0;
      }
   }

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

      std::cout
            << "Create All Initial Transfer Action/Reaction Pairs (acct 1 -> acct 2, acct 2 -> acct 1) between all provided accounts."
            << std::endl;
      const auto action_pairs_vector = create_initial_transfer_actions(salt, period, _handler_acct, accounts,
                                                                       private_key_vector);

      std::cout
            << "Stop Generation (form potential ongoing generation in preparation for starting new generation run)."
            << std::endl;
      stop_generation();

      std::cout << "Create All Initial Transfer Transactions (one for each created action)." << std::endl;
      _trxs = create_initial_transfer_transactions(action_pairs_vector,
                                                   ++_nonce_prefix,
                                                   _nonce,
                                                   _trx_expiration,
                                                   _chain_id,
                                                   _last_irr_block_id);

      std::cout << "Setup p2p transaction provider" << std::endl;

      std::cout
            << "Update each trx to qualify as unique and fresh timestamps, re-sign trx, and send each updated transactions via p2p transaction provider"
            << std::endl;

      _provider.setup();
      return true;
   }

   bool transfer_trx_generator::tear_down() {
      _provider.log_trxs(_log_dir);
      _provider.teardown();

      std::cout << "Sent transactions: " << _txcount << std::endl;
      std::cout << "Tear down p2p transaction provider" << std::endl;

      //Stop & Cleanup
      std::cout << "Stop Generation." << std::endl;
      stop_generation();
      return true;
   }

   bool transfer_trx_generator::generate_and_send() {
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

   bool transfer_trx_generator::stop_on_trx_fail() {
      return _stop_on_trx_failed;
   }
}

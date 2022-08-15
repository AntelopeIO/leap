#include <eosio/chain_plugin/chain_plugin.hpp>
#include <trx_provider.hpp>

#include <boost/algorithm/string.hpp>

#include <fc/bitutil.hpp>
#include <fc/io/raw.hpp>

#include <contracts.hpp>

#include <iostream>

enum return_codes {
   OTHER_FAIL = -2,
   INITIALIZE_FAIL = -1,
   SUCCESS = 0,
   BAD_ALLOC = 1,
   DATABASE_DIRTY = 2,
   FIXED_REVERSIBLE = SUCCESS,
   EXTRACTED_GENESIS = SUCCESS,
   NODE_MANAGEMENT_SUCCESS = 5
};

uint64_t _total_us = 0;
uint64_t _txcount = 0;

using namespace eosio::testing;
using namespace eosio::chain;
using namespace eosio;

struct action_pair_w_keys {
   action_pair_w_keys(eosio::chain::action first_action, eosio::chain::action second_action, fc::crypto::private_key first_act_signer, fc::crypto::private_key second_act_signer)
       : _first_act(first_action), _second_act(), _first_act_priv_key(first_act_signer), _second_act_priv_key(second_act_signer) {}

   eosio::chain::action _first_act;
   eosio::chain::action _second_act;
   fc::crypto::private_key _first_act_priv_key;
   fc::crypto::private_key _second_act_priv_key;
};

struct signed_transaction_w_signer {
   signed_transaction_w_signer(signed_transaction trx, fc::crypto::private_key key) : _trx(move(trx)), _signer(key) {}

   signed_transaction _trx;
   fc::crypto::private_key _signer;
};

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

signed_transaction_w_signer create_transfer_trx_w_signer(const action& act, const fc::crypto::private_key& priv_key, uint64_t& nonce_prefix, uint64_t& nonce, const fc::microseconds& trx_expiration, const chain_id_type& chain_id, const block_id_type& last_irr_block_id) {
   signed_transaction trx;
   trx.actions.push_back(act);
   trx.context_free_actions.emplace_back(action({}, config::null_account_name, name("nonce"), fc::raw::pack(std::to_string(nonce_prefix) + ":" + std::to_string(++nonce) + ":" + fc::time_point::now().time_since_epoch().count())));
   trx.set_reference_block(last_irr_block_id);
   trx.expiration = fc::time_point::now() + trx_expiration;
   trx.max_net_usage_words = 100;
   trx.sign(priv_key, chain_id);
   return signed_transaction_w_signer(trx, priv_key);
}

vector<signed_transaction_w_signer> create_intial_transfer_transactions(const vector<action_pair_w_keys>& action_pairs_vector, uint64_t& nonce_prefix, uint64_t& nonce, const fc::microseconds& trx_expiration, const chain_id_type& chain_id, const block_id_type& last_irr_block_id) {
   std::vector<signed_transaction_w_signer> trxs;
   trxs.reserve(2 * action_pairs_vector.size());

   try {
      for(action_pair_w_keys ap: action_pairs_vector) {
         trxs.emplace_back(std::move(create_transfer_trx_w_signer(ap._first_act, ap._first_act_priv_key, nonce_prefix, nonce, trx_expiration, chain_id, last_irr_block_id)));
         trxs.emplace_back(std::move(create_transfer_trx_w_signer(ap._second_act, ap._second_act_priv_key, nonce_prefix, nonce, trx_expiration, chain_id, last_irr_block_id)));
      }
   } catch(const std::bad_alloc&) {
      throw;
   } catch(const boost::interprocess::bad_alloc&) {
      throw;
   } catch(const fc::exception&) {
      throw;
   } catch(const std::exception&) {
      throw;
   }

   return trxs;
}

void update_resign_transaction(signed_transaction& trx, fc::crypto::private_key priv_key, uint64_t& nonce_prefix, uint64_t& nonce, const fc::microseconds& trx_expiration, const chain_id_type& chain_id, const block_id_type& last_irr_block_id) {
   try {
      trx.context_free_actions.clear();
      trx.context_free_actions.emplace_back(action({}, config::null_account_name, name("nonce"), fc::raw::pack(std::to_string(nonce_prefix) + ":" + std::to_string(++nonce) + ":" + fc::time_point::now().time_since_epoch().count())));
      trx.set_reference_block(last_irr_block_id);
      trx.expiration = fc::time_point::now() + trx_expiration;
      trx.sign(priv_key, chain_id);
   } catch(const std::bad_alloc&) {
      throw;
   } catch(const boost::interprocess::bad_alloc&) {
      throw;
   } catch(const fc::exception&) {
      throw;
   } catch(const std::exception&) {
      throw;
   }
}

void push_transactions(p2p_trx_provider& provider, vector<signed_transaction_w_signer>& trxs, uint64_t& nonce_prefix, uint64_t& nonce, const fc::microseconds& trx_expiration, const chain_id_type& chain_id, const block_id_type& last_irr_block_id) {
   std::vector<signed_transaction> single_send;
   single_send.reserve(1);

   for(signed_transaction_w_signer& trx: trxs) {
      update_resign_transaction(trx._trx, trx._signer, ++nonce_prefix, nonce, trx_expiration, chain_id, last_irr_block_id);
      single_send.emplace_back(trx._trx);
      provider.send(single_send);
      single_send.clear();
      ++_txcount;
   }
}

void stop_generation() {
   ilog("Stopping transaction generation");

   if(_txcount) {
      ilog("${d} transactions executed, ${t}us / transaction", ("d", _txcount)("t", _total_us / (double) _txcount));
      _txcount = _total_us = 0;
   }
}

vector<name> get_accounts(const vector<string>& account_str_vector) {
   vector<name> acct_name_list;
   for(string account_name: account_str_vector) {
      ilog("get_account about to try to create name for ${acct}", ("acct", account_name));
      acct_name_list.push_back(eosio::chain::name(account_name));
   }
   return acct_name_list;
}

vector<fc::crypto::private_key> get_private_keys(const vector<string>& priv_key_str_vector) {
   vector<fc::crypto::private_key> key_list;
   for(const string& private_key: priv_key_str_vector) {
      ilog("get_private_keys about to try to create private_key for ${key} : gen key ${newKey}", ("key", private_key)("newKey", fc::crypto::private_key(private_key)));
      key_list.push_back(fc::crypto::private_key(private_key));
   }
   return key_list;
}

int main(int argc, char** argv) {
   const uint32_t TRX_EXPIRATION_MAX = 3600;
   variables_map vmap;
   options_description cli("Transaction Generator command line options.");
   string chain_id_in;
   string h_acct;
   string accts;
   string p_keys;
   uint32_t trx_expr;
   string lib_id_str;

   vector<string> account_str_vector;
   vector<string> private_keys_str_vector;


   cli.add_options()
      ("chain-id", bpo::value<string>(&chain_id_in), "set the chain id")
      ("handler-account", bpo::value<string>(&h_acct), "Account name of the handler account for the transfer actions")
      ("accounts", bpo::value<string>(&accts), "comma-separated list of accounts that will be used for transfers. Minimum required accounts: 2.")
      ("priv-keys", bpo::value<string>(&p_keys), "comma-separated list of private keys in same order of accounts list that will be used to sign transactions. Minimum required: 2.")
      ("trx-expiration", bpo::value<uint32_t>(&trx_expr)->default_value(3600), "transaction expiration time in microseconds (us). Defaults to 3,600. Maximum allowed: 3,600")
      ("last-irreversible-block-id", bpo::value<string>(&lib_id_str), "Current last-irreversible-block-id (LIB ID) to use for transactions.")
      ("help,h", "print this list")
      ;

   try {
      bpo::store(bpo::parse_command_line(argc, argv, cli), vmap);
      bpo::notify(vmap);

      if(vmap.count("help") > 0) {
         cli.print(std::cerr);
         return SUCCESS;
      }

      if(!vmap.count("chain-id")) {
         ilog("Initialization error: missing chain-id");
         cli.print(std::cerr);
         return INITIALIZE_FAIL;
      }

      if(!vmap.count("last-irreversible-block-id")) {
         ilog("Initialization error: missing last-irreversible-block-id");
         cli.print(std::cerr);
         return INITIALIZE_FAIL;
      }

      if(vmap.count("handler-account")) {
      } else {
         ilog("Initialization error: missing handler-account");
         cli.print(std::cerr);
         return INITIALIZE_FAIL;
      }

      if(vmap.count("accounts")) {
         boost::split(account_str_vector, accts, boost::is_any_of(","));
         if(account_str_vector.size() < 2) {
            ilog("Initialization error: requires at minimum 2 transfer accounts");
            cli.print(std::cerr);
            return INITIALIZE_FAIL;
         }
      } else {
         ilog("Initialization error: did not specify transfer accounts. requires at minimum 2 transfer accounts");
         cli.print(std::cerr);
         return INITIALIZE_FAIL;
      }

      if(vmap.count("priv-keys")) {
         boost::split(private_keys_str_vector, p_keys, boost::is_any_of(","));
         if(private_keys_str_vector.size() < 2) {
            ilog("Initialization error: requires at minimum 2 private keys");
            cli.print(std::cerr);
            return INITIALIZE_FAIL;
         }
      } else {
         ilog("Initialization error: did not specify accounts' private keys. requires at minimum 2 private keys");
         cli.print(std::cerr);
         return INITIALIZE_FAIL;
      }

      if(vmap.count("trx-expiration")) {
         if(trx_expr > TRX_EXPIRATION_MAX) {
            ilog("Initialization error: Exceeded max value for transaction expiration. Value must be less than ${max}.", ("max", TRX_EXPIRATION_MAX));
            cli.print(std::cerr);
            return INITIALIZE_FAIL;
         }
      }
   } catch(bpo::unknown_option& ex) {
      std::cerr << ex.what() << std::endl;
      cli.print(std::cerr);
      return INITIALIZE_FAIL;
   }

   try {
      ilog("Initial chain id ${chainId}", ("chainId", chain_id_in));
      ilog("Handler account ${acct}", ("acct", h_acct));
      ilog("Transfer accounts ${accts}", ("accts", accts));
      ilog("Account private keys ${priv_keys}", ("priv_keys", p_keys));
      ilog("Transaction expiration microsections ${expr}", ("expr", trx_expr));
      ilog("Reference LIB block id ${LIB}", ("LIB", lib_id_str));

      const chain_id_type chain_id(chain_id_in);
      const name handler_acct = eosio::chain::name(h_acct);
      const vector<name> accounts = get_accounts(account_str_vector);
      const vector<fc::crypto::private_key> private_key_vector = get_private_keys(private_keys_str_vector);
      fc::microseconds trx_expiration{trx_expr};

      const std::string salt = "";
      const uint64_t& period = 20;
      static uint64_t nonce_prefix = 0;
      static uint64_t nonce = static_cast<uint64_t>(fc::time_point::now().sec_since_epoch()) << 32;

      block_id_type last_irr_block_id = fc::variant(lib_id_str).as<block_id_type>();

      std::cout << "Create All Initial Transfer Action/Reaction Pairs (acct 1 -> acct 2, acct 2 -> acct 1) between all provided accounts." << std::endl;
      const auto action_pairs_vector = create_initial_transfer_actions(salt, period, handler_acct, accounts, private_key_vector);

      std::cout << "Stop Generation (form potential ongoing generation in preparation for starting new generation run)." << std::endl;
      stop_generation();

      std::cout << "Create All Initial Transfer Transactions (one for each created action)." << std::endl;
      std::vector<signed_transaction_w_signer> trxs = create_intial_transfer_transactions(action_pairs_vector, ++nonce_prefix, nonce, trx_expiration, chain_id, last_irr_block_id);

      std::cout << "Setup p2p transaction provider" << std::endl;
      p2p_trx_provider provider = p2p_trx_provider();
      provider.setup();

      std::cout << "Update each trx to qualify as unique and fresh timestamps, re-sign trx, and send each updated transactions via p2p transaction provider" << std::endl;
      push_transactions(provider, trxs, ++nonce_prefix, nonce, trx_expiration, chain_id, last_irr_block_id);

      std::cout << "Sent transactions: " << _txcount << std::endl;

      std::cout << "Tear down p2p transaction provider" << std::endl;
      provider.teardown();

      //Stop & Cleanup
      std::cout << "Stop Generation." << std::endl;
      stop_generation();

   } catch(const std::exception& e) {
      elog("${e}", ("e", e.what()));
      return OTHER_FAIL;
   } catch(...) {
      elog("unknown exception");
      return OTHER_FAIL;
   }

   return SUCCESS;
}

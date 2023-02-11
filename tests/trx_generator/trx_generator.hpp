#pragma once
#include <trx_provider.hpp>
#include <string>
#include <vector>
#include <appbase/plugin.hpp>
#include <boost/program_options.hpp>
#include <eosio/chain/transaction.hpp>

namespace eosio::testing {

   struct signed_transaction_w_signer {
      signed_transaction_w_signer(eosio::chain::signed_transaction trx, fc::crypto::private_key key) : _trx(move(trx)), _signer(key) {}

      eosio::chain::signed_transaction _trx;
      fc::crypto::private_key _signer;
   };

   struct trx_generator_base {
      p2p_trx_provider _provider;
      eosio::chain::chain_id_type _chain_id;
      eosio::chain::name _contract_owner_account;
      fc::microseconds _trx_expiration;
      eosio::chain::block_id_type _last_irr_block_id;
      std::string _log_dir;

      uint64_t _total_us = 0;
      uint64_t _txcount = 0;

      std::vector<signed_transaction_w_signer> _trxs;

      uint64_t _nonce = 0;
      uint64_t _nonce_prefix = 0;
      bool _stop_on_trx_failed = true;


      trx_generator_base(std::string chain_id_in, std::string contract_owner_account, fc::microseconds trx_expr, std::string lib_id_str, std::string log_dir, bool stop_on_trx_failed,
         const std::string& peer_endpoint="127.0.0.1", unsigned short port=9876);

      void push_transaction(p2p_trx_provider& provider, signed_transaction_w_signer& trx, uint64_t& nonce_prefix,
                            uint64_t& nonce, const fc::microseconds& trx_expiration, const eosio::chain::chain_id_type& chain_id,
                            const eosio::chain::block_id_type& last_irr_block_id);
      bool generate_and_send();
      bool tear_down();
      void stop_generation();
      bool stop_on_trx_fail();
   };

   struct transfer_trx_generator : public trx_generator_base {
      const std::vector<std::string> _accts;
      std::vector<std::string> _private_keys_str_vector;

      transfer_trx_generator(std::string chain_id_in, std::string contract_owner_account, const std::vector<std::string>& accts,
         fc::microseconds trx_expr, const std::vector<std::string>& private_keys_str_vector, std::string lib_id_str, std::string log_dir, bool stop_on_trx_failed,
         const std::string& peer_endpoint="127.0.0.1", unsigned short port=9876);

      std::vector<eosio::chain::name> get_accounts(const std::vector<std::string>& account_str_vector);
      std::vector<fc::crypto::private_key> get_private_keys(const std::vector<std::string>& priv_key_str_vector);

      bool setup();
   };

   struct trx_generator : public trx_generator_base{
      std::string _abi_data_file_path;
      eosio::chain::name _auth_account;
      eosio::chain::name _action;
      std::string _action_data_file_or_str;
      fc::crypto::private_key _private_key;
      fc::crypto::private_key _owner_private_key;

      const fc::microseconds abi_serializer_max_time = fc::seconds(10); // No risk to client side serialization taking a long time

      trx_generator(std::string chain_id_in, const std::string& abi_data_file, std::string contract_owner_account, const std::string& owner_private_key,
         std::string auth_account, std::string action_name, const std::string& action_data_file_or_str,
         fc::microseconds trx_expr, const std::string& private_key_str, std::string lib_id_str, std::string log_dir, bool stop_on_trx_failed,
         const std::string& peer_endpoint="127.0.0.1", unsigned short port=9876);

      bool setup();
   };
}

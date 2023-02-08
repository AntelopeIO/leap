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

   struct transfer_trx_generator {
      p2p_trx_provider _provider;
      eosio::chain::chain_id_type _chain_id;
      eosio::chain::name _handler_acct;
      const std::vector<std::string> _accts;
      fc::microseconds _trx_expiration;
      std::vector<std::string> _private_keys_str_vector;
      eosio::chain::block_id_type _last_irr_block_id;
      std::string _log_dir;

      uint64_t _total_us = 0;
      uint64_t _txcount = 0;

      std::vector<signed_transaction_w_signer> _trxs;

      uint64_t _nonce = 0;
      uint64_t _nonce_prefix = 0;
      bool _stop_on_trx_failed = true;


      transfer_trx_generator(std::string chain_id_in, std::string handler_acct, const std::vector<std::string>& accts,
         int64_t trx_expr, const std::vector<std::string>& private_keys_str_vector, std::string lib_id_str, std::string log_dir, bool stop_on_trx_failed,
         const std::string& peer_endpoint="127.0.0.1", unsigned short port=9876);

      void push_transaction(p2p_trx_provider& provider, signed_transaction_w_signer& trx, uint64_t& nonce_prefix,
                            uint64_t& nonce, const fc::microseconds& trx_expiration, const eosio::chain::chain_id_type& chain_id,
                            const eosio::chain::block_id_type& last_irr_block_id);

      std::vector<eosio::chain::name> get_accounts(const std::vector<std::string>& account_str_vector);
      std::vector<fc::crypto::private_key> get_private_keys(const std::vector<std::string>& priv_key_str_vector);

      bool stop_on_trx_fail();

      bool setup();
      bool tear_down();

      void stop_generation();
      bool generate_and_send();
   };
}

#pragma once

#include<vector>
#include<eosio/chain/transaction.hpp>
#include<eosio/chain/block.hpp>
#include<boost/asio/ip/tcp.hpp>
#include<fc/network/message_buffer.hpp>
#include<eosio/chain/thread_utils.hpp>

namespace eosio::testing {
   using send_buffer_type = std::shared_ptr<std::vector<char>>;

   struct p2p_connection {
      std::string _peer_endpoint;
      boost::asio::io_service _p2p_service;
      boost::asio::ip::tcp::socket _p2p_socket;
      unsigned short _peer_port;

      p2p_connection(const std::string& peer_endpoint, unsigned short peer_port) :
            _peer_endpoint(peer_endpoint), _p2p_service(), _p2p_socket(_p2p_service), _peer_port(peer_port) {}

      void connect();
      void disconnect();
      void send_transaction(const chain::packed_transaction& trx);
   };

   struct p2p_trx_provider {
      p2p_trx_provider(const std::string& peer_endpoint="127.0.0.1", unsigned short port=9876);

      void setup();
      void send(const std::vector<chain::signed_transaction>& trxs);
      void send(const chain::signed_transaction& trx);
      void teardown();

   private:
      p2p_connection _peer_connection;
   };

}
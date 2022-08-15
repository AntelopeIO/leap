#pragma once

#include<vector>
#include<eosio/chain/transaction.hpp>
#include<eosio/chain/block.hpp>
#include<boost/asio/ip/tcp.hpp>
#include<fc/network/message_buffer.hpp>
#include<eosio/chain/thread_utils.hpp>

namespace eosio::testing {

   using send_buffer_type = std::shared_ptr<std::vector<char>>;


   struct simple_trx_generator {
      void setup() {}
      void teardown() {}

      void generate(std::vector<chain::signed_transaction>& trxs, size_t requested) {

      }
   };

   template<typename G, typename I> struct simple_tps_tester {
      G trx_generator;
      I trx_provider;
      size_t num_trxs = 1;

      std::vector<chain::signed_transaction> trxs;

      void run() {
         trx_generator.setup();
         trx_provider.setup();

         trx_generator.generate(trxs, num_trxs);
         trx_provider.send(trxs);

         trx_provider.teardown();
         trx_generator.teardown();
      }
   };

   struct p2p_connection {
      std::string _peer_endpoint;
      boost::asio::io_service p2p_service;
      boost::asio::ip::tcp::socket p2p_socket;

      p2p_connection(std::string peer_endpoint) : _peer_endpoint(peer_endpoint), p2p_service(), p2p_socket(p2p_service) {}

      void connect();
      void disconnect();
      void send_transaction(const chain::packed_transaction& trx);
   };


   struct p2p_trx_provider {
      p2p_trx_provider(std::string peer_endpoint="http://localhost:8080");

      void setup();
      void send(const std::vector<chain::signed_transaction>& trxs);
      void teardown();

   private:
      p2p_connection _peer_connection;

   };

   template <typename T>
   struct timeboxed_trx_provider {
      T trx_provider;

      void setup() {
         trx_provider.setup();
      }

      void teardown() {
         trx_provider.teardown();
      }

      void send(const std::vector<chain::signed_transaction>& trxs) {
         // set timer
         trx_provider.send(trxs);
         // handle timeout or success
      }

   };
}
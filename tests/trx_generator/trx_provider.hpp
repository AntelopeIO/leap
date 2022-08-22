#pragma once

#include<vector>
#include<eosio/chain/transaction.hpp>
#include<eosio/chain/block.hpp>
#include<boost/asio/ip/tcp.hpp>
#include<fc/network/message_buffer.hpp>
#include<eosio/chain/thread_utils.hpp>
#include<chrono>

using namespace std::chrono_literals;

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

   using fc::time_point;

   struct tps_test_stats {
      uint32_t total_trxs;
      uint32_t trxs_left;

      time_point start_time;
      time_point expected_end_time;
   };

   struct simple_tps_monitor {
      bool monitor_test(const tps_test_stats& stats) {return true;}
   };

   struct null_trx_generator {
      void generate_and_send() {}
   };

   constexpr int64_t min_sleep_us = 100;

   template<typename G, typename M>
   struct trx_tps_tester {
      G _generator;
      M _monitor;

      uint32_t _gen_duration_seconds;
      uint32_t _target_tps;

      trx_tps_tester(G generator, M monitor, uint32_t gen_duration_seconds, uint32_t target_tps) :
            _generator(), _monitor(), _gen_duration_seconds(gen_duration_seconds), _target_tps(target_tps) {

      }

      void run() {
         tps_test_stats stats;

         stats.total_trxs = _gen_duration_seconds * _target_tps;
         stats.trxs_left = stats.total_trxs;
         stats.start_time = fc::time_point::now();
         stats.expected_end_time = stats.start_time + fc::microseconds{_gen_duration_seconds * std::chrono::microseconds(1s).count()};

         bool keep_running = true;
         fc::microseconds trx_interval{std::chrono::microseconds(1s).count() / _target_tps};

         fc::time_point last_run;
         fc::time_point next_run;

         while (keep_running) {
            last_run = fc::time_point::now();
            next_run = last_run + trx_interval;

            _generator.generate_and_send();
            stats.trxs_left--;

            keep_running = (_monitor.monitor_test(stats) && stats.trxs_left);

            if (keep_running) {
               fc::microseconds time_to_sleep{next_run - fc::time_point::now()};
               if (time_to_sleep.count() > min_sleep_us) {
                  std::this_thread::sleep_for(std::chrono::microseconds(time_to_sleep.count()));
               }
            }

         }
      }
   };
}
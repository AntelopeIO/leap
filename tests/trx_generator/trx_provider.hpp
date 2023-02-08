#pragma once

#include<vector>
#include<eosio/chain/transaction.hpp>
#include<eosio/chain/block.hpp>
#include<boost/asio/ip/tcp.hpp>
#include<fc/network/message_buffer.hpp>
#include<eosio/chain/thread_utils.hpp>
#include<chrono>
#include<thread>

using namespace std::chrono_literals;

namespace eosio::testing {
   using send_buffer_type = std::shared_ptr<std::vector<char>>;

   struct logged_trx_data {
      eosio::chain::transaction_id_type _trx_id;
      fc::time_point _sent_timestamp;

      logged_trx_data(eosio::chain::transaction_id_type trx_id, fc::time_point sent=fc::time_point::now()) :
         _trx_id(trx_id), _sent_timestamp(sent) {}
   };

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
      void log_trxs(const std::string& log_dir);
      void teardown();

   private:
      p2p_connection _peer_connection;
      std::vector<logged_trx_data> _sent_trx_data;
   };

   using fc::time_point;

   struct tps_test_stats {
      uint32_t          total_trxs = 0;
      uint32_t          trxs_left = 0;
      uint32_t          trxs_sent = 0;
      time_point        start_time;
      time_point        expected_end_time;
      time_point        last_run;
      time_point        next_run;
      int64_t           time_to_next_trx_us = 0;
      fc::microseconds  trx_interval;
      uint32_t          expected_sent;
   };

   constexpr int64_t min_sleep_us                  = 1;
   constexpr int64_t default_spin_up_time_us       = std::chrono::microseconds(1s).count();
   constexpr uint32_t default_max_lag_per          = 5;
   constexpr int64_t default_max_lag_duration_us  = std::chrono::microseconds(1s).count();

   struct null_tps_monitor {
      bool monitor_test(const tps_test_stats& stats) {return true;}
   };

   struct tps_performance_monitor {
      fc::microseconds                    _spin_up_time;
      uint32_t                            _max_lag_per;
      fc::microseconds                    _max_lag_duration_us;
      bool                                _terminated_early;
      std::optional<fc::time_point>       _violation_start_time;

      tps_performance_monitor(int64_t spin_up_time=default_spin_up_time_us, uint32_t max_lag_per=default_max_lag_per,
                              int64_t max_lag_duration_us=default_max_lag_duration_us) : _spin_up_time(spin_up_time),
                              _max_lag_per(max_lag_per), _max_lag_duration_us(max_lag_duration_us), _terminated_early(false) {}

      bool monitor_test(const tps_test_stats& stats);
      bool terminated_early() {return _terminated_early;}
   };

   template<typename G, typename M>
   struct trx_tps_tester {
      std::shared_ptr<G> _generator;
      std::shared_ptr<M> _monitor;

      uint32_t _gen_duration_seconds;
      uint32_t _target_tps;

      trx_tps_tester(std::shared_ptr<G> generator, std::shared_ptr<M> monitor, uint32_t gen_duration_seconds, uint32_t target_tps) :
            _generator(generator), _monitor(monitor),
               _gen_duration_seconds(gen_duration_seconds), _target_tps(target_tps) {

      }

      bool run() {
         if ((_target_tps) < 1 || (_gen_duration_seconds < 1)) {
            elog("target tps (${tps}) and duration (${dur}) must both be 1+", ("tps", _target_tps)("dur", _gen_duration_seconds));
            return false;
         }

         if (!_generator->setup()) {
            return false;
         }

         tps_test_stats stats;
         stats.trx_interval = fc::microseconds(std::chrono::microseconds(1s).count() / _target_tps);

         stats.total_trxs = _gen_duration_seconds * _target_tps;
         stats.trxs_left = stats.total_trxs;
         stats.start_time = fc::time_point::now();
         stats.expected_end_time = stats.start_time + fc::microseconds{_gen_duration_seconds * std::chrono::microseconds(1s).count()};
         stats.time_to_next_trx_us = 0;

         bool keep_running = true;

         while (keep_running) {
            stats.last_run = fc::time_point::now();
            stats.next_run = stats.start_time + fc::microseconds(stats.trx_interval.count() * (stats.trxs_sent+1));

            if (_generator->generate_and_send()) {
               stats.trxs_sent++;
            } else {
               elog("generator unable to create/send a transaction");
               if (_generator->stop_on_trx_fail()) {
                  elog("generator stopping due to trx failure to send.");
                  break;
               }
            }

            stats.expected_sent = ((stats.last_run - stats.start_time).count() / stats.trx_interval.count()) +1;
            stats.trxs_left--;

            keep_running = ((_monitor == nullptr || _monitor->monitor_test(stats)) && stats.trxs_left);

            if (keep_running) {
               fc::microseconds time_to_sleep{stats.next_run - fc::time_point::now()};
               if (time_to_sleep.count() >= min_sleep_us) {
                  std::this_thread::sleep_for(std::chrono::microseconds(time_to_sleep.count()));
               }
               stats.time_to_next_trx_us = time_to_sleep.count();
            }
         }

         _generator->tear_down();

         return true;
      }
   };
}
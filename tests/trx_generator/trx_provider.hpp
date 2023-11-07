#pragma once

#include<eosio/chain/transaction.hpp>
#include<eosio/chain/block.hpp>
#include<eosio/chain/thread_utils.hpp>

#include<boost/asio/ip/tcp.hpp>
#include<boost/asio/strand.hpp>

#include<chrono>
#include<thread>
#include<variant>
#include<vector>
#include<mutex>

using namespace std::chrono_literals;

namespace eosio::testing {
   using send_buffer_type = std::shared_ptr<std::vector<char>>;

   struct logged_trx_data {
      eosio::chain::transaction_id_type _trx_id;
      fc::time_point _timestamp;

      explicit logged_trx_data(eosio::chain::transaction_id_type trx_id, fc::time_point time_of_interest=fc::time_point::now()) :
         _trx_id(trx_id), _timestamp(time_of_interest) {}
   };

   struct provider_base_config {
      std::string    _peer_endpoint_type = "p2p";
      std::string    _peer_endpoint      = "127.0.0.1";
      unsigned short _port               = 9876;
      // Api endpoint not truly used for p2p connections as transactions are streamed directly to p2p endpoint
      std::string    _api_endpoint       = "/v1/chain/send_transaction2";

      std::string to_string() const {
         std::ostringstream ss;
         ss << "Provider base config endpoint type: " << _peer_endpoint_type << " peer_endpoint: " << _peer_endpoint
            << " port: " << _port << " api endpoint: " << _api_endpoint;
         return ss.str();
      }
   };

   struct acked_trx_trace_info {
      bool        _valid           = false;
      uint32_t    _block_num       = 0;
      uint32_t    _cpu_usage_us    = 0;
      uint32_t    _net_usage_words = 0;
      std::string _block_time      = "";

      std::string to_string() const {
         std::ostringstream ss;
         ss << "Acked Transaction Trace Info "
            << "valid: " << _valid << " block num: " << _block_num << " cpu usage us: " << _cpu_usage_us
            << " net usage words: " << _net_usage_words << " block time: " << _block_time;
         return ss.str();
      }
   };

   struct provider_connection {
      const provider_base_config&                                 _config;
      eosio::chain::named_thread_pool<struct provider_connection> _connection_thread_pool;

      std::mutex _trx_ack_map_lock;
      std::map<eosio::chain::transaction_id_type, fc::time_point> _trxs_ack_time_map;

      explicit provider_connection(const provider_base_config& provider_config)
          : _config(provider_config) {}

      virtual ~provider_connection() = default;

      void init_and_connect();
      void cleanup_and_disconnect();
      fc::time_point get_trx_ack_time(const eosio::chain::transaction_id_type& trx_id);
      void trx_acknowledged(const eosio::chain::transaction_id_type& trx_id, const fc::time_point& ack_time);

      virtual acked_trx_trace_info get_acked_trx_trace_info(const eosio::chain::transaction_id_type& trx_id) = 0;
      virtual void send_transaction(const chain::packed_transaction& trx) = 0;

    private:
      virtual void connect()    = 0;
      virtual void disconnect() = 0;
   };

   struct http_connection : public provider_connection {
      std::mutex                                                     _trx_info_map_lock;
      std::map<eosio::chain::transaction_id_type, acked_trx_trace_info> _acked_trx_trace_info_map;

      std::atomic<uint64_t> _acknowledged{0};
      std::atomic<uint64_t> _sent{0};
      std::atomic<uint64_t> _errors{0};

      explicit http_connection(const provider_base_config& provider_config)
          : provider_connection(provider_config) {}

      void send_transaction(const chain::packed_transaction& trx) final;
      void record_trx_info(const eosio::chain::transaction_id_type& trx_id, uint32_t block_num, uint32_t cpu_usage_us,
                           uint32_t net_usage_words, const std::string& block_time);
      acked_trx_trace_info get_acked_trx_trace_info(const eosio::chain::transaction_id_type& trx_id) override final;

    private:
      void connect() override final;
      void disconnect() override final;
      bool needs_response_trace_info();
      bool is_read_only_transaction();
   };

   struct p2p_connection : public provider_connection {
      boost::asio::ip::tcp::socket _p2p_socket;
      boost::asio::io_context::strand _strand;
      std::atomic<uint64_t> _sent_callback_num{0};
      std::atomic<uint64_t> _sent{0};


      explicit p2p_connection(const provider_base_config& provider_config)
          : provider_connection(provider_config)
          , _p2p_socket(_connection_thread_pool.get_executor())
          , _strand(_connection_thread_pool.get_executor()){}

      void send_transaction(const chain::packed_transaction& trx) final;

      acked_trx_trace_info get_acked_trx_trace_info(const eosio::chain::transaction_id_type& trx_id) override final;

    private:
      void connect() override final;
      void disconnect() override final;
   };

   struct trx_provider {
      explicit trx_provider(const provider_base_config& provider_config);

      void setup();
      void send(const chain::signed_transaction& trx);
      void log_trxs(const std::string& log_dir);
      void teardown();

    private:
      std::variant<std::monostate, http_connection, p2p_connection> _conn;
      provider_connection*         _peer_connection;
      std::vector<logged_trx_data> _sent_trx_data;
   };

   struct tps_test_stats {
      uint32_t         total_trxs = 0;
      uint32_t         trxs_left  = 0;
      uint32_t         trxs_sent  = 0;
      fc::time_point   start_time;
      fc::time_point   expected_end_time;
      fc::time_point   last_run;
      fc::time_point   next_run;
      int64_t          time_to_next_trx_us = 0;
      fc::microseconds trx_interval;
      uint32_t         expected_sent;
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

      explicit tps_performance_monitor(int64_t spin_up_time=default_spin_up_time_us, uint32_t max_lag_per=default_max_lag_per,
                              int64_t max_lag_duration_us=default_max_lag_duration_us) : _spin_up_time(spin_up_time),
                              _max_lag_per(max_lag_per), _max_lag_duration_us(max_lag_duration_us), _terminated_early(false) {}

      bool monitor_test(const tps_test_stats& stats);
      bool terminated_early() {return _terminated_early;}
   };

   struct trx_tps_tester_config {
      uint32_t _gen_duration_seconds;
      uint32_t _target_tps;

      std::string to_string() const {
         std::ostringstream ss;
         ss << "Trx Tps Tester Config: duration: " << _gen_duration_seconds << " target tps: " << _target_tps;
         return ss.str();
      };
   };

   template<typename G, typename M>
   struct trx_tps_tester {
      std::shared_ptr<G> _generator;
      std::shared_ptr<M> _monitor;
      trx_tps_tester_config _config;

      explicit trx_tps_tester(std::shared_ptr<G> generator, std::shared_ptr<M> monitor, const trx_tps_tester_config& tester_config) :
            _generator(generator), _monitor(monitor), _config(tester_config) {
      }

      bool run() {
         if ((_config._target_tps) < 1 || (_config._gen_duration_seconds < 1)) {
            elog("target tps (${tps}) and duration (${dur}) must both be 1+", ("tps", _config._target_tps)("dur", _config._gen_duration_seconds));
            return false;
         }

         if (!_generator->setup()) {
            return false;
         }

         tps_test_stats stats;
         stats.trx_interval = fc::microseconds(std::chrono::microseconds(1s).count() / _config._target_tps);

         stats.total_trxs = _config._gen_duration_seconds * _config._target_tps;
         stats.trxs_left = stats.total_trxs;
         stats.start_time = fc::time_point::now();
         stats.expected_end_time = stats.start_time + fc::microseconds{_config._gen_duration_seconds * std::chrono::microseconds(1s).count()};
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
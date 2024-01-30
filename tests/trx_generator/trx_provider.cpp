#include <trx_provider.hpp>
#include <http_client_async.hpp>

#include <fc/io/raw.hpp>
#include <fc/log/appender.hpp>
#include <fc/io/json.hpp>

#include <boost/asio.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <eosio/chain/exceptions.hpp>

namespace eosio::testing {
   using namespace boost::asio;
   using namespace std::literals::string_literals;
   using ip::tcp;

   constexpr auto message_header_size = sizeof(uint32_t);
   constexpr uint32_t packed_trx_which = 8; // this is the "which" for packed_transaction in the net_message variant

   static send_buffer_type create_send_buffer( const chain::packed_transaction& m ) {
      const uint32_t which_size = fc::raw::pack_size(chain::unsigned_int(packed_trx_which));
      const uint32_t payload_size = which_size + fc::raw::pack_size( m );
      const size_t buffer_size = message_header_size + payload_size;

      const char* const header = reinterpret_cast<const char* const>(&payload_size); // avoid variable size encoding of uint32_t


      auto send_buffer = std::make_shared<std::vector<char>>(buffer_size);
      fc::datastream<char*> ds( send_buffer->data(), buffer_size);
      ds.write( header, message_header_size );
      fc::raw::pack( ds, fc::unsigned_int(packed_trx_which));
      fc::raw::pack( ds, m );

      return send_buffer;
   }

   void provider_connection::init_and_connect() {
      _connection_thread_pool.start(1,
                                    [&](const fc::exception &e) {
                                       wlog("Exception in connection_thread: ${e}", ("e", e.to_detail_string()));
                                    });
      connect();
   };

   void provider_connection::cleanup_and_disconnect() {
      disconnect();
      _connection_thread_pool.stop();
   };

   fc::time_point provider_connection::get_trx_ack_time(const eosio::chain::transaction_id_type& trx_id) {
      fc::time_point              time_acked;
      std::lock_guard<std::mutex> lock(_trx_ack_map_lock);
      auto                        search = _trxs_ack_time_map.find(trx_id);
      if (search != _trxs_ack_time_map.end()) {
         time_acked = search->second;
      } else {
         elog("get_trx_ack_time - Transaction acknowledge time not found for transaction with id: ${id}",
              ("id", trx_id));
         time_acked = fc::time_point::min();
      }
      return time_acked;
   }

   void provider_connection::trx_acknowledged(const eosio::chain::transaction_id_type& trx_id,
                                              const fc::time_point&                    ack_time) {
      std::lock_guard<std::mutex> lock(_trx_ack_map_lock);
      _trxs_ack_time_map[trx_id] = ack_time;
   }

   void p2p_connection::connect() {
      ilog("Attempting P2P connection to ${ip}:${port}.", ("ip", _config._peer_endpoint)("port", _config._port));
      tcp::resolver r(_connection_thread_pool.get_executor());
      auto i = r.resolve(tcp::v4(), _config._peer_endpoint, std::to_string(_config._port));
      boost::asio::connect(_p2p_socket, i);
      ilog("Connected to ${ip}:${port}.", ("ip", _config._peer_endpoint)("port", _config._port));
   }

   void p2p_connection::disconnect() {
      int max    = 30;
      int waited = 0;
      for (uint64_t sent = _sent.load(), sent_callback_num = _sent_callback_num.load();
           sent != sent_callback_num && waited < max;
           sent = _sent.load(), sent_callback_num = _sent_callback_num.load()) {
         ilog("disconnect waiting on ack - sent ${s} | acked ${a} | waited ${w}",
              ("s", sent)("a", sent_callback_num)("w", waited));
         sleep(1);
         ++waited;
      }
      if (waited == max) {
         elog("disconnect failed to receive all acks in time - sent ${s} | acked ${a} | waited ${w}",
              ("s", _sent.load())("a", _sent_callback_num.load())("w", waited));
      }
   }

   void p2p_connection::send_transaction(const chain::packed_transaction& trx) {
      send_buffer_type msg = create_send_buffer(trx);

      ++_sent;
      _strand.post( [this, msg{std::move(msg)}, id{trx.id()}]() {
         boost::asio::write(_p2p_socket, boost::asio::buffer(*msg));
         trx_acknowledged(id, fc::time_point::min()); //using min to identify ack time as not applicable for p2p
         ++_sent_callback_num;
      } );
   }

   acked_trx_trace_info p2p_connection::get_acked_trx_trace_info(const eosio::chain::transaction_id_type& trx_id) {
      return {};
   }

   void http_connection::connect() {}

   void http_connection::disconnect() {
      int max    = 30;
      int waited = 0;
      for (uint64_t sent = _sent.load(), acknowledged = _acknowledged.load();
           sent != acknowledged && waited < max;
           sent = _sent.load(), acknowledged = _acknowledged.load()) {
         ilog("disconnect waiting on ack - sent ${s} | acked ${a} | waited ${w}",
              ("s", sent)("a", acknowledged)("w", waited));
         sleep(1);
         ++waited;
      }
      if (waited == max) {
         elog("disconnect failed to receive all acks in time - sent ${s} | acked ${a} | waited ${w}",
               ("s", _sent.load())("a", _acknowledged.load())("w", waited));
      }
      if (_errors.load()) {
         elog("${n} errors reported during http calls, see logs", ("n", _errors.load()));
      }
   }

   bool http_connection::needs_response_trace_info() {
      return is_read_only_transaction();
   }

   bool http_connection::is_read_only_transaction() {
      return _config._api_endpoint == "/v1/chain/send_read_only_transaction";
   }

   void http_connection::send_transaction(const chain::packed_transaction& trx) {
      const int         http_version = 11;
      const std::string content_type = "application/json"s;

      bool        retry                = false;
      bool        tx_rtn_failure_trace = true;
      auto        to_send              = fc::mutable_variant_object()("return_failure_trace", tx_rtn_failure_trace)("retry_trx", retry)("transaction", trx);
      std::string msg_body             = fc::json::to_string(to_send, fc::time_point::maximum());

      http_client_async::http_request_params params{_connection_thread_pool.get_executor(),
                                                    _config._peer_endpoint,
                                                    _config._port,
                                                    _config._api_endpoint,
                                                    http_version,
                                                    content_type};
      http_client_async::async_http_request(
          params, std::move(msg_body),
          [this, trx_id = trx.id()](boost::beast::error_code                                      ec,
                                    boost::beast::http::response<boost::beast::http::string_body> response) {
             trx_acknowledged(trx_id, fc::time_point::now());
             if (ec) {
                elog("http error: ${c}: ${m}", ("c", ec.value())("m", ec.message()));
                ++_errors;
                return;
             }

             if (this->needs_response_trace_info() && response.result() == boost::beast::http::status::ok) {
                bool exception = false;
                auto exception_handler = [this, &response, &exception](const fc::exception_ptr& ex) {
                   elog("Fail to parse JSON from string: ${string}", ("string", response.body()));
                   ++_errors;
                   exception = true;
                };
                try {
                   fc::variant resp_json = fc::json::from_string(response.body());
                   if (resp_json.is_object() && resp_json.get_object().contains("processed")) {
                      const auto& processed      = resp_json["processed"];
                      const auto& block_num      = processed["block_num"].as_uint64();
                      const auto& block_time     = processed["block_time"].as_string();
                      const auto& elapsed_time     = processed["elapsed"].as_uint64();
                      std::string status         = "failed";
                      uint32_t    net            = 0;
                      uint32_t    cpu            = 0;
                      if (processed.get_object().contains("receipt")) {
                         const auto& receipt = processed["receipt"];
                         if (receipt.is_object()) {
                            status = receipt["status"].as_string();
                            net    = receipt["net_usage_words"].as_uint64() * 8;
                            cpu    = receipt["cpu_usage_us"].as_uint64();
                         }
                         if (status == "executed") {
                            record_trx_info(trx_id, block_num, this->is_read_only_transaction() ? elapsed_time : cpu, net, block_time);
                         } else {
                            elog("async_http_request Transaction receipt status not executed: ${string}",
                                 ("string", response.body()));
                         }
                      } else {
                         elog("async_http_request Transaction failed, no receipt: ${string}",
                              ("string", response.body()));
                      }
                   } else {
                      elog("async_http_request Transaction failed, transaction not processed: ${string}",
                           ("string", response.body()));
                   }
                } CATCH_AND_CALL(exception_handler)
                if (exception)
                   return;
             }

             if (!(response.result() == boost::beast::http::status::accepted ||
                   response.result() == boost::beast::http::status::ok)) {
                elog("async_http_request Failed with response http status code: ${s}, response: ${r}",
                     ("s", response.result_int())("r", response.body()));
             }
             ++this->_acknowledged;
          });
      ++_sent;
   }

   void http_connection::record_trx_info(const eosio::chain::transaction_id_type& trx_id, uint32_t block_num,
                                         uint32_t cpu_usage_us, uint32_t net_usage_words,
                                         const std::string& block_time) {
      std::lock_guard<std::mutex> lock(_trx_info_map_lock);
      _acked_trx_trace_info_map.insert({trx_id, {true, block_num, cpu_usage_us, net_usage_words, block_time}});
   }

   acked_trx_trace_info http_connection::get_acked_trx_trace_info(const eosio::chain::transaction_id_type& trx_id) {
      acked_trx_trace_info        info;
      std::lock_guard<std::mutex> lock(_trx_info_map_lock);
      auto                        search = _acked_trx_trace_info_map.find(trx_id);
      if (search != _acked_trx_trace_info_map.end()) {
         info = search->second;
      } else {
         elog("get_acked_trx_trace_info - Acknowledged transaction trace info not found for transaction with id: ${id}", ("id", trx_id));
      }
      return info;
   }

   trx_provider::trx_provider(const provider_base_config& provider_config) {
      if (provider_config._peer_endpoint_type == "http") {
         _conn.emplace<http_connection>(provider_config);
         _peer_connection = &std::get<http_connection>(_conn);
      } else {
         _conn.emplace<p2p_connection>(provider_config);
         _peer_connection = &std::get<p2p_connection>(_conn);
      }
   }

   void trx_provider::setup() { _peer_connection->init_and_connect(); }

   void trx_provider::send(const chain::signed_transaction& trx) {
      chain::packed_transaction pt(trx);
      _peer_connection->send_transaction(pt);
      _sent_trx_data.push_back(logged_trx_data(trx.id()));
   }

   void trx_provider::log_trxs(const std::string& log_dir) {
      std::ostringstream fileName;
      fileName << log_dir << "/trx_data_output_" << getpid() << ".txt";
      std::ofstream out(fileName.str());

      for (const logged_trx_data& data : _sent_trx_data) {
         fc::time_point   acked = _peer_connection->get_trx_ack_time(data._trx_id);
         std::string      acked_str;
         fc::microseconds ack_round_trip_us;
         if (fc::time_point::min() == acked) {
            acked_str         = "NA";
            ack_round_trip_us = fc::microseconds(-1);
         } else {
            acked_str         = acked.to_iso_string();
            ack_round_trip_us = acked - data._timestamp;
         }
         out << std::string(data._trx_id) << "," << data._timestamp.to_iso_string() << "," << acked_str << ","
             << ack_round_trip_us.count();

         acked_trx_trace_info info = _peer_connection->get_acked_trx_trace_info(data._trx_id);
         if (info._valid) {
            out << "," << info._block_num << "," << info._cpu_usage_us << "," << info._net_usage_words << "," << info._block_time;
         }
         out << "\n";
      }
      out.close();
   }

   void trx_provider::teardown() {
      _peer_connection->cleanup_and_disconnect();
   }

   bool tps_performance_monitor::monitor_test(const tps_test_stats &stats) {
      if ((!stats.expected_sent) || (stats.last_run - stats.start_time < _spin_up_time)) {
         return true;
      }

      int32_t trxs_behind = stats.expected_sent - stats.trxs_sent;
      if (trxs_behind < 1) {
         return true;
      }

      uint32_t per_off = (100*trxs_behind) / stats.expected_sent;

      if (per_off > _max_lag_per) {
         if (_violation_start_time.has_value()) {
           auto lag_duration_us = stats.last_run - _violation_start_time.value();
           if (lag_duration_us > _max_lag_duration_us) {
               elog("Target tps lagging outside of defined limits. Terminating test");
               elog("Expected=${expected}, Sent=${sent}, Percent off=${per_off}, Violation start=${vstart} ",
                    ("expected",  stats.expected_sent)
                    ("sent", stats.trxs_sent)
                    ("per_off", per_off)
                    ("vstart", _violation_start_time));
               _terminated_early = true;
               return false;
           }
         } else {
            _violation_start_time.emplace(stats.last_run);
         }
      } else {
         if (_violation_start_time.has_value()) {
            _violation_start_time.reset();
         }
      }
      return true;
   }
}

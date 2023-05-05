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

   void p2p_connection::connect() {
      ilog("Attempting P2P connection to ${ip}:${port}.", ("ip", _config._peer_endpoint)("port", _config._port));
      tcp::resolver r(_connection_thread_pool.get_executor());
      auto i = r.resolve(tcp::v4(), _config._peer_endpoint, std::to_string(_config._port));
      boost::asio::connect(_p2p_socket, i);
      ilog("Connected to ${ip}:${port}.", ("ip", _config._peer_endpoint)("port", _config._port));
   }

   void p2p_connection::disconnect() {
      ilog("Closing socket.");
      _p2p_socket.close();
      ilog("Socket closed.");
   }

   void p2p_connection::send_transaction(const chain::packed_transaction& trx) {
      send_buffer_type msg = create_send_buffer(trx);
      _p2p_socket.send(boost::asio::buffer(*msg));
   }

   void http_connection::connect() {}

   void http_connection::disconnect() {
      int max    = 30;
      int waited = 0;
      while (_sent.load() != _acknowledged.load() && waited < max) {
         ilog("http_connection::disconnect waiting on ack - sent ${s} | acked ${a} | waited ${w}",
              ("s", _sent.load())("a", _acknowledged.load())("w", waited));
         sleep(1);
         ++waited;
      }
      if (waited == max) {
         elog("http_connection::disconnect failed to receive all acks in time - sent ${s} | acked ${a} | waited ${w}",
               ("s", _sent.load())("a", _acknowledged.load())("w", waited));
      }
   }

   void http_connection::send_transaction(const chain::packed_transaction& trx) {
      const std::string target       = "/v1/chain/send_transaction2"s;
      const int         http_version = 11;
      const std::string content_type = "application/json"s;

      bool        retry                = false;
      bool        tx_rtn_failure_trace = true;
      auto        to_send              = fc::mutable_variant_object()("return_failure_trace", tx_rtn_failure_trace)("retry_trx", retry)("transaction", trx);
      std::string msg_body             = fc::json::to_string(to_send, fc::time_point::maximum());

      http_client_async::http_request_params params{_connection_thread_pool.get_executor(),
                                                    _config._peer_endpoint,
                                                    _config._port,
                                                    target,
                                                    http_version,
                                                    content_type};
      http_client_async::async_http_request(
          params, std::move(msg_body),
          [msg_body, &acked = _acknowledged](boost::beast::error_code                                      ec,
                                             boost::beast::http::response<boost::beast::http::string_body> response) {
             ++acked;
             if (response.result() != boost::beast::http::status::accepted) {
                elog("async_http_request Failed with response http status code: ${status}", ("status", response.result_int()));
             }
          });
      ++_sent;
   }

   trx_provider::trx_provider(const provider_base_config& provider_config)
       : _http_conn(provider_config)
       , _p2p_conn(provider_config) {
      if (provider_config._peer_endpoint_type == "http") {
         _peer_connection = &_http_conn;
      } else {
         _peer_connection = &_p2p_conn;
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

      for (logged_trx_data data : _sent_trx_data) {
         out << std::string(data._trx_id) << ","<< data._sent_timestamp.to_iso_string() << "\n";
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

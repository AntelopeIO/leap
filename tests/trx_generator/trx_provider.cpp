#include <trx_provider.hpp>

#include <fc/io/raw.hpp>
#include <fc/log/appender.hpp>

#include <boost/asio.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <eosio/chain/exceptions.hpp>

namespace eosio::testing {
   using namespace boost::asio;
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
      tcp::resolver r(_p2p_service);
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

   p2p_trx_provider::p2p_trx_provider(const provider_base_config& provider_config) :
      _peer_connection(provider_config) {

   }

   void p2p_trx_provider::setup() {
      _peer_connection.connect();
   }

   void p2p_trx_provider::send(const chain::signed_transaction& trx) {
      chain::packed_transaction pt(trx);
      _peer_connection.send_transaction(pt);
      _sent_trx_data.push_back(logged_trx_data(trx.id()));
   }

   void p2p_trx_provider::send(const std::vector<chain::signed_transaction>& trxs) {
      for(const auto& t : trxs ){
         send(t);
      }
   }

   void p2p_trx_provider::log_trxs(const std::string& log_dir) {
      std::ostringstream fileName;
      fileName << log_dir << "/trx_data_output_" << getpid() << ".txt";
      std::ofstream out(fileName.str());

      for (logged_trx_data data : _sent_trx_data) {
         out << std::string(data._trx_id) << ","<< std::string(data._sent_timestamp) << "\n";
      }
      out.close();
   }

   void p2p_trx_provider::teardown() {
      _peer_connection.disconnect();
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

#include <trx_provider.hpp>

#include <fc/network/message_buffer.hpp>
#include <fc/network/ip.hpp>
#include <fc/io/json.hpp>
#include <fc/io/raw.hpp>
#include <fc/log/appender.hpp>
#include <fc/log/logger_config.hpp>
#include <fc/log/trace.hpp>
#include <fc/reflect/variant.hpp>
#include <fc/crypto/rand.hpp>
#include <fc/exception/exception.hpp>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ip/host_name.hpp>
#include <boost/asio/steady_timer.hpp>

using std::string;
using std::vector;
using namespace eosio;

namespace eosio::testing {
   using namespace boost::asio;
   using ip::tcp;

   void p2p_connection::connect() {
      p2p_socket.connect( tcp::endpoint( boost::asio::ip::address::from_string(_peer_endpoint), 8090));
      boost::system::error_code error;
   }

   void p2p_connection::disconnect() {
      p2p_socket.close();
   }

   constexpr auto     message_header_size = sizeof(uint32_t);

   static send_buffer_type create_send_buffer( const chain::packed_transaction& m ) {
      const uint32_t payload_size = fc::raw::pack_size( m );

      const char* const header = reinterpret_cast<const char* const>(&payload_size); // avoid variable size encoding of uint32_t
      const size_t buffer_size = message_header_size + payload_size;

      auto send_buffer = std::make_shared<vector<char>>(buffer_size);
      fc::datastream<char*> ds( send_buffer->data(), buffer_size);
      ds.write( header, message_header_size );
      fc::raw::pack( ds, fc::unsigned_int(8));
      fc::raw::pack( ds, m );

      return send_buffer;
   }

   void p2p_connection::send_transaction(const chain::packed_transaction& trx) {
      send_buffer_type msg = create_send_buffer(trx);
      p2p_socket.send(boost::asio::buffer(*msg));
   }

   p2p_trx_provider::p2p_trx_provider(std::string peer_endpoint) : _peer_connection(peer_endpoint) {

   }

   void p2p_trx_provider::setup() {
      _peer_connection.connect();
   }

   void p2p_trx_provider::send(const std::vector<chain::signed_transaction>& trxs) {
      for(const auto& t : trxs ){
         chain::packed_transaction pt(t);
         _peer_connection.send_transaction(pt);
      }
   }

  void p2p_trx_provider::teardown() {
      _peer_connection.disconnect();
  }

}


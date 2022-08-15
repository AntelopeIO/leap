#include <trx_provider.hpp>

#include <fc/network/ip.hpp>
#include <fc/io/raw.hpp>
#include <fc/log/appender.hpp>

#include <boost/asio/ip/tcp.hpp>

using std::string;
using std::vector;
using namespace eosio;

namespace eosio::testing {
   using namespace boost::asio;
   using ip::tcp;

   constexpr auto message_header_size = sizeof(uint32_t);
   constexpr uint32_t packed_trx_which = 8; // this is the "which" for packed_transaction in the net_message variant

   static send_buffer_type create_send_buffer( const chain::packed_transaction& m ) {
      const uint32_t payload_size = fc::raw::pack_size( m );

      const char* const header = reinterpret_cast<const char* const>(&payload_size); // avoid variable size encoding of uint32_t
      const size_t buffer_size = message_header_size + payload_size;

      auto send_buffer = std::make_shared<vector<char>>(buffer_size);
      fc::datastream<char*> ds( send_buffer->data(), buffer_size);
      ds.write( header, message_header_size );
      fc::raw::pack( ds, fc::unsigned_int(packed_trx_which));
      fc::raw::pack( ds, m );

      return send_buffer;
   }

   void p2p_connection::connect() {
      _p2p_socket.connect(tcp::endpoint(boost::asio::ip::address::from_string(_peer_endpoint), _peer_port));
   }

   void p2p_connection::disconnect() {
      _p2p_socket.close();
   }

   void p2p_connection::send_transaction(const chain::packed_transaction& trx) {
      send_buffer_type msg = create_send_buffer(trx);
      _p2p_socket.send(boost::asio::buffer(*msg));
   }

   p2p_trx_provider::p2p_trx_provider(const std::string& peer_endpoint, unsigned short peer_port) :
      _peer_connection(peer_endpoint, peer_port) {

   }

   void p2p_trx_provider::setup() {
      _peer_connection.connect();
   }

   void p2p_trx_provider::send(const chain::signed_transaction& trx) {
      chain::packed_transaction pt(trx);
      _peer_connection.send_transaction(pt);
   }

   void p2p_trx_provider::send(const std::vector<chain::signed_transaction>& trxs) {
      for(const auto& t : trxs ){
         send(t);
      }
   }

  void p2p_trx_provider::teardown() {
      _peer_connection.disconnect();
  }

}

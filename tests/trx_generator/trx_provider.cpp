#include <trx_provider.hpp>

#include <fc/network/ip.hpp>
#include <fc/io/raw.hpp>
#include <fc/log/appender.hpp>

#include <boost/asio/ip/tcp.hpp>
#include <eosio/chain/exceptions.hpp>

using std::string;
using std::vector;
using namespace eosio;

namespace eosio::testing {
   using namespace boost::asio;
   using ip::tcp;

   constexpr auto message_header_size = sizeof(uint32_t);
   constexpr uint32_t packed_trx_which = 8; // this is the "which" for packed_transaction in the net_message variant

   static send_buffer_type create_send_buffer( const chain::packed_transaction& m ) {
      const uint32_t which_size = fc::raw::pack_size(chain::unsigned_int(packed_trx_which));
      const uint32_t payload_size = which_size + fc::raw::pack_size( m );
      const size_t buffer_size = message_header_size + payload_size;

      ilog("Creating transaction buffer which size=${wsiz}, payload size=${psiz}, buffer size=${bsiz}",
           ("wsiz", which_size)("psiz", payload_size)("bsiz", buffer_size));
      const char* const header = reinterpret_cast<const char* const>(&payload_size); // avoid variable size encoding of uint32_t


      auto send_buffer = std::make_shared<vector<char>>(buffer_size);
      fc::datastream<char*> ds( send_buffer->data(), buffer_size);
      ds.write( header, message_header_size );
      fc::raw::pack( ds, fc::unsigned_int(packed_trx_which));
      fc::raw::pack( ds, m );

      return send_buffer;
   }

   void p2p_connection::connect() {
      ilog("Attempting P2P connection to ${ip}:${port}.", ("ip", _peer_endpoint)("port", _peer_port));
      _p2p_socket.connect(tcp::endpoint(boost::asio::ip::address::from_string(_peer_endpoint), _peer_port));
      ilog("Connected to ${ip}:${port}.", ("ip", _peer_endpoint)("port", _peer_port));
   }

   void p2p_connection::disconnect() {
      ilog("Closing socket.");
      _p2p_socket.close();
      ilog("Socket closed.");
   }

   void p2p_connection::send_transaction(const chain::packed_transaction& trx) {
      send_buffer_type msg = create_send_buffer(trx);
      ilog("Sending packed transaction ${trxid}", ("trxid", trx.id()));
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

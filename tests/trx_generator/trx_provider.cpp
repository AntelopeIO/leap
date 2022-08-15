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

   void p2p_connection::connect() {

   }

   void p2p_connection::disconnect() {

   }

   void p2p_connection::send_transaction(const chain::packed_transaction& trx) {

   }

   p2p_trx_provider::p2p_trx_provider(std::string peer_endpoint) : _peer_connection(peer_endpoint) {

   }

   void p2p_trx_provider::setup() {
      _peer_connection.connect();
   }

   void p2p_trx_provider::send(const std::vector<chain::signed_transaction>& trxs) {
      for(const auto& t : trxs ){
         packed_transaction pt(t);
         net_message msg{std::move(pt)};

         _peer_connection.send_transaction(pt);
      }
   }

  void p2p_trx_provider::teardown() {
      _peer_connection.disconnect();
  }

}


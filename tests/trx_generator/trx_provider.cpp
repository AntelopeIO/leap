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
using boost::asio::ip::tcp;
using boost::asio::ip::address_v4;
using boost::asio::ip::host_name;
using namespace eosio;

namespace eosio::testing {

   connection::connection(std::shared_ptr<eosio::chain::named_thread_pool> thread_pool, const string &endpoint)
         : threads(thread_pool), peer_addr(endpoint),
           strand(thread_pool->get_executor()),
           socket(new tcp::socket(thread_pool->get_executor())),
           log_p2p_address(endpoint),
           connection_id(++my_impl->current_connection_id),
           response_expected_timer(thread_pool->get_executor()),
           last_handshake_recv(),
           last_handshake_sent() {
      // fc_ilog(logger, "created connection ${c} to ${n}", ("c", connection_id)("n", endpoint));
   }

   connection::connection(std::shared_ptr<eosio::chain::named_thread_pool> thread_pool)
         : peer_addr(),
           strand(thread_pool->get_executor()),
           socket(new tcp::socket(thread_pool->get_executor())),
           connection_id(++my_impl->current_connection_id),
           response_expected_timer(thread_pool->get_executor()),
           last_handshake_recv(),
           last_handshake_sent() {
      // fc_dlog(logger, "new connection object created");
   }

// called from connection strand
   void connection::update_endpoints() {
      boost::system::error_code ec;
      boost::system::error_code ec2;
      auto rep = socket->remote_endpoint(ec);
      auto lep = socket->local_endpoint(ec2);
      log_remote_endpoint_ip = ec ? unknown : rep.address().to_string();
      log_remote_endpoint_port = ec ? unknown : std::to_string(rep.port());
      local_endpoint_ip = ec2 ? unknown : lep.address().to_string();
      local_endpoint_port = ec2 ? unknown : std::to_string(lep.port());
      std::lock_guard<std::mutex> g_conn(conn_mtx);
      remote_endpoint_ip = log_remote_endpoint_ip;
   }

// called from connection strand
   void connection::set_connection_type(const string &peer_add) {
      // host:port:[<trx>|<blk>]
      string::size_type colon = peer_add.find(':');
      string::size_type colon2 = peer_add.find(':', colon + 1);
      string::size_type end = colon2 == string::npos
                              ? string::npos : peer_add.find_first_of(" :+=.,<>!$%^&(*)|-#@\t", colon2 +
                                                                                                1); // future proof by including most symbols without using regex
      string host = peer_add.substr(0, colon);
      string port = peer_add.substr(colon + 1, colon2 == string::npos ? string::npos : colon2 - (colon + 1));
      string type = colon2 == string::npos ? "" : end == string::npos ?
                                                  peer_add.substr(colon2 + 1) : peer_add.substr(colon2 + 1,
                                                                                                end - (colon2 + 1));

      if (type.empty()) {
        /* fc_dlog(logger, "Setting connection ${c} type for: ${peer} to both transactions and blocks",
                 ("c", connection_id)("peer", peer_add));*/
         connection_type = both;
      } else if (type == "trx") {
         /*fc_dlog(logger, "Setting connection ${c} type for: ${peer} to transactions only",
                 ("c", connection_id)("peer", peer_add));*/
         connection_type = transactions_only;
      } else if (type == "blk") {
         /*fc_dlog(logger, "Setting connection ${c} type for: ${peer} to blocks only",
                 ("c", connection_id)("peer", peer_add)); */
         connection_type = blocks_only;
      } else {
         /* fc_wlog(logger, "Unknown connection ${c} type: ${t}, for ${peer}",
                 ("c", connection_id)("t", type)("peer", peer_add)); */
      }
   }

// called from connection stand
   bool connection::start_session() {
      verify_strand_in_this_thread(strand, __func__, __LINE__);

      update_endpoints();
      boost::asio::ip::tcp::no_delay nodelay(true);
      boost::system::error_code ec;
      socket->set_option(nodelay, ec);
      if (ec) {
//      peer_elog( this, "connection failed (set_option): ${e1}", ( "e1", ec.message() ) );
         close();
         return false;
      } else {
//      peer_dlog( this, "connected" );
         socket_open = true;
         start_read_message();
         return true;
      }
   }

   bool connection::connected() {
      return socket_is_open() && !connecting;
   }

   bool connection::current() {
      return (connected() && !syncing);
   }

   void connection::flush_queues() {
      buffer_queue.clear_write_queue();
   }

   void connection::close(bool reconnect, bool shutdown) {
      strand.post([self = shared_from_this(), reconnect, shutdown]() {
         connection::_close(self.get(), reconnect, shutdown);
      });
   }

// called from connection strand
   void connection::_close(connection *self, bool reconnect, bool shutdown) {
      self->socket_open = false;
      boost::system::error_code ec;
      if (self->socket->is_open()) {
         self->socket->shutdown(tcp::socket::shutdown_both, ec);
         self->socket->close(ec);
      }
      self->socket.reset(new tcp::socket(self->threads->get_executor()));
      self->flush_queues();
      self->connecting = false;
      self->syncing = false;
      ++self->consecutive_immediate_connection_close;
      bool has_last_req = false;
      {
         std::lock_guard<std::mutex> g_conn(self->conn_mtx);
         has_last_req = self->last_req.has_value();
         self->last_handshake_recv = handshake_message();
         self->last_handshake_sent = handshake_message();
         self->last_close = fc::time_point::now();
         self->conn_node_id = fc::sha256();
      }
      if (has_last_req && !shutdown) {
         my_impl->dispatcher->retry_fetch(self->shared_from_this());
      }
      self->peer_requested.reset();
      self->sent_handshake_count = 0;
      if (!shutdown) my_impl->sync_master->sync_reset_lib_num(self->shared_from_this(), true);
      peer_ilog(self, "closing");
      self->cancel_wait();

      if (reconnect && !shutdown) {
         my_impl->start_conn_timer(std::chrono::milliseconds(100), connection_wptr());
      }
   }

   void connection::stop_send() {
      syncing = false;
   }


// called from connection strand
   void connection::send_time() {
      time_message xpkt;
      xpkt.org = rec;
      xpkt.rec = dst;
      xpkt.xmt = get_time();
      org = xpkt.xmt;
      enqueue(xpkt);
   }

// called from connection strand
   void connection::send_time(const time_message &msg) {
      time_message xpkt;
      xpkt.org = msg.xmt;
      xpkt.rec = msg.dst;
      xpkt.xmt = get_time();
      enqueue(xpkt);
   }

// called from connection strand
   void connection::queue_write(const std::shared_ptr<vector<char>> &buff,
                                std::function<void(boost::system::error_code, std::size_t)> callback,
                                bool to_sync_queue) {
      if (!buffer_queue.add_write_queue(buff, callback, to_sync_queue)) {
         /* peer_wlog(this, "write_queue full ${s} bytes, giving up on connection",
                   ("s", buffer_queue.write_queue_size())); */
         close();
         return;
      }
      do_queue_write();
   }

// called from connection strand
   void connection::do_queue_write() {
      if (!buffer_queue.ready_to_send())
         return;
      connection_ptr c(shared_from_this());

      std::vector<boost::asio::const_buffer> bufs;
      buffer_queue.fill_out_buffer(bufs);

      strand.post([c{std::move(c)}, bufs{std::move(bufs)}]() {
         boost::asio::async_write(*c->socket, bufs,
                                  boost::asio::bind_executor(c->strand,
                                                             [c, socket = c->socket](boost::system::error_code ec,
                                                                                     std::size_t w) {
                                                                try {
                                                                   c->buffer_queue.clear_out_queue();
                                                                   // May have closed connection and cleared buffer_queue
                                                                   if (!c->socket_is_open() || socket != c->socket) {
                                                                      peer_ilog(c,
                                                                                "async write socket ${r} before callback",
                                                                                ("r", c->socket_is_open() ? "changed"
                                                                                                          : "closed"));
                                                                      c->close();
                                                                      return;
                                                                   }

                                                                   if (ec) {
                                                                      if (ec.value() != boost::asio::error::eof) {
                                                                         peer_elog(c, "Error sending to peer: ${i}",
                                                                                   ("i", ec.message()));
                                                                      } else {
                                                                         peer_wlog(c,
                                                                                   "connection closure detected on write");
                                                                      }
                                                                      c->close();
                                                                      return;
                                                                   }

                                                                   c->buffer_queue.out_callback(ec, w);

                                                                   c->enqueue_sync_block();
                                                                   c->do_queue_write();
                                                                } catch (const std::bad_alloc &) {
                                                                   throw;
                                                                } catch (const boost::interprocess::bad_alloc &) {
                                                                   throw;
                                                                } catch (const fc::exception &ex) {
                                                                   peer_elog(c, "fc::exception in do_queue_write: ${s}",
                                                                             ("s", ex.to_string()));
                                                                } catch (const std::exception &ex) {
                                                                   peer_elog(c,
                                                                             "std::exception in do_queue_write: ${s}",
                                                                             ("s", ex.what()));
                                                                } catch (...) {
                                                                   peer_elog(c, "Unknown exception in do_queue_write");
                                                                }
                                                             }));
      });
   }

}
using send_buffer_type = std::shared_ptr<std::vector<char>>;

struct buffer_factory {

   /// caches result for subsequent calls, only provide same net_message instance for each invocation
   const send_buffer_type& get_send_buffer( const net_message& m ) {
      if( !send_buffer ) {
         send_buffer = create_send_buffer( m );
      }
      return send_buffer;
   }

protected:
   send_buffer_type send_buffer;

protected:
   static send_buffer_type create_send_buffer( const net_message& m ) {
      const uint32_t payload_size = fc::raw::pack_size( m );

      const char* const header = reinterpret_cast<const char* const>(&payload_size); // avoid variable size encoding of uint32_t
      const size_t buffer_size = message_header_size + payload_size;

      auto send_buffer = std::make_shared<vector<char>>(buffer_size);
      fc::datastream<char*> ds( send_buffer->data(), buffer_size);
      ds.write( header, message_header_size );
      fc::raw::pack( ds, m );

      return send_buffer;
   }

   template< typename T>
   static send_buffer_type create_send_buffer( uint32_t which, const T& v ) {
      // match net_message static_variant pack
      const uint32_t which_size = fc::raw::pack_size( unsigned_int( which ) );
      const uint32_t payload_size = which_size + fc::raw::pack_size( v );

      const char* const header = reinterpret_cast<const char* const>(&payload_size); // avoid variable size encoding of uint32_t
      const size_t buffer_size = message_header_size + payload_size;

      auto send_buffer = std::make_shared<vector<char>>( buffer_size );
      fc::datastream<char*> ds( send_buffer->data(), buffer_size );
      ds.write( header, message_header_size );
      fc::raw::pack( ds, unsigned_int( which ) );
      fc::raw::pack( ds, v );

      return send_buffer;
   }

};

struct trx_buffer_factory : public buffer_factory {

   /// caches result for subsequent calls, only provide same packed_transaction_ptr instance for each invocation.
   const send_buffer_type& get_send_buffer( const packed_transaction_ptr& trx ) {
      if( !send_buffer ) {
         send_buffer = create_send_buffer( trx );
      }
      return send_buffer;
   }

private:

   static std::shared_ptr<std::vector<char>> create_send_buffer( const packed_transaction_ptr& trx ) {
      static_assert( packed_transaction_which == fc::get_index<net_message, packed_transaction>() );
      // this implementation is to avoid copy of packed_transaction to net_message
      // matches which of net_message for packed_transaction
      return buffer_factory::create_send_buffer( packed_transaction_which, *trx );
   }
};

namespace eosio::testing {

   p2p_trx_provider::p2p_trx_provider(std::shared_ptr<named_thread_pool> tp, std::string peer_endpoint) {
      _peer_connection = std::make_shared<connection>(tp, peer_endpoint);
   }

   void p2p_trx_provider::setup() {
      _peer_connection->resolve_and_connect();
   }

   void p2p_trx_provider::send(const std::vector<chain::signed_transaction>& trxs) {
      for(const auto& t : trxs ){
         packed_transaction pt(t);
         net_message msg{std::move(pt)};

         _peer_connection->enqueue(msg);
      }
   }

  void p2p_trx_provider::teardown() {
      _peer_connection->close();
  }

}
using namespace eosio::testing;

int main(int argc, char** argv)  {
  simple_tps_tester<simple_trx_generator, p2p_trx_provider> tester;

  tester.run();
}

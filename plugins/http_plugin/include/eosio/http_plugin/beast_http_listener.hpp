#pragma once

#include <eosio/http_plugin/beast_http_session.hpp>
#include <eosio/http_plugin/common.hpp>
#include <sstream>
#include <type_traits>

namespace eosio {
// since beast_http_listener handles both TCP and UNIX endpoints we need a template here
// to get the path if makes sense, so that we can call ::unlink() before opening socket
// in beast_http_listener::listen() by tdefault return blank string
template<typename T>
std::string get_endpoint_path(const T& endpt) { return {}; }

std::string get_endpoint_path(const stream_protocol::endpoint& endpt) { return endpt.path(); }

// Accepts incoming connections and launches the sessions
// session_type should be a subclass of beast_http_session
// protocol type must have sub types acceptor and endpoint, e.g. boost::asio::ip::tcp;
// socket type must be the socket e.g, boost::asio::ip::tcp::socket
template<typename session_type, typename protocol_type, typename socket_type>
class beast_http_listener : public std::enable_shared_from_this<beast_http_listener<session_type, protocol_type, socket_type>> {
private:
   bool is_listening_ = false;

   std::shared_ptr<http_plugin_state> plugin_state_;

   typename protocol_type::acceptor acceptor_;
   socket_type socket_;

   boost::asio::deadline_timer accept_error_timer_;

public:
   beast_http_listener() = default;
   beast_http_listener(const beast_http_listener&) = delete;
   beast_http_listener(beast_http_listener&&) = delete;

   beast_http_listener& operator=(const beast_http_listener&) = delete;
   beast_http_listener& operator=(beast_http_listener&&) = delete;

   beast_http_listener(std::shared_ptr<http_plugin_state> plugin_state) : is_listening_(false), plugin_state_(std::move(plugin_state)), acceptor_(plugin_state_->thread_pool.get_executor()), socket_(plugin_state_->thread_pool.get_executor()), accept_error_timer_(plugin_state_->thread_pool.get_executor()) {}

   virtual ~beast_http_listener() {
      try {
         stop_listening();
      } catch(...) {}
   };

   void listen(typename protocol_type::endpoint endpoint) {
      if(is_listening_) return;

      // for unix sockets we should delete the old socket
      if(std::is_same<socket_type, stream_protocol::socket>::value) {
         ::unlink(get_endpoint_path(endpoint).c_str());
      }

      beast::error_code ec;
      // Open the acceptor
      acceptor_.open(endpoint.protocol(), ec);
      if(ec) {
         fail(ec, "open", plugin_state_->logger, "closing port");
         return;
      }

      // Allow address reuse
      acceptor_.set_option(asio::socket_base::reuse_address(true), ec);
      if(ec) {
         fail(ec, "set_option", plugin_state_->logger, "closing port");
         return;
      }

      // Bind to the server address
      acceptor_.bind(endpoint, ec);
      if(ec) {
         fail(ec, "bind", plugin_state_->logger, "closing port");
         return;
      }

      // Start listening for connections
      auto max_connections = asio::socket_base::max_listen_connections;
      fc_ilog(plugin_state_->logger, "acceptor_.listen()");
      acceptor_.listen(max_connections, ec);
      if(ec) {
         fail(ec, "listen", plugin_state_->logger, "closing port");
         return;
      }
      is_listening_ = true;
   }

   // Start accepting incoming connections
   void start_accept() {
      if(!is_listening_) return;
      do_accept();
   }

   bool is_listening() {
      return is_listening_;
   }

   void stop_listening() {
      if(is_listening_) {
         plugin_state_->thread_pool.stop();
         is_listening_ = false;
      }
   }

private:
   void do_accept() {
      auto self = this->shared_from_this();
      acceptor_.async_accept(socket_, [self](beast::error_code ec) {
         if(ec == boost::system::errc::too_many_files_open) {
            // retry accept() after timeout to avoid cpu loop on accept
            fail(ec, "accept", self->plugin_state_->logger, "too many files open - waiting 500ms");
            self->accept_error_timer_.expires_from_now(boost::posix_time::milliseconds(500));
            self->accept_error_timer_.async_wait([self = self->shared_from_this()](beast::error_code ec) {
               if (!ec)
                  self->do_accept();
            });
         } else {
            if (ec) {
               fail(ec, "accept", self->plugin_state_->logger, "closing connection");
            } else {
               // Create the session object and run it
               std::make_shared<session_type>(
                  std::move(self->socket_),
                  self->plugin_state_)
                  ->run_session();
            }
            
            // Accept another connection
            self->do_accept();
         }
      });
   }
};// end class beast_http_Listener
}// namespace eosio

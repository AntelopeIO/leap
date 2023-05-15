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
// socket type must be the socket e.g, boost::asio::ip::tcp::socket
template<typename socket_type>
class beast_http_listener : public std::enable_shared_from_this<beast_http_listener<socket_type>> {
private:
   std::shared_ptr<http_plugin_state> plugin_state_;

   using protocol_type = typename socket_type::protocol_type;
   typename protocol_type::acceptor acceptor_;
   socket_type socket_;
   std::string local_address_;

   boost::asio::deadline_timer accept_error_timer_;
   api_category_set categories_ = {};
public:
   beast_http_listener() = default;
   beast_http_listener(const beast_http_listener&) = delete;
   beast_http_listener(beast_http_listener&&) = delete;

   beast_http_listener& operator=(const beast_http_listener&) = delete;
   beast_http_listener& operator=(beast_http_listener&&) = delete;

   beast_http_listener(std::shared_ptr<http_plugin_state> plugin_state, api_category_set categories,
                       typename protocol_type::endpoint endpoint,
                       const std::string& local_address="")
       : plugin_state_(std::move(plugin_state)), acceptor_(plugin_state_->thread_pool.get_executor(), endpoint),
         socket_(plugin_state_->thread_pool.get_executor()), local_address_(local_address),
         accept_error_timer_(plugin_state_->thread_pool.get_executor()), categories_(categories) {
   }

   virtual ~beast_http_listener() {};

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
               boost::system::error_code re_ec;
               auto re = self->socket_.remote_endpoint(re_ec);
               std::string remote_endpoint = re_ec ? "unknown" : boost::lexical_cast<std::string>(re);
               std::make_shared<beast_http_session<socket_type>>(
                  std::move(self->socket_),
                  self->plugin_state_,
                  std::move(remote_endpoint),
                  self->categories_, 
                  self->local_address_)
                  ->run_session();
            }
            
            // Accept another connection
            self->do_accept();
         }
      });
   }

   bool is_ip_v6_only() const {
      boost::asio::ip::v6_only option;
      acceptor_.get_option(option);
      return option.value();
   }
};// end class beast_http_Listener
}// namespace eosio

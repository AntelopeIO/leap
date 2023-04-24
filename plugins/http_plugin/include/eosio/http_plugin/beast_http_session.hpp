#pragma once

#include <eosio/http_plugin/common.hpp>
#include <fc/io/json.hpp>

#include <boost/iostreams/device/array.hpp>
#include <boost/iostreams/stream.hpp>

#include <memory>
#include <string>
#include <charconv>

namespace eosio {

using std::chrono::steady_clock;

// Boost 1.70 introduced a breaking change that causes problems with construction of strand objects from tcp_socket
// this is suggested fix OK'd Beast author (V. Falco) to handle both versions gracefully
// see https://stackoverflow.com/questions/58453017/boost-asio-tcp-socket-1-70-not-backward-compatible
#if BOOST_VERSION < 107000
typedef tcp::socket tcp_socket_t;
#else
typedef asio::basic_stream_socket<asio::ip::tcp, asio::io_context::executor_type> tcp_socket_t;
#endif

using boost::asio::local::stream_protocol;

#if BOOST_VERSION < 107000
using local_stream = boost::asio::basic_stream_socket<stream_protocol>;
#else
#if BOOST_VERSION < 107300
using local_stream = beast::basic_stream<
      stream_protocol,
      asio::executor,
      beast::unlimited_rate_policy>;
#else
using local_stream = beast::basic_stream<
      stream_protocol,
      asio::any_io_executor,
      beast::unlimited_rate_policy>;
#endif
#endif

//------------------------------------------------------------------------------
// fail()
//  this function is generally reserved in the case of a severe error which results
//  in immediate termination of the session, with no response sent back to the client
void fail(beast::error_code ec, char const* what, fc::logger& logger, char const* action) {
   fc_elog(logger, "${w}: ${m}", ("w", what)("m", ec.message()));
   fc_elog(logger, action);
}


template<class T>
bool allow_host(const http::request<http::string_body>& req, T& session,
                const std::shared_ptr<http_plugin_state>& plugin_state) {
   auto is_conn_secure = session.is_secure();

   auto& socket = session.socket();
#if BOOST_VERSION < 107000
   auto& lowest_layer = beast::get_lowest_layer<tcp_socket_t&>(socket);
#else
   auto& lowest_layer = beast::get_lowest_layer(socket);
#endif
   auto local_endpoint = lowest_layer.local_endpoint();
   auto local_socket_host_port = local_endpoint.address().to_string() + ":" + std::to_string(local_endpoint.port());
   const std::string host_str(req["host"]);
   if(host_str.empty() || !host_is_valid(*plugin_state,
                                         host_str,
                                         local_socket_host_port,
                                         is_conn_secure)) {
      return false;
   }

   return true;
}

// Handle HTTP connection using boost::beast for TCP communication
// Subclasses of this class (plain_session, ssl_session (now removed), etc.)
// T can be request or response or anything serializable to boost iostreams
template<typename T>
std::string to_log_string(const T& req, size_t max_size = 1024) {
   assert( max_size > 4 );
   std::string buffer( max_size, '\0' );
   {
      boost::iostreams::array_sink sink( buffer.data(), buffer.size() );
      boost::iostreams::stream stream( sink );
      stream << req;
   }
   buffer.resize( std::strlen( buffer.data() ) );
   if( buffer.size() == max_size ) {
      buffer[max_size - 3] = '.';
      buffer[max_size - 2] = '.';
      buffer[max_size - 1] = '.';
   }
   std::replace_if( buffer.begin(), buffer.end(), []( unsigned char c ) { return c == '\r' || c == '\n'; }, ' ' );
   return buffer;
}

// use the Curiously Recurring Template Pattern so that
// the same code works with both regular TCP sockets and UNIX sockets
template<class Derived>
class beast_http_session : public detail::abstract_conn {
protected:
   beast::flat_buffer buffer_;

   // time points for timeout measurement and perf metrics
   steady_clock::time_point session_begin_, read_begin_, handle_begin_, write_begin_;
   uint64_t read_time_us_ = 0, handle_time_us_ = 0, write_time_us_ = 0;

   // HTTP parser object
   std::optional<http::request_parser<http::string_body>> req_parser_;

   // HTTP response object
   std::optional<http::response<http::string_body>> res_;

   std::shared_ptr<http_plugin_state> plugin_state_;
   std::string remote_endpoint_;

   // whether response should be sent back to client when an exception occurs
   bool is_send_exception_response_ = true;

   void set_content_type_header(http_content_type content_type) {
      switch (content_type) {
         case http_content_type::plaintext:
            res_->set(http::field::content_type, "text/plain");
            break;

         case http_content_type::json:
         default:
            res_->set(http::field::content_type, "application/json");
      }
   }

   enum class continue_state_t { none, read_body, reject };
   continue_state_t continue_state_ { continue_state_t::none };

   template<
         class Body, class Allocator>
   void
   handle_request(http::request<Body, http::basic_fields<Allocator>>&& req) {
      res_->version(req.version());
      res_->set(http::field::content_type, "application/json");
      res_->keep_alive(req.keep_alive());
      if(plugin_state_->server_header.size())
         res_->set(http::field::server, plugin_state_->server_header);

      // Request path must be absolute and not contain "..".
      if(req.target().empty() || req.target()[0] != '/' || req.target().find("..") != beast::string_view::npos) {
         error_results results{static_cast<uint16_t>(http::status::bad_request), "Illegal request-target"};
         send_response( fc::json::to_string( results, fc::time_point::maximum() ),
                        static_cast<unsigned int>(http::status::bad_request) );
         return;
      }

      try {
         if(!derived().allow_host(req)) {
            error_results results{static_cast<uint16_t>(http::status::bad_request), "Disallowed HTTP HOST header in the request"};
            send_response( fc::json::to_string( results, fc::time_point::maximum() ),
                        static_cast<unsigned int>(http::status::bad_request) );
            return;
         }

         if(!plugin_state_->access_control_allow_origin.empty()) {
            res_->set("Access-Control-Allow-Origin", plugin_state_->access_control_allow_origin);
         }
         if(!plugin_state_->access_control_allow_headers.empty()) {
            res_->set("Access-Control-Allow-Headers", plugin_state_->access_control_allow_headers);
         }
         if(!plugin_state_->access_control_max_age.empty()) {
            res_->set("Access-Control-Max-Age", plugin_state_->access_control_max_age);
         }
         if(plugin_state_->access_control_allow_credentials) {
            res_->set("Access-Control-Allow-Credentials", "true");
         }

         // Respond to options request
         if(req.method() == http::verb::options) {
            send_response("{}", static_cast<unsigned int>(http::status::ok));
            return;
         }

         fc_dlog( plugin_state_->logger, "Request:  ${ep} ${r}",
                  ("ep", remote_endpoint_)("r", to_log_string(req)) );

         std::string resource = std::string(req.target());
         // look for the URL handler to handle this resource
         auto handler_itr = plugin_state_->url_handlers.find(resource);
         if(handler_itr != plugin_state_->url_handlers.end()) {
            if(plugin_state_->logger.is_enabled(fc::log_level::all))
               plugin_state_->logger.log(FC_LOG_MESSAGE(all, "resource: ${ep}", ("ep", resource)));
            std::string body = req.body();
            auto content_type = handler_itr->second.content_type;
            set_content_type_header(content_type);

            if (plugin_state_->update_metrics)
               plugin_state_->update_metrics({resource});

            handler_itr->second.fn(derived().shared_from_this(),
                                std::move(resource),
                                std::move(body),
                                make_http_response_handler(plugin_state_, derived().shared_from_this(), content_type));
         } else {
            fc_dlog( plugin_state_->logger, "404 - not found: ${ep}", ("ep", resource) );
            error_results results{static_cast<uint16_t>(http::status::not_found), "Not Found",
                                  error_results::error_info( fc::exception( FC_LOG_MESSAGE( error, "Unknown Endpoint" ) ),
                                                             http_plugin::verbose_errors() )};
            send_response( fc::json::to_string( results, fc::time_point::maximum() ),
                           static_cast<unsigned int>(http::status::not_found) );
         }
      } catch(...) {
         handle_exception();
      }
   }

private:
   void send_100_continue_response(bool do_continue) {
      auto res = std::make_shared<http::response<http::empty_body>>();
         
      res->version(11);
      if (do_continue) {
         res->result(http::status::continue_);
         continue_state_ = continue_state_t::read_body;   // after sending the continue response, just read the body with the same parser
      } else {
         res->result(http::status::unauthorized);
         continue_state_ = continue_state_t::reject;
      }
      res->set(http::field::server, plugin_state_->server_header);
      
      http::async_write(
         derived().stream(),
         *res,
         [self = derived().shared_from_this(), res](beast::error_code ec, std::size_t bytes_transferred) {
            self->on_write(ec, bytes_transferred, false);
         });
   }

public:

   virtual void send_busy_response(std::string&& what) final {
      error_results::error_info ei;
      ei.code = static_cast<int64_t>(http::status::too_many_requests);
      ei.name = "Busy";
      ei.what = std::move(what);
      error_results results{static_cast<uint16_t>(http::status::too_many_requests), "Busy", ei};
      send_response(fc::json::to_string(results, fc::time_point::maximum()),
                    static_cast<unsigned int>(http::status::too_many_requests) );
   }
   
   virtual std::string verify_max_bytes_in_flight(size_t extra_bytes) final {
      auto bytes_in_flight_size = plugin_state_->bytes_in_flight.load() + extra_bytes;
      if(bytes_in_flight_size > plugin_state_->max_bytes_in_flight) {
         fc_dlog(plugin_state_->logger, "429 - too many bytes in flight: ${bytes}", ("bytes", bytes_in_flight_size));
         return "Too many bytes in flight: " + std::to_string( bytes_in_flight_size );
      }
      return {};
   }

   virtual std::string verify_max_requests_in_flight() final {
      if(plugin_state_->max_requests_in_flight < 0)
         return {};

      auto requests_in_flight_num = plugin_state_->requests_in_flight.load();
      if(requests_in_flight_num > plugin_state_->max_requests_in_flight) {
         fc_dlog(plugin_state_->logger, "429 - too many requests in flight: ${requests}", ("requests", requests_in_flight_num));
         return "Too many requests in flight: " + std::to_string( requests_in_flight_num );
      }
      return {};
   }

   // Access the derived class, this is part of
   // the Curiously Recurring Template Pattern idiom.
   Derived& derived() {
      return static_cast<Derived&>(*this);
   }

public:
   // shared_from_this() requires default constructor
   beast_http_session() = default;

   beast_http_session(std::shared_ptr<http_plugin_state> plugin_state, std::string remote_endpoint)
       : plugin_state_(std::move(plugin_state)),
         remote_endpoint_(std::move(remote_endpoint)) {
      plugin_state_->requests_in_flight += 1;
      req_parser_.emplace();
      req_parser_->body_limit(plugin_state_->max_body_size);
      res_.emplace();

      session_begin_ = steady_clock::now();
      read_time_us_ = handle_time_us_ = write_time_us_ = 0;

      // default to true
      is_send_exception_response_ = true;
   }

   virtual ~beast_http_session() {
      is_send_exception_response_ = false;
      plugin_state_->requests_in_flight -= 1;
      if(plugin_state_->logger.is_enabled(fc::log_level::all)) {
         auto session_time = steady_clock::now() - session_begin_;
         auto session_time_us = std::chrono::duration_cast<std::chrono::microseconds>(session_time).count();
         plugin_state_->logger.log(FC_LOG_MESSAGE(all, "session time    ${t}", ("t", session_time_us)));
         plugin_state_->logger.log(FC_LOG_MESSAGE(all, "        read    ${t}", ("t", read_time_us_)));
         plugin_state_->logger.log(FC_LOG_MESSAGE(all, "        handle  ${t}", ("t", handle_time_us_)));
         plugin_state_->logger.log(FC_LOG_MESSAGE(all, "        write   ${t}", ("t", write_time_us_)));
      }
   }

   void do_read_header() {
      read_begin_ = steady_clock::now();

      // Read a request
      http::async_read_header(
            derived().stream(),
            buffer_,
            *req_parser_,
            [self = derived().shared_from_this()](beast::error_code ec, std::size_t bytes_transferred) {
               self->on_read_header(ec, bytes_transferred);
            });
   }

   void on_read_header(beast::error_code ec, std::size_t /* bytes_transferred */) {
      if(ec) {
         if(ec == http::error::end_of_stream) // other side closed the connection
            return derived().do_eof();
         
         return fail(ec, "read_header", plugin_state_->logger, "closing connection");
      }

      // Check for the Expect field value
      if (req_parser_->get()[http::field::expect] == "100-continue") {
         bool do_continue = true;
         auto sv = req_parser_->get()[http::field::content_length];
         if (uint64_t sz; !sv.empty() && std::from_chars(sv.data(), sv.data() + sv.size(), sz).ec == std::errc() &&
             sz > plugin_state_->max_body_size) {
            do_continue = false;
         }
         send_100_continue_response(do_continue);
         return;
      }

      // Read the rest of the message.
      do_read();
   }

   void do_read() {
      // Read a request
      http::async_read(
            derived().stream(),
            buffer_,
            *req_parser_,
            [self = derived().shared_from_this()](beast::error_code ec, std::size_t bytes_transferred) {
               self->on_read(ec, bytes_transferred);
            });
   }

   void on_read(beast::error_code ec, std::size_t /* bytes_transferred */) {

      if(ec) {
         // By default, http_plugin runs in keep_alive mode (persistent connections)
         // hence respecting the http 1.1 standard. So after sending a response, we wait
         // on another read. If the client disconnects, we may get
         // http::error::end_of_stream or asio::error::connection_reset.
         if(ec == http::error::end_of_stream || ec == asio::error::connection_reset)
            return derived().do_eof();

         return fail(ec, "read", plugin_state_->logger, "closing connection");
      }

      auto req = req_parser_->release();

      handle_begin_ = steady_clock::now();
      auto dt = handle_begin_ - read_begin_;
      read_time_us_ += std::chrono::duration_cast<std::chrono::microseconds>(dt).count();

      // Send the response
      handle_request(std::move(req));
   }

   void on_write(beast::error_code ec,
                 std::size_t bytes_transferred,
                 bool close) {
      boost::ignore_unused(bytes_transferred);

      if(ec) {
         return fail(ec, "write", plugin_state_->logger, "closing connection");
      }

      auto dt = steady_clock::now() - write_begin_;
      write_time_us_ += std::chrono::duration_cast<std::chrono::microseconds>(dt).count();

      if(close) {
         // This means we should close the connection, usually because
         // the response indicated the "Connection: close" semantic.
         return derived().do_eof();
      }

      // create a new response object
      res_.emplace();

      switch(continue_state_) {
      case continue_state_t::read_body:
         // just sent "100-continue" response - now read the body with same parser
         continue_state_ = continue_state_t::none;
         do_read();
         break;
         
      case continue_state_t::reject:
         // request body too large. After issuing 401 response, close connection
         continue_state_ = continue_state_t::none;
         derived().do_eof();
         break;
         
      default:
         assert(continue_state_ == continue_state_t::none);
         
         // create a new parser to clear state
         req_parser_.emplace();
         req_parser_->body_limit(plugin_state_->max_body_size);

         // Read another request
         do_read_header();
         break;
      }
   }

   virtual void handle_exception() final {
      std::string err_str;
      try {
         try {
            throw;
         } catch(const fc::exception& e) {
            err_str = e.to_detail_string();
            fc_elog(plugin_state_->logger, "fc::exception: ${w}", ("w", err_str));
            if( is_send_exception_response_ ) {
               error_results results{static_cast<uint16_t>(http::status::internal_server_error),
                                     "Internal Service Error",
                                     error_results::error_info( e, http_plugin::verbose_errors() )};
               err_str = fc::json::to_string( results, fc::time_point::now() + plugin_state_->max_response_time );
            }
         } catch(std::exception& e) {
            err_str = e.what();
            fc_elog(plugin_state_->logger, "std::exception: ${w}", ("w", err_str));
            if( is_send_exception_response_ ) {
               error_results results{static_cast<uint16_t>(http::status::internal_server_error),
                                     "Internal Service Error",
                                     error_results::error_info( fc::exception( FC_LOG_MESSAGE( error, err_str ) ),
                                                                http_plugin::verbose_errors() )};
               err_str = fc::json::to_string( results, fc::time_point::now() + plugin_state_->max_response_time );
            }
         } catch(...) {
            err_str = "Unknown exception";
            fc_elog(plugin_state_->logger, err_str);
            if( is_send_exception_response_ ) {
               error_results results{static_cast<uint16_t>(http::status::internal_server_error),
                                     "Internal Service Error",
                                     error_results::error_info(
                                           fc::exception( FC_LOG_MESSAGE( error, err_str ) ),
                                           http_plugin::verbose_errors() )};
               err_str = fc::json::to_string( results, fc::time_point::maximum() );
            }
         }
      } catch (fc::timeout_exception& e) {
         fc_elog( plugin_state_->logger, "Timeout exception ${te} attempting to handle exception: ${e}", ("te", e.to_detail_string())("e", err_str) );
         err_str = R"xxx({"message": "Internal Server Error"})xxx";
      } catch (...) {
         fc_elog( plugin_state_->logger, "Exception attempting to handle exception: ${e}", ("e", err_str) );
         err_str = R"xxx({"message": "Internal Server Error"})xxx";
      }


      if(is_send_exception_response_) {
         set_content_type_header(http_content_type::json);
         res_->keep_alive(false);
         res_->set(http::field::server, BOOST_BEAST_VERSION_STRING);

         send_response(std::move(err_str), static_cast<unsigned int>(http::status::internal_server_error));
         derived().do_eof();
      }
   }

   void increment_bytes_in_flight(size_t sz) {
      plugin_state_->bytes_in_flight += sz;
   }

   void decrement_bytes_in_flight(size_t sz) {
      plugin_state_->bytes_in_flight -= sz;
   }

   virtual void send_response(std::string&& json, unsigned int code) final {
      auto payload_size = json.size();
      increment_bytes_in_flight(payload_size);
      write_begin_ = steady_clock::now();
      auto dt = write_begin_ - handle_begin_;
      handle_time_us_ += std::chrono::duration_cast<std::chrono::microseconds>(dt).count();

      res_->result(code);
      res_->body() = std::move(json);
      res_->prepare_payload();

      // Determine if we should close the connection after
      bool close = !(plugin_state_->keep_alive) || res_->need_eof();

      fc_dlog( plugin_state_->logger, "Response: ${ep} ${b}",
               ("ep", remote_endpoint_)("b", to_log_string(*res_)) );

      // Write the response
      http::async_write(
         derived().stream(),
         *res_,
         [self = derived().shared_from_this(), payload_size, close](beast::error_code ec, std::size_t bytes_transferred) {
            self->decrement_bytes_in_flight(payload_size);
            self->on_write(ec, bytes_transferred, close);
         });
   }

   void run_session() {
      if(auto error_str = verify_max_requests_in_flight(); !error_str.empty()) {
         send_busy_response(std::move(error_str));
         return derived().do_eof();
      }

      derived().run();
   }
};// end class beast_http_session

// Handles a plain HTTP connection
class plain_session
    : public beast_http_session<plain_session>,
      public std::enable_shared_from_this<plain_session> {
   tcp_socket_t socket_;

public:
   // Create the session
   plain_session(
         tcp_socket_t socket,
         std::shared_ptr<http_plugin_state> plugin_state,
         std::string remote_endpoint)
       : beast_http_session<plain_session>(std::move(plugin_state), std::move(remote_endpoint)), socket_(std::move(socket)) {}

   tcp_socket_t& stream() { return socket_; }
   tcp_socket_t& socket() { return socket_; }

   // Start the asynchronous operation
   void run() {
      do_read_header();
   }

   void do_eof() {
      is_send_exception_response_ = false;
      try {
         // Send a TCP shutdown
         beast::error_code ec;
         socket_.shutdown(tcp::socket::shutdown_send, ec);
         // At this point the connection is closed gracefully
      } catch(...) {
         handle_exception();
      }
   }

   bool is_secure() { return false; };

   bool allow_host(const http::request<http::string_body>& req) {
      return eosio::allow_host(req, *this, plugin_state_);
   }

   static constexpr auto name() {
      return "plain_session";
   }
};// end class plain_session

// unix domain sockets
class unix_socket_session
    : public std::enable_shared_from_this<unix_socket_session>,
      public beast_http_session<unix_socket_session> {

   // The socket used to communicate with the client.
   stream_protocol::socket socket_;

public:
   unix_socket_session(stream_protocol::socket sock,
                       std::shared_ptr<http_plugin_state> plugin_state,
                       std::string remote_endpoint)
       : beast_http_session(std::move(plugin_state), std::move(remote_endpoint)), socket_(std::move(sock)) {}

   virtual ~unix_socket_session() = default;

   bool allow_host(const http::request<http::string_body>& req) {
      // always allow local hosts
      return true;
   }

   void do_eof() {
      is_send_exception_response_ = false;
      try {
         // Send a shutdown signal
         boost::system::error_code ec;
         socket_.shutdown(stream_protocol::socket::shutdown_send, ec);
         // At this point the connection is closed gracefully
      } catch(...) {
         handle_exception();
      }
   }

   bool is_secure() { return false; };

   void run() {
      do_read_header();
   }

   stream_protocol::socket& stream() { return socket_; }
   stream_protocol::socket& socket() { return socket_; }

   static constexpr auto name() {
      return "unix_socket_session";
   }
};// end class unix_socket_session

}// namespace eosio

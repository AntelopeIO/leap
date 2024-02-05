#pragma once

#include <eosio/http_plugin/common.hpp>
#include <eosio/http_plugin/api_category.hpp>

#include <fc/io/json.hpp>
#include <fc/time.hpp>

#include <boost/iostreams/device/array.hpp>
#include <boost/iostreams/stream.hpp>

#include <memory>
#include <string>
#include <charconv>

namespace eosio {

using std::chrono::steady_clock;


//------------------------------------------------------------------------------
// fail()
//  this function is generally reserved in the case of a severe error which results
//  in immediate termination of the session, with no response sent back to the client
void fail(beast::error_code ec, char const* what, fc::logger& logger, char const* action) {
   fc_elog(logger, "${w}: ${m}", ("w", what)("m", ec.message()));
   fc_elog(logger, action);
}


bool allow_host(const std::string& host_str, tcp::socket& socket,
                const http_plugin_state& plugin_state) {

   auto& lowest_layer = beast::get_lowest_layer(socket);
   auto local_endpoint = lowest_layer.local_endpoint();
   if(host_str.empty() || !host_is_valid(plugin_state,
                                         host_str,
                                         local_endpoint.address())) {
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
template <class Socket>
class beast_http_session : public detail::abstract_conn,
                           public std::enable_shared_from_this<beast_http_session<Socket>> {

   std::shared_ptr<http_plugin_state> plugin_state_;
   Socket             socket_;
   api_category_set   categories_;
   beast::flat_buffer buffer_;

   // time points for timeout measurement and perf metrics
   steady_clock::time_point session_begin_, read_begin_, handle_begin_, write_begin_;
   uint64_t read_time_us_ = 0, handle_time_us_ = 0, write_time_us_ = 0;

   // HTTP parser object
   std::optional<http::request_parser<http::string_body>> req_parser_;

   // HTTP response object
   std::optional<http::response<http::string_body>> res_;

   std::string remote_endpoint_;
   std::string local_address_;

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
         fc_dlog( plugin_state_->get_logger(), "Return bad_reqest:  ${target}",  ("target", std::string(req.target())) );
         error_results results{static_cast<uint16_t>(http::status::bad_request), "Illegal request-target"};
         send_response( fc::json::to_string( results, fc::time_point::maximum() ),
                        static_cast<unsigned int>(http::status::bad_request) );
         return;
      }

      try {
         if(!allow_host(req)) {
            fc_dlog( plugin_state_->get_logger(), "bad host:  ${HOST}", ("HOST", std::string(req["host"])));
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

         fc_dlog( plugin_state_->get_logger(), "Request:  ${ep} ${r}",
                  ("ep", remote_endpoint_)("r", to_log_string(req)) );

         std::string resource = std::string(req.target());
         // look for the URL handler to handle this resource
         auto handler_itr = plugin_state_->url_handlers.find(resource);
         if(handler_itr != plugin_state_->url_handlers.end() && categories_.contains(handler_itr->second.category)) {
            if(plugin_state_->get_logger().is_enabled(fc::log_level::all))
               plugin_state_->get_logger().log(FC_LOG_MESSAGE(all, "resource: ${ep}", ("ep", resource)));
            std::string body = req.body();
            auto content_type = handler_itr->second.content_type;
            set_content_type_header(content_type);

            if (plugin_state_->update_metrics)
               plugin_state_->update_metrics({resource});

            handler_itr->second.fn(this->shared_from_this(),
                                std::move(resource),
                                std::move(body),
                                make_http_response_handler(*plugin_state_, this->shared_from_this(), content_type));
         } else if (resource == "/v1/node/get_supported_apis") {
            http_plugin::get_supported_apis_result result;
            for (const auto& handler : plugin_state_->url_handlers) {
               if (categories_.contains(handler.second.category))
                  result.apis.push_back(handler.first);
            }
            send_response(fc::json::to_string(fc::variant(result), fc::time_point::maximum()), 200);
         } else {
            fc_dlog( plugin_state_->get_logger(), "404 - not found: ${ep}", ("ep", resource) );
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
         socket_,
         *res,
         [self = this->shared_from_this(), res](beast::error_code ec, std::size_t bytes_transferred) {
            self->on_write(ec, bytes_transferred, false);
         });
   }

public:

   virtual void send_busy_response(std::string&& what) final {
      error_results::error_info ei;
      ei.code = static_cast<int64_t>(http::status::service_unavailable);
      ei.name = "Busy";
      ei.what = std::move(what);
      error_results results{static_cast<uint16_t>(http::status::service_unavailable), "Busy", ei};
      send_response(fc::json::to_string(results, fc::time_point::maximum()),
                    static_cast<unsigned int>(http::status::service_unavailable) );
   }
   
   virtual std::string verify_max_bytes_in_flight(size_t extra_bytes) final {
      auto bytes_in_flight_size = plugin_state_->bytes_in_flight.load() + extra_bytes;
      if(bytes_in_flight_size > plugin_state_->max_bytes_in_flight) {
         fc_dlog(plugin_state_->get_logger(), "503 - too many bytes in flight: ${bytes}", ("bytes", bytes_in_flight_size));
         return "Too many bytes in flight: " + std::to_string( bytes_in_flight_size );
      }
      return {};
   }

   virtual std::string verify_max_requests_in_flight() final {
      if(plugin_state_->max_requests_in_flight < 0)
         return {};

      auto requests_in_flight_num = plugin_state_->requests_in_flight.load();
      if(requests_in_flight_num > plugin_state_->max_requests_in_flight) {
         fc_dlog(plugin_state_->get_logger(), "503 - too many requests in flight: ${requests}", ("requests", requests_in_flight_num));
         return "Too many requests in flight: " + std::to_string( requests_in_flight_num );
      }
      return {};
   }

public:
   // shared_from_this() requires default constructor
   beast_http_session() = default;

   beast_http_session(Socket&& socket, std::shared_ptr<http_plugin_state> plugin_state, std::string remote_endpoint,
                      api_category_set categories, const std::string& local_address)
       : plugin_state_(std::move(plugin_state)), socket_(std::move(socket)), categories_(categories),
         remote_endpoint_(std::move(remote_endpoint)), local_address_(local_address) {
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
      if(plugin_state_->get_logger().is_enabled(fc::log_level::all)) {
         auto session_time = steady_clock::now() - session_begin_;
         auto session_time_us = std::chrono::duration_cast<std::chrono::microseconds>(session_time).count();
         plugin_state_->get_logger().log(FC_LOG_MESSAGE(all, "session time    ${t}", ("t", session_time_us)));
         plugin_state_->get_logger().log(FC_LOG_MESSAGE(all, "        read    ${t}", ("t", read_time_us_)));
         plugin_state_->get_logger().log(FC_LOG_MESSAGE(all, "        handle  ${t}", ("t", handle_time_us_)));
         plugin_state_->get_logger().log(FC_LOG_MESSAGE(all, "        write   ${t}", ("t", write_time_us_)));
      }
   }

   void do_read_header() {
      read_begin_ = steady_clock::now();

      // Read a request
      http::async_read_header(
            socket_,
            buffer_,
            *req_parser_,
            [self = this->shared_from_this()](beast::error_code ec, std::size_t bytes_transferred) {
               self->on_read_header(ec, bytes_transferred);
            });
   }

   void on_read_header(beast::error_code ec, std::size_t /* bytes_transferred */) {
      if(ec) {
         // See on_read comment below
         if(ec == http::error::end_of_stream || ec == asio::error::connection_reset)
            return do_eof();

         return fail(ec, "read_header", plugin_state_->get_logger(), "closing connection");
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
            socket_,
            buffer_,
            *req_parser_,
            [self = this->shared_from_this()](beast::error_code ec, std::size_t bytes_transferred) {
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
            return do_eof();

         return fail(ec, "read", plugin_state_->get_logger(), "closing connection");
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
         return fail(ec, "write", plugin_state_->get_logger(), "closing connection");
      }

      auto dt = steady_clock::now() - write_begin_;
      write_time_us_ += std::chrono::duration_cast<std::chrono::microseconds>(dt).count();

      if(close) {
         // This means we should close the connection, usually because
         // the response indicated the "Connection: close" semantic.
         return do_eof();
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
         do_eof();
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
            fc_elog(plugin_state_->get_logger(), "fc::exception: ${w}", ("w", err_str));
            if( is_send_exception_response_ ) {
               error_results results{static_cast<uint16_t>(http::status::internal_server_error),
                                     "Internal Service Error",
                                     error_results::error_info( e, http_plugin::verbose_errors() )};
               err_str = fc::json::to_string( results, fc::time_point::now().safe_add(plugin_state_->max_response_time) );
            }
         } catch(std::exception& e) {
            err_str = e.what();
            fc_elog(plugin_state_->get_logger(), "std::exception: ${w}", ("w", err_str));
            if( is_send_exception_response_ ) {
               error_results results{static_cast<uint16_t>(http::status::internal_server_error),
                                     "Internal Service Error",
                                     error_results::error_info( fc::exception( FC_LOG_MESSAGE( error, err_str ) ),
                                                                http_plugin::verbose_errors() )};
               err_str = fc::json::to_string( results, fc::time_point::now().safe_add(plugin_state_->max_response_time) );
            }
         } catch(...) {
            err_str = "Unknown exception";
            fc_elog(plugin_state_->get_logger(), err_str);
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
         fc_elog( plugin_state_->get_logger(), "Timeout exception ${te} attempting to handle exception: ${e}", ("te", e.to_detail_string())("e", err_str) );
         err_str = R"xxx({"message": "Internal Server Error"})xxx";
      } catch (...) {
         fc_elog( plugin_state_->get_logger(), "Exception attempting to handle exception: ${e}", ("e", err_str) );
         err_str = R"xxx({"message": "Internal Server Error"})xxx";
      }


      if(is_send_exception_response_) {
         set_content_type_header(http_content_type::json);
         res_->keep_alive(false);
         res_->set(http::field::server, BOOST_BEAST_VERSION_STRING);

         send_response(std::move(err_str), static_cast<unsigned int>(http::status::internal_server_error));
         do_eof();
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

      fc_dlog( plugin_state_->get_logger(), "Response: ${ep} ${b}",
               ("ep", remote_endpoint_)("b", to_log_string(*res_)) );

      // Write the response
      http::async_write(
         socket_,
         *res_,
         [self = this->shared_from_this(), payload_size, close](beast::error_code ec, std::size_t bytes_transferred) {
            self->decrement_bytes_in_flight(payload_size);
            self->on_write(ec, bytes_transferred, close);
         });
   }

   void run_session() {
      if(auto error_str = verify_max_requests_in_flight(); !error_str.empty()) {
         res_->keep_alive(false);
         send_busy_response(std::move(error_str));
         return;
      }

      do_read_header();
   }

   void do_eof() {
      is_send_exception_response_ = false;
      try {
         // Send a shutdown signal
         beast::error_code ec;
         socket_.shutdown(Socket::shutdown_both, ec);
         socket_.close(ec);
         // At this point the connection is closed gracefully
      } catch(...) {
         handle_exception();
      }
   }


   bool allow_host(const http::request<http::string_body>& req) {
      if constexpr(std::is_same_v<Socket, tcp::socket>) {
         const std::string host_str(req["host"]);
         if (host_str != local_address_)
            return eosio::allow_host(host_str, socket_, *plugin_state_);
      }
      return true;
   }

}; // end class beast_http_session

}// namespace eosio

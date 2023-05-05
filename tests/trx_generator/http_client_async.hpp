#pragma once

// The majority of the code here are derived from boost source
// libs/beast/example/http/client/async/http_client_async.cpp
// with minimum modification and yet reusable.
//
//
// Copyright (c) 2016-2019 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/boostorg/beast
//

//------------------------------------------------------------------------------
//
// Example: HTTP client, asynchronous
//
//------------------------------------------------------------------------------

#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <string>

//------------------------------------------------------------------------------

namespace eosio {
namespace http_client_async {

namespace beast = boost::beast;         // from <boost/beast.hpp>
namespace http  = beast::http;          // from <boost/beast/http.hpp>
namespace net   = boost::asio;          // from <boost/asio.hpp>
using tcp       = boost::asio::ip::tcp; // from <boost/asio/ip/tcp.hpp>

using response_callback_t = std::function<void(beast::error_code ec, http::response<http::string_body>)>;

namespace details {

// Report a failure
inline void fail(beast::error_code ec, char const* what) { std::cerr << what << ": " << ec.message() << "\n"; }

// Performs an HTTP GET and prints the response
class session : public std::enable_shared_from_this<session> {
   tcp::resolver                     resolver_;
   beast::tcp_stream                 stream_;
   beast::flat_buffer                buffer_; // (Must persist between reads)
   http::request<http::string_body>  req_;
   http::response<http::string_body> res_;
   response_callback_t               response_callback_;

 public:
   // Objects are constructed with a strand to
   // ensure that handlers do not execute concurrently.
   explicit session(net::io_context& ioc, const response_callback_t& response_callback)
       : resolver_(net::make_strand(ioc))
       , stream_(net::make_strand(ioc))
       , response_callback_(response_callback) {}

   // Start the asynchronous operation
   void run(const std::string& host, const unsigned short port, const std::string& target, int version,
            const std::string& content_type, std::string&& request_body) {
      // Set up an HTTP GET request message
      req_.version(version);
      req_.method(http::verb::post);
      req_.target(target);
      req_.set(http::field::host, host);
      req_.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
      req_.set(http::field::content_type, content_type);
      req_.body() = std::move(request_body);
      req_.prepare_payload();

      // Look up the domain name
      resolver_.async_resolve(
          host, std::to_string(port), [self = this->shared_from_this()](beast::error_code ec, auto res) { self->on_resolve(ec, res); });
   }

   void on_resolve(beast::error_code ec, tcp::resolver::results_type results) {
      if (ec) {
         response_callback_(ec, {});
         return fail(ec, "resolve");
      }

      // Set a timeout on the operation
      stream_.expires_after(std::chrono::seconds(30));

      // Make the connection on the IP address we get from a lookup
      stream_.async_connect(results, [self = this->shared_from_this()](beast::error_code ec, auto endpt) {
         self->on_connect(ec, endpt);
      });
   }

   void on_connect(beast::error_code ec, tcp::resolver::results_type::endpoint_type) {
      if (ec) {
         response_callback_(ec, {});
         return fail(ec, "connect");
      }

      // Set a timeout on the operation
      stream_.expires_after(std::chrono::seconds(30));

      // Send the HTTP request to the remote host
      http::async_write(stream_, req_, [self = this->shared_from_this()](beast::error_code ec, auto bytes_transferred) {
         self->on_write(ec, bytes_transferred);
      });
   }

   void on_write(beast::error_code ec, std::size_t bytes_transferred) {
      boost::ignore_unused(bytes_transferred);

      if (ec) {
         response_callback_(ec, {});
         return fail(ec, "write");
      }

      // Receive the HTTP response
      http::async_read(stream_, buffer_, res_,
                       [self = this->shared_from_this()](beast::error_code ec, auto bytes_transferred) {
                          self->on_read(ec, bytes_transferred);
                       });
   }

   void on_read(beast::error_code ec, std::size_t bytes_transferred) {
      boost::ignore_unused(bytes_transferred);

      if (ec) {
         response_callback_(ec, {});
         return fail(ec, "read");
      }

      // Write the response message to the callback
      response_callback_(ec, res_);

      // Gracefully close the socket
      stream_.socket().shutdown(tcp::socket::shutdown_both, ec);

      // not_connected happens sometimes so don't bother reporting it.
      if (ec && ec != beast::errc::not_connected)
         return fail(ec, "shutdown");

      // If we get here then the connection is closed gracefully
   }
};
} // namespace details

struct http_request_params {
   net::io_context&  ioc;
   const std::string host;
   const unsigned short port;
   const std::string target;
   int               version;
   const std::string content_type;
};

inline void async_http_request(http_request_params& req_params, const std::string&& request_body,
                               const response_callback_t& response_callback) {
   std::make_shared<details::session>(req_params.ioc, response_callback)
       ->run(req_params.host, req_params.port, req_params.target, req_params.version, req_params.content_type,
             std::move(request_body));
};
} // namespace http_client_async
} // namespace eosio

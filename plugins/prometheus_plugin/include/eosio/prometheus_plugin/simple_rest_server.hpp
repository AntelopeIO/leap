#pragma once

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>

namespace eosio { namespace rest {

   // The majority of the code here are derived from boost source
   // libs/beast/example/http/server/async/http_server_async.cpp
   // with minium modification and yet reusable.

   namespace beast = boost::beast;         // from <boost/beast.hpp>
   namespace http  = beast::http;          // from <boost/beast/http.hpp>
   namespace net   = boost::asio;          // from <boost/asio.hpp>
   using tcp       = boost::asio::ip::tcp; // from <boost/asio/ip/tcp.hpp>
   template <typename T>
   class simple_server {
      T* self() { return static_cast<T*>(this); }

      void fail(beast::error_code ec, char const* what) { self()->log_error(what, ec.message()); }
      // Return a response for the given request.
      http::response<http::string_body> handle_request(http::request<http::string_body>&& req) {
         // Returns a bad request response
         auto const bad_request = [&req](beast::string_view why) {
            http::response<http::string_body> res{ http::status::bad_request, req.version() };
            res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
            res.set(http::field::content_type, "text/plain");
            res.keep_alive(req.keep_alive());
            res.body() = std::string(why);
            res.prepare_payload();
            return res;
         };

         // Returns a not found response
         auto const not_found = [&req](beast::string_view target) {
            http::response<http::string_body> res{ http::status::not_found, req.version() };
            res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
            res.set(http::field::content_type, "text/html");
            res.keep_alive(req.keep_alive());
            res.body() = "The resource '" + std::string(target) + "' was not found.";
            res.prepare_payload();
            return res;
         };

         // Returns a server error response
         auto const server_error = [&req](beast::string_view what) {
            http::response<http::string_body> res{ http::status::internal_server_error, req.version() };
            res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
            res.set(http::field::content_type, "text/html");
            res.keep_alive(req.keep_alive());
            res.body() = "An error occurred: '" + std::string(what) + "'";
            res.prepare_payload();
            return res;
         };

         // Make sure we can handle the method
         if (!self()->allow_method(req.method()))
            return bad_request("Unknown HTTP-method");

         // Request path must be absolute and not contain "..".
         auto target = req.target();
         if (target.empty() || target[0] != '/' || target.find("..") != beast::string_view::npos)
            return bad_request("Illegal request-target");

         try {
            auto res = self()->on_request(std::move(req));
            if (!res)
               not_found(target);
            return *res;
         } catch (std::exception& ex) { return server_error(ex.what()); }
      }

      class session : public std::enable_shared_from_this<session> {
         beast::tcp_stream                stream_;
         beast::flat_buffer               buffer_;
         http::request<http::string_body> req_;
         simple_server*                   server_;

       public:
         // Take ownership of the stream
         session(tcp::socket&& socket, simple_server* server) : stream_(std::move(socket)), server_(server) {}

         // Start the asynchronous operation
         void run() {
            // We need to be executing within a strand to perform async operations
            // on the I/O objects in this session. Although not strictly necessary
            // for single-threaded contexts, this example code is written to be
            // thread-safe by default.
            net::dispatch(stream_.get_executor(),
                          beast::bind_front_handler(&session::do_read, this->shared_from_this()));
         }

         void do_read() {
            // Make the request empty before reading,
            // otherwise the operation behavior is undefined.
            req_ = {};

            // Set the timeout.
            stream_.expires_after(std::chrono::seconds(30));

            // Read a request
            http::async_read(stream_, buffer_, req_,
                             beast::bind_front_handler(&session::on_read, this->shared_from_this()));
         }

         void on_read(beast::error_code ec, std::size_t bytes_transferred) {
            boost::ignore_unused(bytes_transferred);

            // This means they closed the connection
            if (ec == http::error::end_of_stream)
               return do_close();

            if (ec)
               return server_->fail(ec, "read");

            // Send the response
            send_response(server_->handle_request(std::move(req_)));
         }

         void send_response(http::response<http::string_body>&& msg) {
            bool keep_alive = msg.keep_alive();

            // Write the response
            http::async_write(stream_, std::move(msg),
                              beast::bind_front_handler(&session::on_write, this->shared_from_this(), keep_alive));
         }

         void on_write(bool keep_alive, beast::error_code ec, std::size_t bytes_transferred) {
            boost::ignore_unused(bytes_transferred);

            if (ec)
               return server_->fail(ec, "write");

            if (!keep_alive) {
               // This means we should close the connection, usually because
               // the response indicated the "Connection: close" semantic.
               return do_close();
            }

            // Read another request
            do_read();
         }

         void do_close() {
            // Send a TCP shutdown
            beast::error_code ec;
            stream_.socket().shutdown(tcp::socket::shutdown_send, ec);

            // At this point the connection is closed gracefully
         }
      };

      //------------------------------------------------------------------------------

      // Accepts incoming connections and launches the sessions
      class listener : public std::enable_shared_from_this<listener> {
         net::io_context& ioc_;
         tcp::acceptor    acceptor_;
         simple_server*   server_;

       public:
         listener(net::io_context& ioc, tcp::endpoint endpoint, simple_server* server)
             : ioc_(ioc), acceptor_(net::make_strand(ioc)), server_(server) {
            beast::error_code ec;

            // Open the acceptor
            acceptor_.open(endpoint.protocol(), ec);
            if (ec) {
               server_->fail(ec, "open");
               return;
            }

            // Allow address reuse
            acceptor_.set_option(net::socket_base::reuse_address(true), ec);
            if (ec) {
               server_->fail(ec, "set_option");
               return;
            }

            // Bind to the server address
            acceptor_.bind(endpoint, ec);
            if (ec) {
               server_->fail(ec, "bind");
               return;
            }

            // Start listening for connections
            acceptor_.listen(net::socket_base::max_listen_connections, ec);
            if (ec) {
               server_->fail(ec, "listen");
               return;
            }
         }

         // Start accepting incoming connections
         void run() { do_accept(); }

       private:
         void do_accept() {
            // The new connection gets its own strand
            acceptor_.async_accept(net::make_strand(ioc_),
                                   beast::bind_front_handler(&listener::on_accept, this->shared_from_this()));
         }

         void on_accept(beast::error_code ec, tcp::socket socket) {
            if (ec) {
               server_->fail(ec, "accept");
               return; // To avoid infinite loop
            } else {
               // Create the session and run it
               std::make_shared<session>(std::move(socket), server_)->run();
            }

            // Accept another connection
            do_accept();
         }
      };

    public:
      void run(net::io_context& ioc, tcp::endpoint endpoint) { std::make_shared<listener>(ioc, endpoint, this)->run(); }
   };
}} // namespace eosio::rest
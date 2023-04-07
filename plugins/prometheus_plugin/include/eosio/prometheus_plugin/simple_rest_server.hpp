#pragma once

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>

namespace eosio { namespace rest {

   // The majority of the code here are derived from boost source
   // libs/beast/example/http/server/async/http_server_async.cpp
   // with minimum modification and yet reusable.

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
         auto server_header = self()->server_header();
         // Returns a bad request response
         auto const bad_request = [&req, &server_header](std::string_view why) {
            http::response<http::string_body> res{ http::status::bad_request, req.version() };
            res.set(http::field::server, server_header);
            res.set(http::field::content_type, "text/plain");
            res.keep_alive(req.keep_alive());
            res.body() = std::string(why);
            res.prepare_payload();
            return res;
         };

         // Returns a not found response
         auto const not_found = [&req, &server_header](std::string_view target) {
            http::response<http::string_body> res{ http::status::not_found, req.version() };
            res.set(http::field::server, server_header);
            res.set(http::field::content_type, "text/plain");
            res.keep_alive(req.keep_alive());
            res.body() = "The resource '" + std::string(target) + "' was not found.";
            res.prepare_payload();
            return res;
         };

         // Returns a server error response
         auto const server_error = [&req, &server_header](std::string_view what) {
            http::response<http::string_body> res{ http::status::internal_server_error, req.version() };
            res.set(http::field::server, server_header);
            res.set(http::field::content_type, "text/plain");
            res.keep_alive(req.keep_alive());
            res.body() = "An error occurred: '" + std::string(what) + "'";
            res.prepare_payload();
            return res;
         };

         // Make sure we can handle the method
         if (!self()->allow_method(req.method()))
            return bad_request("Unknown HTTP-method");

         // Request path must be absolute and not contain "..".
         std::string_view target{req.target().data(), req.target().size()};
         if (target.empty() || target[0] != '/' || target.find("..") != std::string_view::npos)
            return bad_request("Illegal request-target");

         try {
            auto res = self()->on_request(std::move(req));
            if (!res)
               not_found(target);
            return *res;
         } catch (std::exception& ex) { return server_error(ex.what()); }
      }

      class session : public std::enable_shared_from_this<session> {
         tcp::socket                                        socket_;
         boost::asio::io_context::strand                    strand_;
         beast::flat_buffer                                 buffer_;
         http::request<http::string_body>                   req_;
         simple_server*                                     server_;
         std::shared_ptr<http::response<http::string_body>> res_;

       public:
         // Take ownership of the stream
         session(net::io_context& ioc, tcp::socket&& socket, simple_server* server)
             : socket_(std::move(socket)), strand_(ioc), server_(server) {}

         // Start the asynchronous operation
         void run() { do_read(); }

         void do_read() {
            // Make the request empty before reading,
            // otherwise the operation behavior is undefined.
            req_ = {};

            // Read a request
            http::async_read(
                  socket_, buffer_, req_,
                  boost::asio::bind_executor(strand_, [self = this->shared_from_this()](beast::error_code ec,
                                                                                        std::size_t bytes_transferred) {
                     self->on_read(ec, bytes_transferred);
                  }));
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
            // The lifetime of the message has to extend
            // for the duration of the async operation so
            // we use a shared_ptr to manage it.
            res_ = std::make_shared<http::response<http::string_body>>(std::move(msg));

            // Write the response
            http::async_write(socket_, *res_,
                              boost::asio::bind_executor(socket_.get_executor(),
                                                         [self = this->shared_from_this(), close = res_->need_eof()](
                                                               beast::error_code ec, std::size_t bytes_transferred) {
                                                            self->on_write(ec, bytes_transferred, close);
                                                         }));
         }

         void on_write(boost::system::error_code ec, std::size_t bytes_transferred, bool close) {
            boost::ignore_unused(bytes_transferred);

            if (ec)
               return server_->fail(ec, "write");

            if (close) {
               // This means we should close the connection, usually because
               // the response indicated the "Connection: close" semantic.
               return do_close();
            }

            // We're done with the response so delete it
            res_ = nullptr;

            // Read another request
            do_read();
         }

         void do_close() {
            // Send a TCP shutdown
            beast::error_code ec;
            socket_.shutdown(tcp::socket::shutdown_send, ec);

            // At this point the connection is closed gracefully
         }
      };

      //------------------------------------------------------------------------------

      // Accepts incoming connections and launches the sessions
      class listener : public std::enable_shared_from_this<listener> {
         net::io_context& ioc_;
         tcp::acceptor    acceptor_;
         tcp::socket      socket_;
         simple_server*   server_;

       public:
         listener(net::io_context& ioc, tcp::endpoint endpoint, simple_server* server)
             : ioc_(ioc), acceptor_(ioc), socket_(ioc), server_(server) {
            boost::system::error_code ec;

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
         void run() {
            if (!acceptor_.is_open())
               return;
            do_accept();
         }

       private:
         void do_accept() {
            acceptor_.async_accept(
                  socket_, [self = this->shared_from_this()](boost::system::error_code ec) { self->on_accept(ec); });
         }

         void on_accept(boost::system::error_code ec) {
            if (ec) {
               server_->fail(ec, "accept");
            } else {
               // Create the session and run it
               std::make_shared<session>(ioc_, std::move(socket_), server_)->run();
            }

            // Accept another connection
            do_accept();
         }
      };

    public:
      void run(net::io_context& ioc, tcp::endpoint endpoint) { std::make_shared<listener>(ioc, endpoint, this)->run(); }
   };
}} // namespace eosio::rest
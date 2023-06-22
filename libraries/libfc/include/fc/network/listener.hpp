#pragma once

#include <fc/log/logger.hpp>
#include <fc/scoped_exit.hpp>

#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ip/v6_only.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <filesystem>

namespace fc {

inline std::string to_string(const boost::asio::ip::tcp::endpoint& endpoint) {
   const auto& ip_addr        = endpoint.address();
   std::string ip_addr_string = ip_addr.to_string();
   if (ip_addr.is_v6()) {
      ip_addr_string = "[" + ip_addr_string + "]";
   }
   return ip_addr_string + ":" + std::to_string(endpoint.port());
}

inline std::pair<std::string, std::string> split_host_port(std::string_view endpoint) {
   std::string::size_type colon_pos = endpoint.rfind(':');
   if (colon_pos != std::string::npos) {
      auto port = endpoint.substr(colon_pos + 1);
      auto hostname =
            (endpoint[0] == '[' && colon_pos >= 2) ? endpoint.substr(1, colon_pos - 2) : endpoint.substr(0, colon_pos);
      return { std::string(hostname), std::string(port) };
   } else {
      return { std::string(endpoint), {} };
   }
}

/////////////////////////////////////////////////////////////////////////////////////////////
///
/// fc::listener is template class to simplify the code for accepting new socket connections.
/// It can be used for both tcp or Unix socket connection.
///
/// Example Usage:
/// \code{.cpp}
///
/// class shared_state_type;
///
/// template <typename Protocol>
/// struct example_session : std::enable_shared_from_this<example_session<Protocol>> {
///    using socket_type = Protocol::socket;
///    socket_type&& socket_;
///    shared_state_type& shared_state_;
///    example_session(socket_type&& socket, shared_state_type& shared_state)
///      : socket_(std::move(socket)), shared_state_(shared_state_) {}
///
///    // ...
///    void start();
/// };
///
/// template <typename Protocol>
/// struct example_listener : fc::listener<example_listener<Protocol>, Protocol>{
///    static constexpr uint32_t accept_timeout_ms = 200;
///    shared_state_type& shared_state_;
///
///    example_listener(boost::asio::io_context& executor,
///                     logger& logger,
///                     const std::string& local_address,
///                     const typename Protocol::endpoint& endpoint,
///                     shared_state_type& shared_state)
///    : fc::listener<example_listener<Protocol>, Protocol>
///        (executor, logger, boost::posix_time::milliseconds(accept_timeout_ms), local_address, endpoint)
///    , shared_state_(shared_state) {}
///
///    std::string extra_listening_log_info() {
///       return shared_state_.info_to_be_printed_after_address_is_resolved_and_listening;
///    }
///
///    void create_session(Protocol::socket&& sock) {
///        auto session = std::make_shared<example_session>(std::move(sock), shared_state_);
///        session->start();
///    }
///  };
///
///  int main() {
///    boost::asio::io_context ioc;
///    fc::logger logger = fc::logger::get(DEFAULT_LOGGER);
///    shared_state_type shared_state{...};
///
///    // usage for accepting tcp connection
///    // notice that it only throws std::system_error, not fc::exception
///    example_listener<boost::asio::ip::tcp>::create(executor, logger, "localhost:8080", std::ref(shared_state));
///
///    // usage for accepting unix socket connection
///    example_listener<boost::asio::local::stream_protocol>::create(executor, logger, "tmp.sock",
///    std::ref(shared_state));
///
///    ioc.run();
///    return 0;
///  }
/// \endcode
///
/////////////////////////////////////////////////////////////////////////////////////////////

template <typename T, typename Protocol>
struct listener : std::enable_shared_from_this<T> {
   using endpoint_type = typename Protocol::endpoint;

   typename Protocol::acceptor      acceptor_;
   boost::asio::deadline_timer      accept_error_timer_;
   boost::posix_time::time_duration accept_timeout_;
   logger&                          logger_;
   std::string                      local_address_;

   listener(boost::asio::io_context& executor, logger& logger, boost::posix_time::time_duration accept_timeout,
            const std::string& local_address, const endpoint_type& endpoint)
       : acceptor_(executor, endpoint), accept_error_timer_(executor), accept_timeout_(accept_timeout), logger_(logger),
         local_address_(std::is_same_v<Protocol, boost::asio::ip::tcp>
                              ? local_address
                              : std::filesystem::absolute(local_address).string()) {}

   ~listener() {
      if constexpr (std::is_same_v<Protocol, boost::asio::local::stream_protocol>) {
         std::filesystem::remove(local_address_);
      }
   }

   void do_accept() {
      acceptor_.async_accept([self = this->shared_from_this()](boost::system::error_code ec, auto&& peer_socket) {
         self->on_accept(ec, std::forward<decltype(peer_socket)>(peer_socket));
      });
   }

   template <typename Socket>
   void on_accept(boost::system::error_code ec, Socket&& socket) {
      if (!ec) {
         static_cast<T*>(this)->create_session(std::forward<Socket>(socket));
         do_accept();
      } else if (ec == boost::system::errc::too_many_files_open) {
         // retry accept() after timeout to avoid cpu loop on accept
         fc_elog(logger_, "open file limit reached: not accepting new connections for next ${timeout}ms",
                 ("timeout", accept_timeout_.total_milliseconds()));
         accept_error_timer_.expires_from_now(accept_timeout_);
         accept_error_timer_.async_wait([self = this->shared_from_this()](boost::system::error_code ec) {
            if (!ec)
               self->do_accept();
         });
      } else if (int code = ec.value(); code == ENETDOWN || code == EPROTO || code == ENOPROTOOPT ||
                                        code == EHOSTDOWN || code == EHOSTUNREACH || code == EOPNOTSUPP ||
                                        code == ENETUNREACH
#ifdef ENONET
                                        || code == ENONET
#endif
#ifdef __APPLE__
                                        //guard against failure of asio's internal SO_NOSIGPIPE call after accept()
                                        || code == EINVAL
#endif
      ) {
         // according to https://man7.org/linux/man-pages/man2/accept.2.html, reliable application should
         // retry when these error codes are returned
         fc_wlog(logger_, "closing connection, accept error: ${m}", ("m", ec.message()));
         do_accept();
      } else {
         fc_elog(logger_, "Unrecoverable accept error, stop listening: ${m}", ("m", ec.message()));
      }
   }

   const char* extra_listening_log_info() { return ""; }

   void log_listening(const endpoint_type& endpoint, const std::string& local_address) {
      std::string info;
      if constexpr (std::is_same_v<Protocol, boost::asio::ip::tcp>) {
         info = fc::to_string(endpoint) + " resolved from " + local_address;
      } else {
         info = "Unix socket " + local_address;
      }
      info += static_cast<T*>(this)->extra_listening_log_info();
      fc_ilog(logger_, "start listening on ${info}", ("info", info));
   }

   /// @brief Create listeners to listen on endpoints resolved from address
   /// @param ...args  The arguments to forward to the listener constructor so that they can be accessed
   ///                 from create_session() to construct the customized session objects.
   /// @throws std::system_error
   template <typename... Args>
   static void create(boost::asio::io_context& executor, logger& logger, const std::string& address, Args&&... args) {
      using tcp = boost::asio::ip::tcp;
      if constexpr (std::is_same_v<Protocol, tcp>) {
         auto [host, port] = split_host_port(address);
         if (port.empty()) {
            fc_elog(logger, "port is not specified for address ${addr}", ("addr", address));
            throw std::system_error(std::make_error_code(std::errc::bad_address));
         }

         boost::system::error_code ec;
         tcp::resolver             resolver(executor);
         auto                      endpoints = resolver.resolve(host, port, tcp::resolver::passive, ec);
         if (ec) {
            fc_elog(logger, "failed to resolve address: ${msg}", ("msg", ec.message()));
            throw std::system_error(ec);
         }

         int                          listened = 0;
         std::optional<tcp::endpoint> unspecified_ipv4_addr;
         bool                         has_unspecified_ipv6_only = false;

         auto create_server = [&](const auto& endpoint) {
            const auto& ip_addr = endpoint.address();
            try {
               auto server = std::make_shared<T>(executor, logger, address, endpoint, std::forward<Args&&>(args)...);
               server->log_listening(endpoint, address);
               server->do_accept();
               ++listened;
               has_unspecified_ipv6_only = ip_addr.is_unspecified() && ip_addr.is_v6();
               if (has_unspecified_ipv6_only) {
                  boost::asio::ip::v6_only option;
                  server->acceptor_.get_option(option);
                  has_unspecified_ipv6_only &= option.value();
               }

            } catch (boost::system::system_error& ex) {
               fc_wlog(logger, "unable to listen on ${ip_addr}:${port} resolved from ${address}: ${msg}",
                       ("ip_addr", ip_addr.to_string())("port", endpoint.port())("address", address)("msg", ex.what()));
            }
         };

         for (const auto& ep : endpoints) {
            const auto& endpoint = ep.endpoint();
            const auto& ip_addr  = endpoint.address();
            if (ip_addr.is_unspecified() && ip_addr.is_v4() && endpoints.size() > 1) {
               // it is an error to bind a socket to the same port for both ipv6 and ipv4 INADDR_ANY address when
               // the system has ipv4-mapped ipv6 enabled by default, we just skip the ipv4 for now.
               unspecified_ipv4_addr = endpoint;
               continue;
            }
            create_server(endpoint);
         }

         if (unspecified_ipv4_addr.has_value() && has_unspecified_ipv6_only) {
            create_server(*unspecified_ipv4_addr);
         }

         if (listened == 0) {
            fc_elog(logger, "none of the addresses resolved from ${addr} can be listened to", ("addr", address));
            throw std::system_error(std::make_error_code(std::errc::bad_address));
         }
      } else {
         using stream_protocol = boost::asio::local::stream_protocol;
         static_assert(std::is_same_v<Protocol, stream_protocol>);

         namespace fs       = std::filesystem;
         auto     cwd       = fs::current_path();
         fs::path sock_path = address;

         fs::create_directories(sock_path.parent_path());
         // The maximum length of the socket path is defined by sockaddr_un::sun_path. On Linux,
         // according to unix(7), it is 108 bytes. On FreeBSD, according to unix(4), it is 104 bytes.
         // Therefore, we create the unix socket with the relative path to its parent path to avoid the
         // problem.
         fs::current_path(sock_path.parent_path());
         auto restore = fc::make_scoped_exit([cwd] { fs::current_path(cwd); });

         endpoint_type endpoint{ sock_path.filename().string() };

         boost::system::error_code ec;
         stream_protocol::socket   test_socket(executor);
         test_socket.connect(endpoint, ec);

         // looks like a service is already running on that socket, don't touch it... fail out
         if (ec == boost::system::errc::success) {
            fc_elog(logger, "The unix socket path ${addr} is already in use", ("addr", address));
            throw std::system_error(std::make_error_code(std::errc::address_in_use));
         }
         // socket exists but no one home, go ahead and remove it and continue on
         else if (ec == boost::system::errc::connection_refused)
            fs::remove(sock_path);

         auto server = std::make_shared<T>(executor, logger, address, endpoint, std::forward<Args&&>(args)...);
         server->log_listening(endpoint, address);
         server->do_accept();
      }
   }
};
} // namespace fc

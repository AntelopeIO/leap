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

template <typename Protocol>
struct listener_base;

template <>
struct listener_base<boost::asio::ip::tcp> {
   listener_base(const std::string&) {}
};

template <>
struct listener_base<boost::asio::local::stream_protocol> {
   std::filesystem::path path_;
   listener_base(const std::string& local_address) : path_(std::filesystem::absolute(local_address)) {}
   ~listener_base() {
      std::error_code ec;
      std::filesystem::remove(path_, ec);
   }
};

////////////////////////////////////////////////////////////////////////////////////////////////////
///
/// @brief fc::listener is template class to simplify the code for accepting new socket connections.
/// It can be used for both tcp or Unix socket connection.
///
/// @note Users should use fc::create_listener() instead, this class is the implementation
/// detail for fc::create_listener().
///
/////////////////////////////////////////////////////////////////////////////////////////////
template <typename Protocol, typename CreateSession>
struct listener : listener_base<Protocol>, std::enable_shared_from_this<listener<Protocol, CreateSession>> {
 private:
   typename Protocol::acceptor      acceptor_;
   boost::asio::deadline_timer      accept_error_timer_;
   boost::posix_time::time_duration accept_timeout_;
   logger&                          logger_;
   std::string                      extra_listening_log_info_;
   CreateSession                    create_session_;

 public:
   using endpoint_type = typename Protocol::endpoint;
   listener(boost::asio::io_context& executor, logger& logger, boost::posix_time::time_duration accept_timeout,
            const std::string& local_address, const endpoint_type& endpoint,
            const std::string& extra_listening_log_info, const CreateSession& create_session)
       : listener_base<Protocol>(local_address), acceptor_(executor, endpoint), accept_error_timer_(executor),
         accept_timeout_(accept_timeout), logger_(logger), extra_listening_log_info_(extra_listening_log_info),
         create_session_(create_session) {}

   const auto& acceptor() const { return acceptor_; }

   void do_accept() {
      acceptor_.async_accept([self = this->shared_from_this()](boost::system::error_code ec, auto&& peer_socket) {
         self->on_accept(ec, std::forward<decltype(peer_socket)>(peer_socket));
      });
   }

   template <typename Socket>
   void on_accept(boost::system::error_code ec, Socket&& socket) {
      if (!ec) {
         create_session_(std::forward<Socket>(socket));
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

   void log_listening(const endpoint_type& endpoint, const std::string& local_address) {
      std::string info;
      if constexpr (std::is_same_v<Protocol, boost::asio::ip::tcp>) {
         info = fc::to_string(endpoint) + " resolved from " + local_address;
      } else {
         info = "Unix socket " + local_address;
      }
      info += extra_listening_log_info_;
      fc_ilog(logger_, "start listening on ${info}", ("info", info));
   }
};

/// @brief create a stream-oriented socket listener which listens on the specified \c address and calls \c
/// create_session whenever a socket is accepted.
///
/// @details
/// This function is used for listening on TCP or Unix socket address and creating corresponding session  when the
/// socket is accepted.
///
/// For TCP socket, the address format can be <hostname>:port or <ipaddress>:port where the `:port` part  is mandatory.
/// If only the port is specified, all network interfaces are listened. The function can listen on multiple IP addresses
/// if the specified hostname is resolved to multiple IP addresses; in other words, it can create more than one
/// fc::listener objects. If port is not specified or none of the resolved address can be listened, an std::system_error
/// with std::errc::bad_address error code will be thrown.
///
/// For Unix socket, this function will temporary change current working directory to the parent of the specified \c
/// address (i.e. socket file path), listen on the filename component of the path, and then restore the working
/// directory before return. This is the workaround for the socket file paths limitation which is around 100 characters.
///
/// The lifetime of the created listener objects is controlled by \c executor, the created objects will be destroyed
/// when \c executor.stop() is called.
///
/// @note
/// This function is not thread safe for Unix socket because it will temporarily change working directory without any
/// lock. Any code which depends the current working directory (such as opening files with relative paths) in other
/// threads should be protected.
///
/// @tparam Protocol either \c boost::asio::ip::tcp or \c boost::asio::local::stream_protocol
/// @throws std::system_error or boost::system::system_error
template <typename Protocol, typename CreateSession>
void create_listener(boost::asio::io_context& executor, logger& logger, boost::posix_time::time_duration accept_timeout,
                     const std::string& address, const std::string& extra_listening_log_info,
                     const CreateSession& create_session) {
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

      auto create_listener = [&](const auto& endpoint) {
         const auto& ip_addr = endpoint.address();
         try {
            auto listener = std::make_shared<fc::listener<Protocol, CreateSession>>(
                  executor, logger, accept_timeout, address, endpoint, extra_listening_log_info, create_session);
            listener->log_listening(endpoint, address);
            listener->do_accept();
            ++listened;
            has_unspecified_ipv6_only = ip_addr.is_unspecified() && ip_addr.is_v6();
            if (has_unspecified_ipv6_only) {
               boost::asio::ip::v6_only option;
               listener->acceptor().get_option(option);
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
         create_listener(endpoint);
      }

      if (unspecified_ipv4_addr.has_value() && has_unspecified_ipv6_only) {
         create_listener(*unspecified_ipv4_addr);
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

      stream_protocol::endpoint endpoint{ sock_path.filename().string() };

      boost::system::error_code ec;
      stream_protocol::socket   test_socket(executor);
      test_socket.connect(endpoint, ec);

      // looks like a service is already running on that socket, don't touch it... fail out
      if (ec == boost::system::errc::success) {
         fc_elog(logger, "The unix socket path ${addr} is already in use", ("addr", address));
         throw std::system_error(std::make_error_code(std::errc::address_in_use));
      }
      else if (ec == boost::system::errc::connection_refused) {
         // socket exists but no one home, go ahead and remove it and continue on
         fs::remove(sock_path);
      }

      auto listener = std::make_shared<fc::listener<stream_protocol, CreateSession>>(
            executor, logger, accept_timeout, address, endpoint, extra_listening_log_info, create_session);
      listener->log_listening(endpoint, address);
      listener->do_accept();
   }
}
} // namespace fc

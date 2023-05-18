#pragma once
#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ip/v6_only.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <filesystem>
#ifdef __cpp_concepts
#include <concepts>
#endif

#include <fc/log/logger.hpp>
#include <fc/scoped_exit.hpp>

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

#ifdef __cpp_concepts
template <class T>
concept listener_state = requires(T state) {
   { state->thread_pool.get_executor() } -> std::same_as<boost::asio::io_context&>;
   { state->get_logger() } -> std::same_as<fc::logger&>;
};
#endif

template <typename T, typename Protocol, typename State>
#ifdef __cpp_concepts
   requires listener_state<State>
#endif
struct listener : std::enable_shared_from_this<T> {
   using endpoint_type = typename Protocol::endpoint;

   State                       state_;
   typename Protocol::acceptor acceptor_;
   boost::asio::deadline_timer accept_error_timer_;

   listener(State state, const endpoint_type& endpoint)
       : state_(state), acceptor_(state->thread_pool.get_executor(), endpoint),
         accept_error_timer_(state->thread_pool.get_executor()) {}

   void do_accept() {
      acceptor_.async_accept(state_->thread_pool.get_executor(),
                             [self = this->shared_from_this()](boost::system::error_code ec, auto&& peer) {
                                self->on_accept(ec, std::forward<decltype(peer)>(peer));
                             });
   }

   template <typename Socket>
   void on_accept(boost::system::error_code ec, Socket&& socket) {
      if (!ec) {
         static_cast<T*>(this)->create_session(std::forward<Socket>(socket));
         do_accept();
      } else if (ec == boost::system::errc::too_many_files_open) {
         // retry accept() after timeout to avoid cpu loop on accept
         auto timeout_ms = static_cast<T*>(this)->accept_timeout_ms;
         fc_elog(state_->get_logger(), "accept: too many files open - waiting ${timeout}ms", ("timeout", timeout_ms));
         accept_error_timer_.expires_from_now(boost::posix_time::milliseconds(timeout_ms));
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
      ) {
         // according to https://man7.org/linux/man-pages/man2/accept.2.html, reliable application should
         // retry when these error codes are returned
         fc_elog(state_->get_logger(), "accept: ${m}", ("m", ec.message()));
         fc_elog(state_->get_logger(), "closing connection");
         do_accept();
      } else {
         fc_elog(state_->get_logger(), "Unrecoverable accept error, stop listening: ${msg}", ("m", ec.message()));
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
      fc_ilog(state_->get_logger(), "start listening on ${ep}", ("info", info));
   }

   template <typename... Args>
   static void create(State state, const std::string& address, Args&&... args) {
      using tcp = boost::asio::ip::tcp;

      if constexpr (std::is_same_v<Protocol, tcp>) {
         auto [host, port] = split_host_port(address);
         if (port.empty()) {
            fc_elog(state->get_logger(), "port is not specified for address ${addr}", ("addr", address));
            throw std::system_error(std::make_error_code(std::errc::bad_address));
         }

         boost::system::error_code ec;
         tcp::resolver             resolver(state->thread_pool.get_executor());
         auto                      endpoints = resolver.resolve(host, port, tcp::resolver::passive, ec);
         if (ec) {
            fc_elog(state->get_logger(), "failed to resolve address: ${msg}", ("msg", ec.message()));
            throw std::system_error(ec);
         }

         int                          listened = 0;
         std::optional<tcp::endpoint> unspecified_ipv4_addr;
         bool                         has_unspecified_ipv6_only = false;

         auto create_server = [&](const auto& endpoint) {
            const auto& ip_addr = endpoint.address();
            try {
               auto server = std::make_shared<T>(state, endpoint, address, std::forward<Args&&>(args)...);
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
               fc_wlog(state->get_logger(), "unable to listen on ${ip_addr}:${port} resolved from ${address}: ${msg}",
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
            fc_elog(state->get_logger(), "none of the addresses resolved from ${addr} can be listened to",
                    ("addr", address));
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
         stream_protocol::socket   test_socket(state->thread_pool.get_executor());
         test_socket.connect(endpoint, ec);

         // looks like a service is already running on that socket, don't touch it... fail out
         if (ec == boost::system::errc::success) {
            fc_elog(state->get_logger(), "The unix socket path ${addr} is already in use", ("addr", address));
            throw std::system_error(std::make_error_code(std::errc::address_in_use));
         }
         // socket exists but no one home, go ahead and remove it and continue on
         else if (ec == boost::system::errc::connection_refused)
            fs::remove(sock_path);

         auto server = std::make_shared<T>(state, endpoint, address, std::forward<Args&&>(args)...);
         server->do_accept();
      }
   }
};
} // namespace fc

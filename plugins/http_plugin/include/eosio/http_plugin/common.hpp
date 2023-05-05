#pragma once

#include <eosio/chain/thread_utils.hpp>// for thread pool
#include <eosio/http_plugin/http_plugin.hpp>

#include <fc/io/raw.hpp>
#include <fc/log/logger_config.hpp>
#include <fc/time.hpp>
#include <fc/utility.hpp>

#include <boost/asio.hpp>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/make_unique.hpp>
#include <boost/optional.hpp>

#include <boost/asio/basic_socket_acceptor.hpp>
#include <boost/asio/basic_socket_iostream.hpp>
#include <boost/asio/basic_stream_socket.hpp>
#include <boost/asio/detail/config.hpp>

#include <atomic>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <string>

namespace eosio {
static uint16_t const uri_default_port = 80;
/// Default port for wss://
static uint16_t const uri_default_secure_port = 443;

using std::map;
using std::set;
using std::string;

namespace beast = boost::beast;     // from <boost/beast.hpp>
namespace http = boost::beast::http;// from <boost/beast/http.hpp>
namespace asio = boost::asio;
using boost::asio::ip::tcp;// from <boost/asio/ip/tcp.hpp>


namespace detail {
/**
* virtualized wrapper for the various underlying connection functions needed in req/resp processng
*/
struct abstract_conn {
   virtual ~abstract_conn() = default;
   virtual std::string verify_max_bytes_in_flight(size_t extra_bytes) = 0;
   virtual std::string verify_max_requests_in_flight() = 0;
   virtual void send_busy_response(std::string&& what) = 0;
   virtual void handle_exception() = 0;

   virtual void send_response(std::string&& json_body, unsigned int code) = 0;
};

using abstract_conn_ptr = std::shared_ptr<abstract_conn>;

/**
* internal url handler that contains more parameters than the handlers provided by external systems
*/
using internal_url_handler_fn = std::function<void(abstract_conn_ptr, string&&, string&&, url_response_callback&&)>;
struct internal_url_handler {
   internal_url_handler_fn fn;
   http_content_type content_type = http_content_type::json;
};
/**
* Helper method to calculate the "in flight" size of a fc::variant
* This is an estimate based on fc::raw::pack if that process can be successfully executed
*
* @param v - the fc::variant
* @return in flight size of v
*/
static size_t in_flight_sizeof(const fc::variant& v) {
   try {
      return fc::raw::pack_size(v);
   } catch(...) {}
   return 0;
}

/**
* Helper method to calculate the "in flight" size of a std::optional<T>
* When the optional doesn't contain value, it will return the size of 0
*
* @param o - the std::optional<T> where T is typename
* @return in flight size of o
*/
template<typename T>
static size_t in_flight_sizeof(const std::optional<T>& o) {
   if(o) {
      return in_flight_sizeof(*o);
   }
   return 0;
}

}// namespace detail

// key -> priority, url_handler
typedef map<string, detail::internal_url_handler> url_handlers_type;

struct http_plugin_state {
   string access_control_allow_origin;
   string access_control_allow_headers;
   string access_control_max_age;
   bool access_control_allow_credentials = false;
   size_t max_body_size{2 * 1024 * 1024};

   std::atomic<size_t> bytes_in_flight{0};
   std::atomic<int32_t> requests_in_flight{0};
   size_t max_bytes_in_flight = 0;
   int32_t max_requests_in_flight = -1;
   fc::microseconds max_response_time{30 * 1000};

   bool validate_host = true;
   set<string> valid_hosts;

   string server_header;

   url_handlers_type url_handlers;
   bool keep_alive = false;

   uint16_t thread_pool_size = 2;
   struct http; // http is a namespace so use an embedded type for the named_thread_pool tag
   eosio::chain::named_thread_pool<http> thread_pool;

   fc::logger& logger;
   std::function<void(http_plugin::metrics)> update_metrics;

   explicit http_plugin_state(fc::logger& log)
       : logger(log) {}

   fc::time_point get_max_response_deadline() const {
      return max_response_time == fc::microseconds::maximum() ? fc::time_point::maximum()
                                                              : fc::time_point::now() + max_response_time;
   }
};

/**
* Construct a lambda appropriate for url_response_callback that will
* JSON-stringify the provided response
*
* @param plugin_state - plugin state object, shared state of http_plugin
* @param session_ptr - beast_http_session object on which to invoke send_response
* @return lambda suitable for url_response_callback
*/
auto make_http_response_handler(std::shared_ptr<http_plugin_state> plugin_state, detail::abstract_conn_ptr session_ptr, http_content_type content_type) {
   return [plugin_state{std::move(plugin_state)},
           session_ptr{std::move(session_ptr)}, content_type](int code, std::optional<fc::variant> response) {
      auto payload_size = detail::in_flight_sizeof(response);
      if(auto error_str = session_ptr->verify_max_bytes_in_flight(payload_size); !error_str.empty()) {
         session_ptr->send_busy_response(std::move(error_str));
         return;
      }

      plugin_state->bytes_in_flight += payload_size;

      // post back to an HTTP thread to allow the response handler to be called from any thread
      boost::asio::post(plugin_state->thread_pool.get_executor(),
                        [plugin_state, session_ptr, code, payload_size, response = std::move(response), content_type]() {
                           try {
                              plugin_state->bytes_in_flight -= payload_size;
                              if (response.has_value()) {
                                 std::string json = (content_type == http_content_type::plaintext) ? response->as_string() : fc::json::to_string(*response, fc::time_point::maximum());
                                 if (auto error_str = session_ptr->verify_max_bytes_in_flight(json.size()); error_str.empty())
                                    session_ptr->send_response(std::move(json), code);
                                 else
                                    session_ptr->send_busy_response(std::move(error_str));
                              } else {
                                 session_ptr->send_response("{}", code);
                              }
                           } catch (...) {
                              session_ptr->handle_exception();
                           }
                        });
   };// end lambda

}

bool host_port_is_valid(const http_plugin_state& plugin_state,
                        const std::string& header_host_port,
                        const string& endpoint_local_host_port) {
   return !plugin_state.validate_host || header_host_port == endpoint_local_host_port || plugin_state.valid_hosts.find(header_host_port) != plugin_state.valid_hosts.end();
}

bool host_is_valid(const http_plugin_state& plugin_state,
                   const std::string& host,
                   const string& endpoint_local_host_port,
                   bool secure) {
   if(!plugin_state.validate_host) {
      return true;
   }

   // normalise the incoming host so that it always has the explicit port
   static auto has_port_expr = std::regex("[^:]:[0-9]+$");/// ends in :<number> without a preceeding colon which implies ipv6
   if(std::regex_search(host, has_port_expr)) {
      return host_port_is_valid(plugin_state, host, endpoint_local_host_port);
   } else {
      // according to RFC 2732 ipv6 addresses should always be enclosed with brackets so we shouldn't need to special case here
      return host_port_is_valid(plugin_state,
                                host + ":" + std::to_string(secure ? uri_default_secure_port : uri_default_port),
                                endpoint_local_host_port);
   }
}

}// end namespace eosio

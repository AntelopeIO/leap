#include <eosio/http_plugin/http_plugin.hpp>
#include <eosio/http_plugin/common.hpp>
#include <eosio/http_plugin/beast_http_listener.hpp>
#include <eosio/chain/exceptions.hpp>

#include <fc/log/logger_config.hpp>
#include <fc/reflect/variant.hpp>
#include <fc/scoped_exit.hpp>

#include <boost/asio.hpp>
#include <boost/optional.hpp>

#include <memory>
#include <regex>

namespace eosio {

   namespace {
      inline fc::logger& logger() {
         static fc::logger log{ "http_plugin" };
         return log;
      }
   }

   static auto _http_plugin = application::register_plugin<http_plugin>();

   using std::vector;
   using std::string;
   using std::regex;
   using boost::asio::ip::tcp;
   using std::shared_ptr;

   static http_plugin_defaults current_http_plugin_defaults;
   static bool verbose_http_errors = false;
   static std::promise<http_plugin*>* plugin_promise;

   void http_plugin::set_defaults(const http_plugin_defaults& config) {
      current_http_plugin_defaults = config;
   }

   std::string http_plugin::get_server_header() {
      return current_http_plugin_defaults.server_header;
   }

   using http_plugin_impl_ptr = std::shared_ptr<class http_plugin_impl>;

   api_category to_category(std::string_view name) {
      if (name == "chain_ro") return api_category::chain_ro;
      if (name == "chain_rw") return api_category::chain_rw;
      if (name == "db_size") return api_category::db_size;
      if (name == "net_ro") return api_category::net_ro;
      if (name == "net_rw") return api_category::net_rw;
      if (name == "producer_ro") return api_category::producer_ro;
      if (name == "producer_rw") return api_category::producer_rw;
      if (name == "snapshot") return api_category::snapshot;
      if (name == "trace_api") return api_category::trace_api;
      if (name == "prometheus") return api_category::prometheus;
      if (name == "test_control") return api_category::test_control;
      return api_category::unknown;
   }

   class http_plugin_impl : public std::enable_shared_from_this<http_plugin_impl> {
      public:
         http_plugin_impl() = default;

         http_plugin_impl(const http_plugin_impl&) = delete;
         http_plugin_impl(http_plugin_impl&&) = delete;

         http_plugin_impl& operator=(const http_plugin_impl&) = delete;
         http_plugin_impl& operator=(http_plugin_impl&&) = delete;

         std::string           http_server_address;
         std::string           unix_sock_path;

         std::map<std::string, api_category_set> categories_by_address;

         // std::vector<shared_ptr<void>>  beast_servers;

         shared_ptr<http_plugin_state> plugin_state = std::make_shared<http_plugin_state>(logger());

         /**
          * Make an internal_url_handler that will run the url_handler on the app() thread and then
          * return to the http thread pool for response processing
          *
          * @pre b.size() has been added to bytes_in_flight by caller
          * @param priority - priority to post to the app thread at
          * @param to_queue - execution queue to post to
          * @param next - the next handler for responses
          * @param my - the http_plugin_impl
          * @param content_type - json or plain txt
          * @return the constructed internal_url_handler
          */
         static detail::internal_url_handler make_app_thread_url_handler(api_entry&& entry, appbase::exec_queue to_queue, int priority, http_plugin_impl_ptr my, http_content_type content_type ) {
            detail::internal_url_handler handler;
            handler.content_type = content_type;
            handler.category = entry.category;
            auto next_ptr = std::make_shared<url_handler>(std::move(entry.handler));
            handler.fn = [my=std::move(my), priority, to_queue, next_ptr=std::move(next_ptr)]
                       ( detail::abstract_conn_ptr conn, string&& r, string&& b, url_response_callback&& then ) {
               if (auto error_str = conn->verify_max_bytes_in_flight(b.size()); !error_str.empty()) {
                  conn->send_busy_response(std::move(error_str));
                  return;
               }

               url_response_callback wrapped_then = [then=std::move(then)](int code, const fc::time_point& deadline, std::optional<fc::variant> resp) {
                  then(code, deadline, std::move(resp));
               };

               // post to the app thread taking shared ownership of next (via std::shared_ptr),
               // sole ownership of the tracked body and the passed in parameters
               // we can't std::move() next_ptr because we post a new lambda for each http request and we need to keep the original
               app().executor().post( priority, to_queue, [next_ptr, conn=std::move(conn), r=std::move(r), b = std::move(b), wrapped_then=std::move(wrapped_then)]() mutable {
                  try {
                     if( app().is_quiting() ) return; // http_plugin shutting down, do not call callback
                     // call the `next` url_handler and wrap the response handler
                     (*next_ptr)( std::move(r), std::move(b), std::move(wrapped_then)) ;
                  } catch( ... ) {
                     conn->handle_exception();
                  }
               } );
            };
            return handler;
         }

         /**
          * Make an internal_url_handler that will run the url_handler directly
          *
          * @pre b.size() has been added to bytes_in_flight by caller
          * @param next - the next handler for responses
          * @return the constructed internal_url_handler
          */
         static detail::internal_url_handler make_http_thread_url_handler(api_entry&& entry, http_content_type content_type) {
            detail::internal_url_handler handler;
            handler.content_type = content_type;
            handler.category = entry.category;
            handler.fn = [next=std::move(entry.handler)]( const detail::abstract_conn_ptr& conn, string&& r, string&& b, url_response_callback&& then ) mutable {
               try {
                  next(std::move(r), std::move(b), std::move(then));
               } catch( ... ) {
                  conn->handle_exception();
               }
             };
            return handler;
         }


         void create_beast_server(std::string address, api_category_set categories) {

            EOS_ASSERT(address.size() >= 2, chain::plugin_config_exception, "Invalid http server address: ${addr}",
                       ("addr", address));

            try {
               if (address[0] == '/' ||
                   (address[0] == '.' &&
                    (address[1] == '/' || (address.size() >= 3 && address[1] == '.' && address[2] == '/')))) {

                  auto                  cwd       = std::filesystem::current_path();
                  std::filesystem::path sock_path = address;
                  if (sock_path.is_relative())
                     sock_path = std::filesystem::weakly_canonical(app().data_dir() / sock_path);
                  ::unlink(sock_path.c_str());
                  std::filesystem::create_directories(sock_path.parent_path());
                  // The maximum length of the socket path is defined by sockaddr_un::sun_path. On Linux,
                  // according to unix(7), it is 108 bytes. On FreeBSD, according to unix(4), it is 104 bytes.
                  // Therefore, we create the unix socket with the relative path to its parent path to avoid the problem.
                  std::filesystem::current_path(sock_path.parent_path());
                  auto restore = fc::make_scoped_exit([cwd]{
                     std::filesystem::current_path(cwd);
                  });

                  using stream_protocol = asio::local::stream_protocol;
                  auto server = std::make_shared<beast_http_listener<stream_protocol::socket>>(
                        plugin_state, categories, stream_protocol::endpoint{ sock_path.filename().string() });
                  server->do_accept();
                  // beast_servers.push_back(server);
                  fc_ilog(logger(), "created beast UNIX socket listener at ${addr}", ("addr", sock_path));
               } else {

                  auto [host, port] = split_host_port(address);
                  EOS_ASSERT(port.size(), chain::plugin_config_exception, "port is not specified");

                  boost::system::error_code ec;
                  tcp::resolver resolver( app().get_io_service());
                  auto endpoints = resolver.resolve(host, port, boost::asio::ip::tcp::resolver::passive, ec);
                  EOS_ASSERT(!ec, chain::plugin_config_exception, "failed to resolve address: ${msg}",
                             ("msg", ec.message()));

                  int listened = 0;
                  std::optional<boost::asio::ip::tcp::endpoint> unspecified_ipv4_addr;
                  bool has_unspecified_ipv6_only = false;

                  auto create_ip_server = [&](auto endpoint) {
                     const auto& ip_addr = endpoint.address();
                     try {
                        auto server = std::make_shared<beast_http_listener<tcp_socket_t>>(
                              plugin_state, categories, endpoint);
                        server->do_accept();
                        ++listened;
                        fc_ilog(logger(), "start listening on ${ip_addr}:${port} for http requests (boost::beast)",
                                ("ip_addr", ip_addr.to_string())("port", endpoint.port()));
                        has_unspecified_ipv6_only = ip_addr.is_unspecified() && ip_addr.is_v6() &&
                            server->is_ip_v6_only();
                        
                     } catch (boost::system::system_error& ex) {
                        fc_wlog(logger(), "unable to listen on ${ip_addr}:${port} resolved from ${address}: ${msg}",
                                ("ip_addr", ip_addr.to_string())("port", endpoint.port())("address", address)("msg",
                                                                                                           ex.what()));
                     }
                  };

                  for (auto ep: endpoints) {
                     const auto& endpoint = ep.endpoint();
                     const auto& ip_addr = endpoint.address();
                     if (ip_addr.is_unspecified() && ip_addr.is_v4() && endpoints.size() > 1) {
                        // it is an error to bind a socket to the same port for both ipv6 and ipv4 INADDR_ANY address when
                        // the system has ipv4-mapped ipv6 enabled by default, we just skip the ipv4 for now.
                        unspecified_ipv4_addr = endpoint;
                        continue;
                     }
                     create_ip_server(endpoint);
                  }

                  if (unspecified_ipv4_addr.has_value() && has_unspecified_ipv6_only) {
                     create_ip_server(*unspecified_ipv4_addr);
                  }

                  EOS_ASSERT (listened > 0, chain::plugin_config_exception, "none of the resolved address can be listened" );
               }
            } catch (const fc::exception& e) {
               fc_elog(logger(), "http service failed to start for ${addr}: ${e}",
                       ("addr", address)("e", e.to_detail_string()));
               throw;
            } catch (const std::exception& e) {
               fc_elog(logger(), "http service failed to start for ${addr}: ${e}", ("addr", address)("e", e.what()));
               throw;
            } catch (...) {
               fc_elog(logger(), "error thrown from http io service");
               throw;
            }
         }
   };

   http_plugin::http_plugin():my(new http_plugin_impl()){
   }
   http_plugin::~http_plugin() = default;

   void http_plugin::set_program_options(options_description&, options_description& cfg) {
      if(current_http_plugin_defaults.default_unix_socket_path.length())
         cfg.add_options()
            ("unix-socket-path", bpo::value<string>()->default_value(current_http_plugin_defaults.default_unix_socket_path),
             "The filename (relative to data-dir) to create a unix socket for HTTP RPC; set blank to disable.");
      else
         cfg.add_options()
            ("unix-socket-path", bpo::value<string>(),
             "The filename (relative to data-dir) to create a unix socket for HTTP RPC; set blank to disable.");

      if(current_http_plugin_defaults.default_http_port)
         cfg.add_options()
            ("http-server-address", bpo::value<string>()->default_value("127.0.0.1:" + std::to_string(current_http_plugin_defaults.default_http_port)),
             "The local IP and port to listen for incoming http connections; set blank to disable.");
      else
         cfg.add_options()
            ("http-server-address", bpo::value<string>(),
             "The local IP and port to listen for incoming http connections; leave blank to disable.");

      if (current_http_plugin_defaults.support_categories) {
         cfg.add_options()
            ("http-category-address", bpo::value<std::vector<string>>(), 
             "The local IP and port to listen for incoming http category connections."
             "  Syntax: category,address\n"
             "    Where the address can be <hostname>:port, <ipaddress>:port or unix socket path;\n"
             "    in addition, unix socket path must starts with '/', './' or '../'. When relative path\n"
             "    is used, it is relative to the data path.\n\n"
             "    Valid categories include chain_ro, chain_rw, db_size, net_ro, net_rw, producer_ro\n"
             "    producer_rw, snapshot, trace_api, prometheus and test_control.\n\n"
             "    A single `hostname:port` specification can be used by multiple categories\n" 
             "    However, two specifications having the same port with different hostname strings\n" 
             "    are always considered as configuration error regardless whether they can be resolved\n"
             "    into the same set of IP addresses.\n\n"
             "  Examples:\n"
             "    chain_ro,127.0.0.1:8080\n"
             "    chain_ro,127.0.0.1:8081\n"
             "    chain_rw,localhost:8081 # ERROR!, same port with different addresses\n"
             "    chain_rw,[::1]:8082\n"
             "    net_ro, localhost:8083\n"
             "    net_rw, server.domain.net:8084\n"
             "    producer_ro,/tmp/absolute_unix_path.sock\n"
             "    producer_rw,./relative_unix_path.sock\n"
             "    trace_api,:8086 # listen on all network interfaces\n\n"
             "  Notice that the behavor for `[::1]` is platform dependent. For system with IPv4 mapped IPv6 networking\n"
             "  is enabled, using `[::1]` will listen on both IPv4 and IPv6; other systems like FreeBSD, it will only\n"
             "  listen on IPv6. On the other hand, the specfications without hostnames like `:8086` will always listen on\n"
             "  both IPv4 and IPv6 on all platforms.");
      }

      cfg.add_options()
            ("access-control-allow-origin", bpo::value<string>()->notifier([this](const string& v) {
                my->plugin_state->access_control_allow_origin = v;
                fc_ilog( logger(), "configured http with Access-Control-Allow-Origin: ${o}",
                         ("o", my->plugin_state->access_control_allow_origin) );
             }),
             "Specify the Access-Control-Allow-Origin to be returned on each request")

            ("access-control-allow-headers", bpo::value<string>()->notifier([this](const string& v) {
                my->plugin_state->access_control_allow_headers = v;
                fc_ilog( logger(), "configured http with Access-Control-Allow-Headers : ${o}",
                         ("o", my->plugin_state->access_control_allow_headers) );
             }),
             "Specify the Access-Control-Allow-Headers to be returned on each request")

            ("access-control-max-age", bpo::value<string>()->notifier([this](const string& v) {
                my->plugin_state->access_control_max_age = v;
                fc_ilog( logger(), "configured http with Access-Control-Max-Age : ${o}",
                         ("o", my->plugin_state->access_control_max_age) );
             }),
             "Specify the Access-Control-Max-Age to be returned on each request.")

            ("access-control-allow-credentials",
             bpo::bool_switch()->notifier([this](bool v) {
                my->plugin_state->access_control_allow_credentials = v;
                if( v ) fc_ilog( logger(), "configured http with Access-Control-Allow-Credentials: true" );
             })->default_value(false),
             "Specify if Access-Control-Allow-Credentials: true should be returned on each request.")
            ("max-body-size", bpo::value<uint32_t>()->default_value(my->plugin_state->max_body_size),
             "The maximum body size in bytes allowed for incoming RPC requests")
            ("http-max-bytes-in-flight-mb", bpo::value<int64_t>()->default_value(500),
             "Maximum size in megabytes http_plugin should use for processing http requests. -1 for unlimited. 429 error response when exceeded." )
            ("http-max-in-flight-requests", bpo::value<int32_t>()->default_value(-1),
             "Maximum number of requests http_plugin should use for processing http requests. 429 error response when exceeded." )
            ("http-max-response-time-ms", bpo::value<int64_t>()->default_value(30),
             "Maximum time for processing a request, -1 for unlimited")
            ("verbose-http-errors", bpo::bool_switch()->default_value(false),
             "Append the error log to HTTP responses")
            ("http-validate-host", boost::program_options::value<bool>()->default_value(true),
             "If set to false, then any incoming \"Host\" header is considered valid")
            ("http-alias", bpo::value<std::vector<string>>()->composing(),
             "Additionally acceptable values for the \"Host\" header of incoming HTTP requests, can be specified multiple times.  Includes http/s_server_address by default.")
            ("http-threads", bpo::value<uint16_t>()->default_value( my->plugin_state->thread_pool_size ),
             "Number of worker threads in http thread pool")
            ("http-keep-alive", bpo::value<bool>()->default_value(true),
             "If set to false, do not keep HTTP connections alive, even if client requests.")
            ;
   }

   void http_plugin::plugin_initialize(const variables_map& options) {
      try {
         handle_sighup(); // setup logging
         my->plugin_state->max_body_size = options.at( "max-body-size" ).as<uint32_t>();
         verbose_http_errors = options.at( "verbose-http-errors" ).as<bool>();

         my->plugin_state->thread_pool_size = options.at( "http-threads" ).as<uint16_t>();
         EOS_ASSERT( my->plugin_state->thread_pool_size > 0, chain::plugin_config_exception,
                     "http-threads ${num} must be greater than 0", ("num", my->plugin_state->thread_pool_size));

         auto max_bytes_mb = options.at( "http-max-bytes-in-flight-mb" ).as<int64_t>();
         EOS_ASSERT( (max_bytes_mb >= -1 && max_bytes_mb < std::numeric_limits<int64_t>::max() / (1024 * 1024)), chain::plugin_config_exception,
                     "http-max-bytes-in-flight-mb (${max_bytes_mb}) must be equal to or greater than -1 and less than ${max}", ("max_bytes_mb", max_bytes_mb) ("max", std::numeric_limits<int64_t>::max() / (1024 * 1024)) );
         if ( max_bytes_mb == -1 ) {
            my->plugin_state->max_bytes_in_flight = std::numeric_limits<size_t>::max();
         } else {
            my->plugin_state->max_bytes_in_flight = max_bytes_mb * 1024 * 1024;
         }
         my->plugin_state->max_requests_in_flight = options.at( "http-max-in-flight-requests" ).as<int32_t>();
         int64_t max_reponse_time_ms = options.at("http-max-response-time-ms").as<int64_t>();
         EOS_ASSERT( max_reponse_time_ms == -1 || max_reponse_time_ms >= 0, chain::plugin_config_exception,
                     "http-max-response-time-ms must be -1, or non-negative: ${m}", ("m", max_reponse_time_ms) );
         // set to one year for -1, unlimited, since this is added to fc::time_point::now() for a deadline
         my->plugin_state->max_response_time = max_reponse_time_ms == -1 ?
               fc::days(365) : fc::microseconds( max_reponse_time_ms * 1000 );

         my->plugin_state->validate_host = options.at("http-validate-host").as<bool>();
         if( options.count( "http-alias" )) {
            const auto& aliases = options["http-alias"].as<vector<string>>();
            for (const auto& alias : aliases ) {
               auto [host, port] = split_host_port(alias);
               my->plugin_state->valid_hosts.insert(host);
            }
         }

         my->plugin_state->valid_hosts.insert("localhost");

         my->plugin_state->keep_alive = options.at("http-keep-alive").as<bool>();

         if (options.count("http-server-address"))
            my->http_server_address = options.at("http-server-address").as<string>();

         if (options.count("unix-socket-path") && !options.at("unix-socket-path").as<string>().empty()) {
            my->unix_sock_path = options.at("unix-socket-path").as<string>();
            if (my->unix_sock_path.size() && my->unix_sock_path[0] != '/')
               my->unix_sock_path = "./" + my->unix_sock_path;
         }

         if (options.count("http-category-address") != 0) {
            EOS_ASSERT(my->http_server_address == "http-category-address" && options.count("unix-socket-path") == 0,
                chain::plugin_config_exception,
                "when http-category-address is specified, http-server-address must be set as "
                "`http-category-address` and `unix-socket-path` must be left unspecified");

            std::map<std::string, std::string> hostnames;
            auto addresses = options["http-category-address"].as<vector<string>>();
            for (const auto& spec : addresses) {
               auto comma_pos = spec.find(',');
               EOS_ASSERT(comma_pos > 0 && comma_pos != std::string_view::npos, chain::plugin_config_exception,
               "http-category-address '${spec}' does not contain a required comma to separate the category and address", ("spec", spec));
               auto category_name = spec.substr(0, comma_pos);
               auto category = to_category(category_name);
               auto address = spec.substr(comma_pos+1);
               EOS_ASSERT(category != api_category::unknown, chain::plugin_config_exception, 
                  "invalid category name `${name}` for http_category_address", ("name", std::string(category_name)));
               auto [host, port] = split_host_port(address);
               if (port.size()) {
                  auto [itr, inserted] = hostnames.try_emplace(port, host);
                  EOS_ASSERT(inserted || host == itr->second, chain::plugin_config_exception, "unable to listen to port ${port} for both ${host} and ${prev}",
                  ("port", std::string(port))("host", std::string(host))("prev", std::string(itr->second)));
               }
               my->categories_by_address[address].insert(category);
            }
         }

         my->plugin_state->server_header = current_http_plugin_defaults.server_header;


         //watch out for the returns above when adding new code here
      } FC_LOG_AND_RETHROW()
   }

   void http_plugin::plugin_startup() {
      app().executor().post(appbase::priority::high, [this] ()
      {
         try {
            my->plugin_state->thread_pool.start( my->plugin_state->thread_pool_size, [](const fc::exception& e) {
               fc_elog( logger(), "Exception in http thread pool, exiting: ${e}", ("e", e.to_detail_string()) );
               app().quit();
            } );

            if (my->http_server_address != "http-category-address") {
               if (my->http_server_address.size()) {
                  my->create_beast_server(my->http_server_address, api_category_set::all());
               }

               if (my->unix_sock_path.size()) {
                  my->create_beast_server(my->unix_sock_path, api_category_set::all());
               }
            } else {
               for (const auto& [address, categories]: my->categories_by_address) {
                  my->create_beast_server(address, categories);
               }
            }

            if (plugin_promise) {
               plugin_promise->set_value(this);
            }

         } catch (...) {
            fc_elog(logger(), "http_plugin startup fails, shutting down");
            app().quit();
         }
      });
   }

   void http_plugin::handle_sighup() {
      fc::logger::update( logger().get_name(), logger() );
   }

   void http_plugin::plugin_shutdown() {
      my->plugin_state->thread_pool.stop();

      // release http_plugin_impl_ptr shared_ptrs captured in url handlers
      my->plugin_state->url_handlers.clear();

      fc_ilog( logger(), "exit shutdown");
   }

   void http_plugin::add_handler(api_entry&& entry, appbase::exec_queue q, int priority, http_content_type content_type) {
      std::string path = entry.path;
      fc_ilog( logger(), "add api url: ${c}", ("c", path) );
      auto p = my->plugin_state->url_handlers.emplace(entry.path, my->make_app_thread_url_handler(std::move(entry), q, priority, my, content_type));
      EOS_ASSERT( p.second, chain::plugin_config_exception, "http url ${u} is not unique", ("u", path) );
   }

   void http_plugin::add_async_handler(api_entry&& entry, http_content_type content_type) {
      std::string path = entry.path;
      fc_ilog( logger(), "add api url: ${c}", ("c", path) );
      auto p = my->plugin_state->url_handlers.emplace(entry.path, my->make_http_thread_url_handler(std::move(entry), content_type));
      EOS_ASSERT( p.second, chain::plugin_config_exception, "http url ${u} is not unique", ("u", path) );
   }

   void http_plugin::post_http_thread_pool(std::function<void()> f) {
      if( f )
         boost::asio::post( my->plugin_state->thread_pool.get_executor(), f );
   }

   void http_plugin::handle_exception( const char* api_name, const char* call_name, const string& body, const url_response_callback& cb) {
      try {
         try {
            throw;
         } catch (chain::unknown_block_exception& e) {
            error_results results{400, "Unknown Block", error_results::error_info(e, verbose_http_errors)};
            cb( 400, fc::time_point::maximum(), fc::variant( results ));
            fc_dlog( logger(), "Unknown block while processing ${api}.${call}: ${e}",
                     ("api", api_name)("call", call_name)("e", e.to_detail_string()) );
         } catch (chain::invalid_http_request& e) {
            error_results results{400, "Invalid Request", error_results::error_info(e, verbose_http_errors)};
            cb( 400, fc::time_point::maximum(), fc::variant( results ));
            fc_dlog( logger(), "Invalid http request while processing ${api}.${call}: ${e}",
                     ("api", api_name)("call", call_name)("e", e.to_detail_string()) );
         } catch (chain::account_query_exception& e) {
            error_results results{400, "Account lookup", error_results::error_info(e, verbose_http_errors)};
            cb( 400, fc::time_point::maximum(), fc::variant( results ));
            fc_dlog( logger(), "Account query exception while processing ${api}.${call}: ${e}",
                     ("api", api_name)("call", call_name)("e", e.to_detail_string()) );
         } catch (chain::unsatisfied_authorization& e) {
            error_results results{401, "UnAuthorized", error_results::error_info(e, verbose_http_errors)};
            cb( 401, fc::time_point::maximum(), fc::variant( results ));
            fc_dlog( logger(), "Auth error while processing ${api}.${call}: ${e}",
                     ("api", api_name)("call", call_name)("e", e.to_detail_string()) );
         } catch (chain::tx_duplicate& e) {
            error_results results{409, "Conflict", error_results::error_info(e, verbose_http_errors)};
            cb( 409, fc::time_point::maximum(), fc::variant( results ));
            fc_dlog( logger(), "Duplicate trx while processing ${api}.${call}: ${e}",
                     ("api", api_name)("call", call_name)("e", e.to_detail_string()) );
         } catch (fc::eof_exception& e) {
            error_results results{422, "Unprocessable Entity", error_results::error_info(e, verbose_http_errors)};
            cb( 422, fc::time_point::maximum(), fc::variant( results ));
            fc_elog( logger(), "Unable to parse arguments to ${api}.${call}", ("api", api_name)( "call", call_name ) );
            fc_dlog( logger(), "Bad arguments: ${args}", ("args", body) );
         } catch (fc::exception& e) {
            error_results results{500, "Internal Service Error", error_results::error_info(e, verbose_http_errors)};
            cb( 500, fc::time_point::maximum(), fc::variant( results ));
            fc_dlog( logger(), "Exception while processing ${api}.${call}: ${e}",
                     ("api", api_name)( "call", call_name )("e", e.to_detail_string()) );
         } catch (std::exception& e) {
            error_results results{500, "Internal Service Error", error_results::error_info(fc::exception( FC_LOG_MESSAGE( error, e.what())), verbose_http_errors)};
            cb( 500, fc::time_point::maximum(), fc::variant( results ));
            fc_dlog( logger(), "STD Exception encountered while processing ${api}.${call}: ${e}",
                     ("api", api_name)("call", call_name)("e", e.what()) );
         } catch (...) {
            error_results results{500, "Internal Service Error",
               error_results::error_info(fc::exception( FC_LOG_MESSAGE( error, "Unknown Exception" )), verbose_http_errors)};
            cb( 500, fc::time_point::maximum(), fc::variant( results ));
            fc_elog( logger(), "Unknown Exception encountered while processing ${api}.${call}",
                     ("api", api_name)( "call", call_name ) );
         }
      } catch (...) {
         std::cerr << "Exception attempting to handle exception for " << api_name << "." << call_name << std::endl;
      }
   }

   bool http_plugin::is_on_loopback(api_category category) const {
      // return (!my->listen_endpoint || my->listen_endpoint->address().is_loopback());
      return false;
   }

   bool http_plugin::verbose_errors() {
      return verbose_http_errors;
   }

   fc::microseconds http_plugin::get_max_response_time()const {
      return my->plugin_state->max_response_time;
   }

   size_t http_plugin::get_max_body_size()const {
      return my->plugin_state->max_body_size;
   }

   void  http_plugin::register_update_metrics(std::function<void(metrics)>&& fun) {
      my->plugin_state->update_metrics = std::move(fun);
   }

   void http_plugin::set_plugin_promise(std::promise<http_plugin*>* promise) {
      plugin_promise = promise;
   }

}

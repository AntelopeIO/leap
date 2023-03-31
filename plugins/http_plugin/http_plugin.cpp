#include <eosio/http_plugin/http_plugin.hpp>
#include <eosio/http_plugin/common.hpp>
#include <eosio/http_plugin/beast_http_listener.hpp>
#include <eosio/chain/exceptions.hpp>

#include <fc/log/logger_config.hpp>
#include <fc/reflect/variant.hpp>

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

   void http_plugin::set_defaults(const http_plugin_defaults& config) {
      current_http_plugin_defaults = config;
   }

   using http_plugin_impl_ptr = std::shared_ptr<class http_plugin_impl>;

   class http_plugin_impl : public std::enable_shared_from_this<http_plugin_impl> {
      public:
         http_plugin_impl() = default;

         http_plugin_impl(const http_plugin_impl&) = delete;
         http_plugin_impl(http_plugin_impl&&) = delete;

         http_plugin_impl& operator=(const http_plugin_impl&) = delete;
         http_plugin_impl& operator=(http_plugin_impl&&) = delete;
         
         std::optional<tcp::endpoint>  listen_endpoint;
         
         std::optional<asio::local::stream_protocol::endpoint> unix_endpoint;

         shared_ptr<beast_http_listener<plain_session, tcp, tcp_socket_t > >  beast_server;
         shared_ptr<beast_http_listener<unix_socket_session, stream_protocol, stream_protocol::socket > > beast_unix_server;

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
         static detail::internal_url_handler make_app_thread_url_handler(const string& url, appbase::exec_queue to_queue, int priority, url_handler next, http_plugin_impl_ptr my, http_content_type content_type ) {
            detail::internal_url_handler handler{url};
            handler.content_type = content_type;
            auto next_ptr = std::make_shared<url_handler>(std::move(next));
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
         static detail::internal_url_handler make_http_thread_url_handler(const string& url, url_handler next, http_content_type content_type) {
            detail::internal_url_handler handler{url};
            handler.content_type = content_type;
            handler.fn = [next=std::move(next)]( const detail::abstract_conn_ptr& conn, string&& r, string&& b, url_response_callback&& then ) mutable {
               try {
                  next(std::move(r), std::move(b), std::move(then));
               } catch( ... ) {
                  conn->handle_exception();
               }
             };
            return handler;
         }

         void add_aliases_for_endpoint( const tcp::endpoint& ep, const string& host, const string& port ) {
            auto resolved_port_str = std::to_string(ep.port());
            plugin_state->valid_hosts.emplace(host + ":" + port);
            plugin_state->valid_hosts.emplace(host + ":" + resolved_port_str);
         }

         void create_beast_server(bool isUnix) {
            if(isUnix) {
               beast_unix_server = std::make_shared<beast_http_listener<unix_socket_session, stream_protocol, stream_protocol::socket> >(plugin_state);
               fc_ilog( logger(), "created beast UNIX socket listener");
            }
            else {
               beast_server = std::make_shared<beast_http_listener<plain_session, tcp, tcp_socket_t> >(plugin_state);
               fc_ilog( logger(), "created beast HTTP listener");
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
             "Additionaly acceptable values for the \"Host\" header of incoming HTTP requests, can be specified multiple times.  Includes http/s_server_address by default.")
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
            my->plugin_state->valid_hosts.insert(aliases.begin(), aliases.end());
         }

         my->plugin_state->keep_alive = options.at("http-keep-alive").as<bool>();

         tcp::resolver resolver( app().get_io_service());
         if( options.count( "http-server-address" ) && options.at( "http-server-address" ).as<string>().length()) {
            string lipstr = options.at( "http-server-address" ).as<string>();
            string host = lipstr.substr( 0, lipstr.find( ':' ));
            string port = lipstr.substr( host.size() + 1, lipstr.size());
            try {
               my->listen_endpoint = *resolver.resolve( tcp::v4(), host, port );
               fc_ilog(logger(),  "configured http to listen on ${h}:${p}", ("h", host)( "p", port ));
            } catch ( const boost::system::system_error& ec ) {
               fc_elog(logger(),  "failed to configure http to listen on ${h}:${p} (${m})",
                     ("h", host)( "p", port )( "m", ec.what()));
            }

            // add in resolved hosts and ports as well
            if (my->listen_endpoint) {
               my->add_aliases_for_endpoint(*my->listen_endpoint, host, port);
            }
         }

         if( options.count( "unix-socket-path" ) && !options.at( "unix-socket-path" ).as<string>().empty()) {
            boost::filesystem::path sock_path = options.at("unix-socket-path").as<string>();
            if (sock_path.is_relative())
               sock_path = app().data_dir() / sock_path;
            my->unix_endpoint = asio::local::stream_protocol::endpoint(sock_path.string());
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

            if(my->listen_endpoint) {
               try {
                  my->create_beast_server(false);

                  fc_ilog( logger(), "start listening for http requests (boost::beast)" );

                  my->beast_server->listen(*my->listen_endpoint);
                  my->beast_server->start_accept();
               } catch ( const fc::exception& e ){
                  fc_elog( logger(), "http service failed to start: ${e}", ("e", e.to_detail_string()) );
                  throw;
               } catch ( const std::exception& e ){
                  fc_elog( logger(), "http service failed to start: ${e}", ("e", e.what()) );
                  throw;
               } catch (...) {
                  fc_elog( logger(), "error thrown from http io service" );
                  throw;
               }
            }

            if(my->unix_endpoint) {
               try {
                  my->create_beast_server(true);
                  
                  my->beast_unix_server->listen(*my->unix_endpoint);
                  my->beast_unix_server->start_accept();
               } catch ( const fc::exception& e ){
                  fc_elog( logger(), "unix socket service (${path}) failed to start: ${e}", ("e", e.to_detail_string())("path",my->unix_endpoint->path()) );
                  throw;
               } catch ( const std::exception& e ){
                  fc_elog( logger(), "unix socket service (${path}) failed to start: ${e}", ("e", e.what())("path",my->unix_endpoint->path()) );
                  throw;
               } catch (...) {
                  fc_elog( logger(), "error thrown from unix socket (${path}) io service", ("path",my->unix_endpoint->path()) );
                  throw;
               }
            }

            add_api({{
               std::string("/v1/node/get_supported_apis"),
               [&](string&&, string&& body, url_response_callback&& cb) {
                  try {
                     auto result = (*this).get_supported_apis();
                     cb(200, fc::time_point::maximum(), fc::variant(result));
                  } catch (...) {
                     handle_exception("node", "get_supported_apis", body.empty() ? "{}" : body, cb);
                  }
               }
            }}, appbase::exec_queue::read_only);
            
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
      if(my->beast_server)
         my->beast_server->stop_listening();
      if(my->beast_unix_server)
         my->beast_unix_server->stop_listening();

      my->plugin_state->thread_pool.stop();

      my->beast_server.reset();
      my->beast_unix_server.reset();

      // release http_plugin_impl_ptr shared_ptrs captured in url handlers
      my->plugin_state->url_handlers.clear();

      fc_ilog( logger(), "exit shutdown");
   }

   void http_plugin::add_handler(const string& url, const url_handler& handler, appbase::exec_queue q, int priority, http_content_type content_type) {
      fc_ilog( logger(), "add api url: ${c}", ("c", url) );
      auto p = my->plugin_state->url_handlers.emplace(url, my->make_app_thread_url_handler(url, q, priority, handler, my, content_type));
      EOS_ASSERT( p.second, chain::plugin_config_exception, "http url ${u} is not unique", ("u", url) );
   }

   void http_plugin::add_async_handler(const string& url, const url_handler& handler, http_content_type content_type) {
      fc_ilog( logger(), "add api url: ${c}", ("c", url) );
      auto p = my->plugin_state->url_handlers.emplace(url, my->make_http_thread_url_handler(url, handler, content_type));
      EOS_ASSERT( p.second, chain::plugin_config_exception, "http url ${u} is not unique", ("u", url) );
   }

   void http_plugin::post_http_thread_pool(std::function<void()> f) {
      if( f )
         boost::asio::post( my->plugin_state->thread_pool.get_executor(), f );
   }

   void http_plugin::handle_exception( const char *api_name, const char *call_name, const string& body, const url_response_callback& cb) {
      try {
         try {
            throw;
         } catch (chain::unknown_block_exception& e) {
            error_results results{400, "Unknown Block", error_results::error_info(e, verbose_http_errors)};
            cb( 400, fc::time_point::maximum(), fc::variant( results ));
         } catch (chain::invalid_http_request& e) {
            error_results results{400, "Invalid Request", error_results::error_info(e, verbose_http_errors)};
            cb( 400, fc::time_point::maximum(), fc::variant( results ));
         } catch (chain::unsatisfied_authorization& e) {
            error_results results{401, "UnAuthorized", error_results::error_info(e, verbose_http_errors)};
            cb( 401, fc::time_point::maximum(), fc::variant( results ));
         } catch (chain::tx_duplicate& e) {
            error_results results{409, "Conflict", error_results::error_info(e, verbose_http_errors)};
            cb( 409, fc::time_point::maximum(), fc::variant( results ));
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

   bool http_plugin::is_on_loopback() const {
      return (!my->listen_endpoint || my->listen_endpoint->address().is_loopback());
   }

   bool http_plugin::is_secure() const {
      return (!my->listen_endpoint || my->listen_endpoint->address().is_loopback());
   }

   bool http_plugin::verbose_errors() {
      return verbose_http_errors;
   }

   http_plugin::get_supported_apis_result http_plugin::get_supported_apis()const {
      get_supported_apis_result result;

      for (const auto& handler : my->plugin_state->url_handlers) {
         if (handler.first != "/v1/node/get_supported_apis")
            result.apis.emplace_back(handler.first);
      }

      return result;
   }

   void http_plugin::register_metrics_listener(chain::plugin_interface::metrics_listener listener) {
      my->plugin_state->metrics.register_listener(std::move(listener));
   }

   fc::microseconds http_plugin::get_max_response_time()const {
      return my->plugin_state->max_response_time;
   }
   
   size_t http_plugin::get_max_body_size()const {
      return my->plugin_state->max_body_size;
   }
   
}

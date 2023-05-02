#include <eosio/prometheus_plugin/prometheus_plugin.hpp>
#include <eosio/prometheus_plugin/simple_rest_server.hpp>

#include <eosio/chain/plugin_interface.hpp>
#include <eosio/chain/thread_utils.hpp>
#include <eosio/http_plugin/macros.hpp>
#include <eosio/net_plugin/net_plugin.hpp>
#include <eosio/http_plugin/http_plugin.hpp>
#include <eosio/producer_plugin/producer_plugin.hpp>

#include <fc/log/logger.hpp>

#include "metrics.hpp"

namespace eosio { 

   static const char* prometheus_api_name = "/v1/prometheus/metrics";
   using namespace prometheus;
   using namespace chain::plugin_interface;

   static auto _prometheus_plugin = application::register_plugin<prometheus_plugin>();

   namespace http = boost::beast::http;
   struct prometheus_plugin_impl : rest::simple_server<prometheus_plugin_impl> {

      std::string server_header() const {
         return http_plugin::get_server_header();
      }

      void log_error(char const* what, const std::string& message) {
         elog("${what}: ${message}", ("what", what)("message", message));
      }

      bool allow_method(http::verb method) const {
         return method == http::verb::get;
      }

      std::optional<http::response<http::string_body>>
       on_request(http::request<http::string_body>&& req) {
         if(req.target() != prometheus_api_name)
            return {};
         http::response<http::string_body> res{ http::status::ok, req.version() };
         // Respond to GET request
         res.set(http::field::server, server_header());
         res.set(http::field::content_type, "text/plain");
         res.keep_alive(req.keep_alive());
         res.body() = _catalog.report();
         res.prepare_payload();
         return res;
      }

      eosio::chain::named_thread_pool<struct prom> _prometheus_thread_pool;
      boost::asio::io_context::strand _prometheus_strand;
      metrics::catalog_type           _catalog;

      boost::asio::ip::tcp::endpoint           _endpoint;

      prometheus_plugin_impl(): _prometheus_strand(_prometheus_thread_pool.get_executor()){ 
         _catalog.register_update_handlers(_prometheus_strand);
      }

      void start() {
         run(_prometheus_thread_pool.get_executor(), _endpoint);
         _prometheus_thread_pool.start(
               1, [](const fc::exception& e) { elog("Prometheus exception ${e}", ("e", e)); });
      }
   };

   prometheus_plugin::prometheus_plugin()
   : my(new prometheus_plugin_impl{}) {
   }

   prometheus_plugin::~prometheus_plugin() = default;

   void prometheus_plugin::set_program_options(options_description&, options_description& cfg) {
      cfg.add_options()
         ("prometheus-exporter-address", bpo::value<string>()->default_value("127.0.0.1:9101"),
            "The local IP and port to listen for incoming prometheus metrics http request.");
   }

   void prometheus_plugin::plugin_initialize(const variables_map& options) {

      string lipstr = options.at("prometheus-exporter-address").as<string>();
      EOS_ASSERT(lipstr.size() > 0, chain::plugin_config_exception, "prometheus-exporter-address must have a value");

      string host = lipstr.substr(0, lipstr.find(':'));
      string port = lipstr.substr(host.size() + 1, lipstr.size());

      boost::system::error_code ec;
      using tcp = boost::asio::ip::tcp;
      tcp::resolver resolver(app().get_io_service());

      my->_endpoint = *resolver.resolve(tcp::v4(), host, port, ec);
      if (!ec) {
         fc_ilog(logger(), "configured prometheus metrics exporter to listen on ${h}", ("h", lipstr));
      } else {
         fc_elog(logger(), "failed to configure prometheus metrics exporter to listen on ${h} (${m})",
                 ("h", lipstr)("m", ec.message()));
      }
   }

   void prometheus_plugin::plugin_startup() {
      my->start();
      ilog("Prometheus plugin started.");
   }

   void prometheus_plugin::plugin_shutdown() {
      my->_prometheus_thread_pool.stop();
      ilog("Prometheus plugin shutdown.");
   }
}
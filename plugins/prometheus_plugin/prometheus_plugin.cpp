#include <eosio/prometheus_plugin/prometheus_plugin.hpp>
#include <eosio/prometheus_plugin/simple_rest_server.hpp>

#include <eosio/chain/plugin_interface.hpp>
#include <eosio/chain/thread_utils.hpp>
#include <eosio/http_plugin/macros.hpp>
#include <eosio/net_plugin/net_plugin.hpp>
#include <eosio/producer_plugin/producer_plugin.hpp>

#include <fc/log/logger.hpp>

#include <prometheus/metric_family.h>
#include <prometheus/collectable.h>
#include <prometheus/counter.h>
#include <prometheus/summary.h>
#include <prometheus/text_serializer.h>
#include <prometheus/registry.h>

namespace eosio {
   static const char* prometheus_api_name = "/v1/prometheus/metrics";
   using namespace prometheus;
   using namespace chain::plugin_interface;

   static auto _prometheus_plugin = application::register_plugin<prometheus_plugin>();
   using metric_type=chain::plugin_interface::metric_type;

   struct prometheus_plugin_metrics : plugin_metrics {
      runtime_metric bytes_transferred{metric_type::counter, "exposer_transferred_bytes_total", "exposer_transferred_bytes_total"};
      runtime_metric num_scrapes{metric_type::counter, "exposer_scrapes_total", "exposer_scrapes_total"};

      std::vector<chain::plugin_interface::runtime_metric> metrics() final {
         return std::vector{
            bytes_transferred,
            num_scrapes
         };
      }
   };

   struct metrics_model {
      std::vector<std::shared_ptr<Collectable>> _collectables;
      std::shared_ptr<Registry> _registry;
      std::vector<std::reference_wrapper<Family<Gauge>>> _gauges;
      std::vector<std::reference_wrapper<Family<Counter>>> _counters;

      void add_gauge_metric(const runtime_metric& plugin_metric) {
         auto& gauge_family = BuildGauge()
               .Name(plugin_metric.family)
               .Help("")
               .Register(*_registry);
         auto& gauge = gauge_family.Add({});
         gauge.Set(plugin_metric.value);

         _gauges.push_back(gauge_family);

         tlog("Added gauge metric ${f}:${l}", ("f", plugin_metric.family) ("l", plugin_metric.label));
      }

      void add_counter_metric(const runtime_metric& plugin_metric) {
         auto& counter_family = BuildCounter()
               .Name(plugin_metric.family)
               .Help("")
               .Register(*_registry);
         auto& counter = counter_family.Add({});
         counter.Increment(plugin_metric.value);
         _counters.push_back(counter_family);

         tlog("Added counter metric ${f}:${l}", ("f", plugin_metric.family) ("l", plugin_metric.label));
      }

      void add_runtime_metric(const runtime_metric& plugin_metric) {
         switch(plugin_metric.type) {
            case metric_type::gauge:
               add_gauge_metric(plugin_metric);
               break;
            case metric_type::counter:
               add_counter_metric(plugin_metric);
               break;

            default:
               break;
         }
      }

      void add_runtime_metrics(const std::vector<runtime_metric>& metrics){
         for (auto const& m : metrics) {
            add_runtime_metric(m);
         }
      }

      metrics_model() {
         _registry = std::make_shared<Registry>();
         _collectables.push_back(_registry);
      }

      std::vector<MetricFamily> collect_metrics() {
         auto collected_metrics = std::vector<MetricFamily>{};

         for (auto&& collectable : _collectables) {

            auto&& metrics = collectable->Collect();
            collected_metrics.insert(collected_metrics.end(),
                                     std::make_move_iterator(metrics.begin()),
                                     std::make_move_iterator(metrics.end()));
         }

         return collected_metrics;
      }

      std::string serialize() {
         const prometheus::TextSerializer serializer;
         return serializer.Serialize(collect_metrics());
      }
   };

   namespace http = boost::beast::http;
   struct prometheus_plugin_impl : rest::simple_server<prometheus_plugin_impl> {

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
         res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
         res.set(http::field::content_type, "text/plain");
         res.keep_alive(req.keep_alive());
         res.body() = metrics();
         res.prepare_payload();
         return res;
      }

      eosio::chain::named_thread_pool<struct prom> _prometheus_thread_pool;
      boost::asio::io_context::strand _prometheus_strand;
      prometheus_plugin_metrics _metrics;

      map<std::string, vector<runtime_metric>> _plugin_metrics;
      boost::asio::ip::tcp::endpoint           _endpoint;

      prometheus_plugin_impl(): _prometheus_strand(_prometheus_thread_pool.get_executor()){ }

      void update_metrics(const std::string& plugin_name, vector<runtime_metric> metrics) {
         auto plugin_metrics = _plugin_metrics.find(plugin_name);
         if (plugin_metrics != _plugin_metrics.end()) {
            plugin_metrics->second = std::move(metrics);
         }
      }

      metrics_listener create_metrics_listener(std::string plugin_name) {
         return  [plugin_name{std::move(plugin_name)}, self=this] (vector<runtime_metric> metrics) {
            self->_prometheus_strand.post(
                  [self, plugin_name, metrics{std::move(metrics)}]() mutable {
                     self->update_metrics(plugin_name, std::move(metrics));
            });
         };
      }


      void initialize_metrics() {
         net_plugin* np = app().find_plugin<net_plugin>();
         if (nullptr != np) {
            _plugin_metrics.emplace(std::pair{"net", std::vector<runtime_metric>()});
            np->register_metrics_listener(create_metrics_listener("net"));
         } else {
            wlog("net_plugin not found -- metrics not added");
         }

         producer_plugin* pp = app().find_plugin<producer_plugin>();
         if (nullptr != pp) {
            _plugin_metrics.emplace(std::pair{"prod", std::vector<runtime_metric>()});
            pp->register_metrics_listener(create_metrics_listener("prod"));
         } else {
            wlog("producer_plugin not found -- metrics not added");
         }

         http_plugin* hp = app().find_plugin<http_plugin>();
         if (nullptr != pp) {
            _plugin_metrics.emplace(std::pair{"http", std::vector<runtime_metric>()});
            hp->register_metrics_listener(create_metrics_listener("http"));
         } else {
            wlog("http_plugin not found -- metrics not added");
         }
      }

      std::string metrics() {
         metrics_model mm;
         vector<runtime_metric> prometheus_metrics = _metrics.metrics();
         mm.add_runtime_metrics(prometheus_metrics);

         for (auto& pm : _plugin_metrics) {
            mm.add_runtime_metrics(pm.second);
         }

         std::string body = mm.serialize();

         _metrics.bytes_transferred.value += body.length();
         _metrics.num_scrapes.value++;

         return body;
      }

      void start() {
         run(_prometheus_thread_pool.get_executor(), _endpoint);
         _prometheus_thread_pool.start(
               1, [](const fc::exception& e) { elog("Prometheus excpetion ${e}", ("e", e)); });
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
      my->initialize_metrics();

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
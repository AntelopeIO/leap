#include <eosio/prometheus_plugin/prometheus_plugin.hpp>
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

         ilog("Added gauge metric ${f}:${l}", ("f", plugin_metric.family) ("l", plugin_metric.label));
      }

      void add_counter_metric(const runtime_metric& plugin_metric) {
         auto& counter_family = BuildCounter()
               .Name(plugin_metric.family)
               .Help("")
               .Register(*_registry);
         auto& counter = counter_family.Add({});
         counter.Increment(plugin_metric.value);
         _counters.push_back(counter_family);

         ilog("Added counter metric ${f}:${l}", ("f", plugin_metric.family) ("l", plugin_metric.label));
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

      void add_runtime_metrics(std::vector<runtime_metric>& metrics){
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

   struct prometheus_plugin_impl {
      eosio::chain::named_thread_pool<struct prom> _prometheus_thread_pool;
      boost::asio::io_context::strand _prometheus_strand;
      prometheus_plugin_metrics _metrics;

      map<std::string, vector<runtime_metric>> _plugin_metrics;

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
            dlog("net_plugin not found -- metrics not added");
         }

         producer_plugin* pp = app().find_plugin<producer_plugin>();
         if (nullptr != pp) {
            _plugin_metrics.emplace(std::pair{"prod", std::vector<runtime_metric>()});
            pp->register_metrics_listener(create_metrics_listener("prod"));
         } else {
            dlog("producer_plugin not found -- metrics not added");
         }

         http_plugin* hp = app().find_plugin<http_plugin>();
         if (nullptr != pp) {
            _plugin_metrics.emplace(std::pair{"http", std::vector<runtime_metric>()});
            hp->register_metrics_listener(create_metrics_listener("http"));
         } else {
            dlog("producer_plugin not found -- metrics not added");
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

      void metrics_async(chain::plugin_interface::next_function<std::string> results) {
         _prometheus_strand.post([self=this, results=std::move(results)]() {
            results(self->metrics());
         });
      }

   };

   using metrics_params = fc::variant_object;

   struct prometheus_api {
      prometheus_plugin_impl& _pp;
      fc::microseconds _max_response_time_us;

      fc::time_point start() const {
         return fc::time_point::now() + _max_response_time_us;
      }

      void metrics(const metrics_params& p, chain::plugin_interface::next_function<std::string> results) {
         _pp.metrics_async(std::move(results));
      }

      prometheus_api(prometheus_plugin_impl& plugin, const fc::microseconds& max_response_time)
      : _pp(plugin)
      , _max_response_time_us(max_response_time){}

   };

   prometheus_plugin::prometheus_plugin()
   : my(new prometheus_plugin_impl{}) {
   }

   prometheus_plugin::~prometheus_plugin() = default;

   void prometheus_plugin::set_program_options(options_description&, options_description& cfg) {
   }

   void prometheus_plugin::plugin_initialize(const variables_map& options) {
      my->initialize_metrics();

      auto& _http_plugin = app().get_plugin<http_plugin>();
      fc::microseconds max_response_time = _http_plugin.get_max_response_time();

      prometheus_api handle(*my, max_response_time);
      app().get_plugin<http_plugin>().add_async_api({
        CALL_ASYNC_WITH_400(prometheus, handle, eosio, metrics, std::string, 200, http_params_types::no_params)}, http_content_type::plaintext);
   }

   void prometheus_plugin::plugin_startup() {
      my->_prometheus_thread_pool.start(1, []( const fc::exception& e ) {
         elog("Prometheus excpetion ${e}:${l}", ("e", e));
      } );

      ilog("Prometheus plugin started.");
   }

   void prometheus_plugin::plugin_shutdown() {
      my->_prometheus_thread_pool.stop();
      ilog("Prometheus plugin shutdown.");
   }
}
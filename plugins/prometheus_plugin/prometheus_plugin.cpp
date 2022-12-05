#include <eosio/prometheus_plugin/prometheus_plugin.hpp>
#include <fc/log/logger.hpp>
#include <prometheus/metric_family.h>
#include <prometheus/collectable.h>
#include <prometheus/counter.h>
#include <prometheus/summary.h>
#include <prometheus/text_serializer.h>
#include <prometheus/registry.h>

#define CALL_WITH_400(api_name, api_handle, call_name, INVOKE, http_response_code) \
{std::string("/v1/" #api_name "/" #call_name), \
   [api_handle](string, string body, url_response_callback cb) mutable { \
          try { \
             body = parse_params<std::string, http_params_types::no_params>(body); \
             INVOKE \
             cb(http_response_code, fc::time_point::maximum(), fc::variant(result)); \
          } catch (...) { \
             http_plugin::handle_exception(#api_name, #call_name, body, cb); \
          } \
       }}

#define INVOKE_R_V(api_handle, call_name) \
     auto result = api_handle->call_name();

namespace eosio {
   using namespace prometheus;
   static appbase::abstract_plugin &_prometheus_plugin = app().register_plugin<prometheus_plugin>();

   struct prometheus_plugin_metrics {
      Family<Counter>& _bytes_transferred_family;
      Counter& _bytes_transferred;
      Family<Counter>& _num_scrapes_family;
      Counter& _num_scrapes;
      Family<Summary>& _request_processing_family;
      Summary& _request_processing;

      prometheus_plugin_metrics(Registry& registry) :
         _bytes_transferred_family(
         BuildCounter()
               .Name("exposer_transferred_bytes_total")
               .Help("Transferred bytes to metrics services")
               .Register(registry)),
         _bytes_transferred(_bytes_transferred_family.Add({})),
         _num_scrapes_family(BuildCounter()
            .Name("exposer_scrapes_total")
            .Help("Number of times metrics were scraped")
            .Register(registry)),
         _num_scrapes(_num_scrapes_family.Add({})),
         _request_processing_family(
         BuildSummary()
            .Name("request_processing_time")
            .Help("Processing time of serving scrape requests, in microseconds")
            .Register(registry)),
         _request_processing(_request_processing_family.Add(
            {}, Summary::Quantiles{{0.5, 0.05}, {0.9, 0.01}, {0.99, 0.001}})) {}
      };

      struct prometheus_plugin_impl {
         std::mutex _collectables_mutex;
         std::vector<std::shared_ptr<Collectable>> _collectables;
         const prometheus::TextSerializer _serializer;
         std::shared_ptr<Registry> _registry;


         std::vector<std::tuple<Family<Gauge>&, Gauge&, runtime_metric&>> _gauges;
         std::vector<std::tuple<Family<Counter>&, Counter&, runtime_metric&>> _counters;

         // metrics for prometheus_plugin itself
         std::unique_ptr<prometheus_plugin_metrics> _metrics;

         // hold onto other plugin metrics
         std::shared_ptr<net_plugin_metrics> net_plugin_metrics_ptr;
         std::shared_ptr<producer_plugin_metrics> producer_plugin_metrics_ptr;

         prometheus_plugin_impl() { }

         void add_gauge_metric(runtime_metric& plugin_metric) {
               auto &gauge_family = BuildGauge()
                     .Name(plugin_metric.family)
                     .Help("")
                     .Register(*_registry);
            auto &gauge = gauge_family.Add({});

            _gauges.push_back(
                  std::tuple<Family<Gauge> &, Gauge &, runtime_metric &>(gauge_family, gauge, plugin_metric));

            ilog("Added gauge metric ${f}:${l}", ("f", plugin_metric.family) ("l", plugin_metric.label));
         }

         void add_counter_metric(runtime_metric& plugin_metric) {
                  auto &counter_family = BuildCounter()
                        .Name(plugin_metric.family)
                        .Help("")
                        .Register(*_registry);
                  auto &counter = counter_family.Add({});
                  _counters.push_back(
                        std::tuple<Family<Counter> &, Counter &, runtime_metric &>(counter_family, counter, plugin_metric));

            ilog("Added counter metric ${f}:${l}", ("f", plugin_metric.family) ("l", plugin_metric.label));
         }

         void add_plugin_metric(runtime_metric& plugin_metric) {
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

         void add_plugin_metrics(std::shared_ptr<net_plugin_metrics> metrics) {
            add_plugin_metric(metrics->num_clients);
            add_plugin_metric(metrics->num_peers);
            add_plugin_metric(metrics->dropped_trxs);
         }

         void add_plugin_metrics(std::shared_ptr<producer_plugin_metrics> metrics) {
            add_plugin_metric(metrics->unapplied_transactions);
            add_plugin_metric(metrics->blacklisted_transactions);
            add_plugin_metric(metrics->blocks_produced);
            add_plugin_metric(metrics->trxs_produced);
            add_plugin_metric(metrics->last_irreversible);
            add_plugin_metric(metrics->block_num);
            add_plugin_metric(metrics->subjective_bill_account_size);
            add_plugin_metric(metrics->subjective_bill_block_size);
            add_plugin_metric(metrics->scheduled_trxs);
         }

         void update_plugin_metrics() {
            for (auto& rtm : _gauges) {
               auto new_val = static_cast<double>(std::get<2>(rtm).value);
               std::get<1>(rtm).Set(new_val);
            }

            for (auto& rtm : _counters) {
               auto new_val = static_cast<double>(std::get<2>(rtm).value);
               std::get<1>(rtm).Increment(new_val-std::get<1>(rtm).Value());
            }
         }

         void initialize_metrics() {
            _registry = std::make_shared<Registry>();
            _metrics = std::make_unique<prometheus_plugin_metrics>(*_registry);
            _collectables.push_back(_registry);

            // this is where we will set up all non-prometheus_plugin metrics

            net_plugin* np = app().find_plugin<net_plugin>();
            if (nullptr != np) {
               net_plugin_metrics_ptr = np->metrics();
               add_plugin_metrics(net_plugin_metrics_ptr);
            } else {
               dlog("net_plugin not found -- metrics not added");
            }

            producer_plugin* pp = app().find_plugin<producer_plugin>();
            if (nullptr != pp) {
               producer_plugin_metrics_ptr = pp->metrics();
               add_plugin_metrics(producer_plugin_metrics_ptr);
            } else {
               dlog("producer_plugin not found -- metrics not added");
            }
         }

         std::string scrape() {
            auto start_time_of_request = std::chrono::steady_clock::now();

            update_plugin_metrics();

            std::vector<prometheus::MetricFamily> metrics;

            {
               std::lock_guard<std::mutex> lock{_collectables_mutex};
               metrics = collect_metrics();
            }

            std::string body = _serializer.Serialize(metrics);

            auto stop_time_of_request = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
                  stop_time_of_request - start_time_of_request);
            _metrics->_request_processing.Observe(duration.count());

            _metrics->_bytes_transferred.Increment(body.length());
            _metrics->_num_scrapes.Increment();

            return body;
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

   };

   prometheus_plugin::prometheus_plugin() {
     my.reset(new prometheus_plugin_impl{});

      app().get_plugin<http_plugin>().add_async_api({
        CALL_WITH_400(prometheus, this, scrape,  INVOKE_R_V(this, scrape), 200), });
   }

   prometheus_plugin::~prometheus_plugin() {}

   std::string prometheus_plugin::scrape() {return my->scrape();}

   void prometheus_plugin::set_program_options(options_description&, options_description& cfg) {

   }

   void prometheus_plugin::plugin_initialize(const variables_map& options) {
      my->initialize_metrics();
   }

   void prometheus_plugin::plugin_startup() {
      ilog("Prometheus plugin started.");
   }

   void prometheus_plugin::plugin_shutdown() {
      ilog("Prometheus plugin shutdown.");
   }
}
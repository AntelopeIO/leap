#include <eosio/prometheus_plugin/prometheus_plugin.hpp>
#include <fc/log/logger.hpp>
#include <prometheus/metric_family.h>
#include <prometheus/collectable.h>
#include <prometheus/counter.h>
#include <prometheus/summary.h>
#include <prometheus/text_serializer.h>
#include <prometheus/registry.h>
#include <eosio/chain/thread_utils.hpp>

#define CALL_ASYNC_WITH_400(api_name, api_handle, api_namespace, call_name, call_result, http_response_code, params_type) \
{std::string("/v1/" #api_name "/" #call_name), \
   [api_handle](string, string body, url_response_callback cb) mutable { \
      auto deadline = api_handle.start(); \
      try { \
         auto params = parse_params<api_namespace::call_name ## _params, params_type>(body);\
         FC_CHECK_DEADLINE(deadline);\
         api_handle.call_name( std::move(params), \
            [cb, body](const std::variant<fc::exception_ptr, call_result>& result){\
               if (std::holds_alternative<fc::exception_ptr>(result)) {\
                  try {\
                     std::get<fc::exception_ptr>(result)->dynamic_rethrow_exception();\
                  } catch (...) {\
                     http_plugin::handle_exception(#api_name, #call_name, body, cb);\
                  }\
               } else {\
                  cb(http_response_code, fc::time_point::maximum(), std::visit(async_result_visitor(), result));\
               }\
            });\
      } catch (...) { \
         http_plugin::handle_exception(#api_name, #call_name, body, cb); \
      } \
   }\
}

#define CALL_WITH_400(api_name, api_handle, call_name, INVOKE, http_response_code) \
{std::string("/v1/" #api_name "/" #call_name), \
   [api_handle](string, string body, url_response_callback cb) mutable { \
          try { \
             body = parse_params<std::string, http_params_types::no_params>(body); \
             INVOKE \
             cb(http_response_code, fc::time_point::maximum(), fc::variant(std::move(result))); \
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

      struct async_result_visitor : public fc::visitor<fc::variant> {
         template<typename T>
         fc::variant operator()(const T& v) const {
            return fc::variant(v);
         }
      };


      struct prometheus_plugin_impl {
         std::vector<std::shared_ptr<Collectable>> _collectables;
         const prometheus::TextSerializer _serializer;
         std::shared_ptr<Registry> _registry;
         // boost::asio::io_context _context;
         eosio::chain::named_thread_pool _prometheus_thread_pool;
         boost::asio::io_context::strand _prometheus_strand;

         std::vector<std::tuple<Family<Gauge>&, Gauge&, const runtime_metric&>> _gauges;
         std::vector<std::tuple<Family<Counter>&, Counter&, const runtime_metric&>> _counters;

         // metrics for prometheus_plugin itself
         std::unique_ptr<prometheus_plugin_metrics> _metrics;

         // hold onto other plugin metrics
         prometheus_plugin_impl(boost::asio::io_context& ctx): _prometheus_thread_pool("prometheus", 1), _prometheus_strand(_prometheus_thread_pool.get_executor()){ }

         void add_gauge_metric(const runtime_metric& plugin_metric) {
            auto& gauge_family = BuildGauge()
                  .Name(plugin_metric.family)
                  .Help("")
                  .Register(*_registry);
            auto& gauge = gauge_family.Add({});

            _gauges.push_back(
                  std::tuple<Family<Gauge>&, Gauge&, const runtime_metric&>(gauge_family, gauge, plugin_metric));

            ilog("Added gauge metric ${f}:${l}", ("f", plugin_metric.family) ("l", plugin_metric.label));
         }

         void add_counter_metric(const runtime_metric& plugin_metric) {
                  auto &counter_family = BuildCounter()
                        .Name(plugin_metric.family)
                        .Help("")
                        .Register(*_registry);
                  auto &counter = counter_family.Add({});
                  _counters.push_back(
                        std::tuple<Family<Counter>&, Counter&, const runtime_metric&>(counter_family, counter, plugin_metric));

            ilog("Added counter metric ${f}:${l}", ("f", plugin_metric.family) ("l", plugin_metric.label));
         }

         void add_plugin_metric(const runtime_metric& plugin_metric) {
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

         void update_metrics(vector<runtime_metric> metrics) {

         }

         metrics_listener create_metrics_listener() {
            return  [self=this] (vector<runtime_metric> metrics) mutable {
               self->_prometheus_strand.post(
                     [self, metrics=std::move(metrics)]() {
                        self->update_metrics(metrics);
                     }
               );
            };
         }

         void initialize_metrics() {
            _registry = std::make_shared<Registry>();
            _metrics = std::make_unique<prometheus_plugin_metrics>(*_registry);
            _collectables.push_back(_registry);

            // this is where we will set up all non-prometheus_plugin metrics

            net_plugin* np = app().find_plugin<net_plugin>();
            if (nullptr != np) {
               np->register_metrics_listener(create_metrics_listener());
            } else {
               dlog("net_plugin not found -- metrics not added");
            }

            producer_plugin* pp = app().find_plugin<producer_plugin>();
            if (nullptr != pp) {
               pp->register_metrics_listener(create_metrics_listener());
            } else {
               dlog("producer_plugin not found -- metrics not added");
            }
         }

         std::string metrics() {
            auto start_time_of_request = std::chrono::steady_clock::now();

            update_plugin_metrics();

            std::vector<prometheus::MetricFamily> metrics;

            metrics = collect_metrics();

            std::string body = _serializer.Serialize(metrics);

            auto stop_time_of_request = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
                  stop_time_of_request - start_time_of_request);
            _metrics->_request_processing.Observe(duration.count());

            _metrics->_bytes_transferred.Increment(body.length());
            _metrics->_num_scrapes.Increment();

            return body;
         }

         void metrics_async(chain::plugin_interface::next_function<std::string> results) {
            _prometheus_strand.post([self=this, results]() {
               results(self->metrics());
            });
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

   using metrics_params = fc::variant_object;

   struct prometheus_api {
      fc::microseconds max_response_time_ms{30*100};
      prometheus_plugin_impl& _pp;

      fc::time_point start() const {
         return fc::time_point::now() + max_response_time_ms;
      }

      void metrics(const metrics_params& p, chain::plugin_interface::next_function<std::string> results) {
         _pp.metrics_async(results);
      }

      prometheus_api(prometheus_plugin_impl& plugin) : _pp(plugin){
      }

   };
   prometheus_plugin::prometheus_plugin() {
      prometheus_api handle(*this->my);
      app().get_plugin<http_plugin>().add_api({
        CALL_ASYNC_WITH_400(prometheus, handle, eosio, metrics, std::string, 200, http_params_types::no_params)});
   }

   prometheus_plugin::~prometheus_plugin() {}

   std::string prometheus_plugin::metrics() {return my->metrics();}

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
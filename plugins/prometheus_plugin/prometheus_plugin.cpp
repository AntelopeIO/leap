#include <eosio/prometheus_plugin/prometheus_plugin.hpp>
#include <fc/log/logger.hpp>
#include <prometheus/metric_family.h>
#include <prometheus/collectable.h>
#include <prometheus/counter.h>
#include <prometheus/summary.h>
#include <prometheus/text_serializer.h>
#include <prometheus/registry.h>

namespace prometheus {
   std::vector<MetricFamily> CollectMetrics(
         const std::vector<std::weak_ptr<prometheus::Collectable>>& collectables) {
      auto collected_metrics = std::vector<MetricFamily>{};

      for (auto&& wcollectable : collectables) {
         auto collectable = wcollectable.lock();
         if (!collectable) {
            continue;
         }

         auto&& metrics = collectable->Collect();
         collected_metrics.insert(collected_metrics.end(),
                                  std::make_move_iterator(metrics.begin()),
                                  std::make_move_iterator(metrics.end()));
      }

      return collected_metrics;
   }

   class MetricsHandler {
   public:
      std::mutex collectables_mutex_;
      std::vector<std::weak_ptr<Collectable>> collectables_;
      Family<Counter>& bytes_transferred_family_;
      Counter& bytes_transferred_;
      Family<Counter>& num_scrapes_family_;
      Counter& num_scrapes_;
      Family<Summary>& request_latencies_family_;
      Summary& request_latencies_;

      MetricsHandler(Registry& registry)
         : bytes_transferred_family_(
         BuildCounter()
               .Name("exposer_transferred_bytes_total")
               .Help("Transferred bytes to metrics services")
               .Register(registry)),
           bytes_transferred_(bytes_transferred_family_.Add({})),
           num_scrapes_family_(BuildCounter()
                                     .Name("exposer_scrapes_total")
                                     .Help("Number of times metrics were scraped")
                                     .Register(registry)),
           num_scrapes_(num_scrapes_family_.Add({})),
           request_latencies_family_(
                 BuildSummary()
                       .Name("exposer_request_latencies")
                       .Help("Latencies of serving scrape requests, in microseconds")
                       .Register(registry)),
           request_latencies_(request_latencies_family_.Add(
                 {}, Summary::Quantiles{{0.5, 0.05}, {0.9, 0.01}, {0.99, 0.001}})) {}

      void cleanup_stale_pointers(
            std::vector<std::weak_ptr<Collectable>>& collectables) {
         collectables.erase(
               std::remove_if(std::begin(collectables), std::end(collectables),
                              [](const std::weak_ptr<Collectable> &candidate) {
                                 return candidate.expired();
                              }),
               std::end(collectables));
      }

      void register_collectable(
            const std::weak_ptr<Collectable>& collectable) {
         std::lock_guard<std::mutex> lock{collectables_mutex_};
         cleanup_stale_pointers(collectables_);
         collectables_.push_back(collectable);
      }

      void remove_collectable(
            const std::weak_ptr<Collectable>& collectable) {
         std::lock_guard<std::mutex> lock{collectables_mutex_};

         auto locked = collectable.lock();
         auto same_pointer = [&locked](const std::weak_ptr<Collectable>& candidate) {
            return locked == candidate.lock();
         };

         collectables_.erase(std::remove_if(std::begin(collectables_),
                                            std::end(collectables_), same_pointer),
                             std::end(collectables_));
      }

      std::string scrape() {
         auto start_time_of_request = std::chrono::steady_clock::now();

         std::vector<MetricFamily> metrics;

         {
            std::lock_guard<std::mutex> lock{collectables_mutex_};
            metrics = CollectMetrics(collectables_);
         }

         const TextSerializer serializer;

         std::string body = serializer.Serialize(metrics);

         auto stop_time_of_request = std::chrono::steady_clock::now();
         auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
               stop_time_of_request - start_time_of_request);
         request_latencies_.Observe(duration.count());

         bytes_transferred_.Increment(body.length());
         num_scrapes_.Increment();
         return body;
      }
   };
}

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

   static appbase::abstract_plugin &_prometheus_plugin = app().register_plugin<prometheus_plugin>();

   struct prometheus_plugin_impl {
      std::shared_ptr<prometheus::Registry> _prometheus_plugin_registry;
      std::unique_ptr<prometheus::MetricsHandler> _metrics_handler;

      prometheus_plugin_impl() :
         _prometheus_plugin_registry(std::make_shared<prometheus::Registry>()),
         _metrics_handler(std::make_unique<prometheus::MetricsHandler>(*_prometheus_plugin_registry)) {
            register_collectable(_prometheus_plugin_registry);
      }

      void register_collectable(
            const std::weak_ptr<prometheus::Collectable>& collectable) {
         _metrics_handler->register_collectable(collectable);
      }

      std::string scrape() {return _metrics_handler->scrape();}
   };

   prometheus_plugin::prometheus_plugin() {
     my.reset(new prometheus_plugin_impl{});

      app().get_plugin<http_plugin>().add_api({
        CALL_WITH_400(prometheus, this, scrape,  INVOKE_R_V(this, scrape), 200), });
   }

   prometheus_plugin::~prometheus_plugin() {}

   std::string prometheus_plugin::scrape() {return my->scrape();}

   void prometheus_plugin::set_program_options(options_description&, options_description& cfg) {

   }

   void prometheus_plugin::plugin_initialize(const variables_map& options) {

   }

   void prometheus_plugin::plugin_startup() {
      ilog("Prometheus plugin started.");
   }

   void prometheus_plugin::plugin_shutdown() {
      ilog("Prometheus plugin shutdown.");
   }
}
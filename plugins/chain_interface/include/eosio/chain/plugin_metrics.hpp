#pragma once

#include <fc/time.hpp>

#include <functional>
#include <string>
#include <vector>

namespace eosio::chain::plugin_interface {

   //
   // prometheus metrics
   //

   enum class metric_type {
      gauge = 1,
      counter = 2
   };

   struct runtime_metric {
      metric_type type = metric_type::gauge;
      std::string family;
      std::string label;
      int64_t value = 0;
   };

   using metrics_listener = std::function<void(std::vector<runtime_metric>)>;

   struct plugin_metrics {

      virtual ~plugin_metrics() = default;
      virtual std::vector<runtime_metric> metrics()=0;

      bool should_post() {
         return (_listener && (fc::time_point::now() > (_last_post + _min_post_interval_us)));
      }

      bool post_metrics() {
         if (should_post()){
            _listener(metrics());
            _last_post = fc::time_point::now();
            return true;
         }

         return false;
      }

      void register_listener(metrics_listener listener) {
         _listener = std::move(listener);
      }

      explicit plugin_metrics(fc::microseconds min_post_interval_us = fc::milliseconds(250))
      : _min_post_interval_us(min_post_interval_us)
      , _listener(nullptr) {}

   private:
      fc::microseconds _min_post_interval_us;
      metrics_listener _listener;
      fc::time_point _last_post;
   };

}

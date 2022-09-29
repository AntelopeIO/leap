#pragma once

#include <cstdint>
#include <string>
#include <tuple>
#include <vector>

//#include "prometheus/detail/core_export.h"

namespace prometheus {

   struct /* PROMETHEUS_CPP_CORE_EXPORT */ client_metric {
         // label_t

         struct label_t {
            std::string name;
            std::string value;

            bool operator<(const label_t& rhs) const {
               return std::tie(name, value) < std::tie(rhs.name, rhs.value);
            }

            bool operator==(const label_t& rhs) const {
               return std::tie(name, value) == std::tie(rhs.name, rhs.value);
            }
         };
         std::vector<label_t> label;

         // counter_t

         struct counter_t {
            double value = 0.0;
         };
         counter_t counter;

         // gauge_t

         struct gauge_t {
            double value = 0.0;
         };
         gauge_t gauge;

         // info_t

         struct info_t {
            double value = 1.0;
         };
         info_t info;

         // summary_t

         struct quantile_t {
            double quantile = 0.0;
            double value = 0.0;
         };

         struct summary_t {
            std::uint64_t sample_count = 0;
            double sample_sum = 0.0;
            std::vector<quantile_t> quantile;
         };
         summary_t summary;

         // histogram_metric

         struct bucket_t {
            std::uint64_t cumulative_count = 0;
            double upper_bound = 0.0;
         };

         struct histogram_metric {
            std::uint64_t sample_count = 0;
            double sample_sum = 0.0;
            std::vector<bucket_t> bucket;
         };
         histogram_metric histogram;

         // untyped_t

         struct untyped_t {
            double value = 0;
         };
         untyped_t untyped;

         // Timestamp

         std::int64_t timestamp_ms = 0;
   };

}  // namespace prometheus
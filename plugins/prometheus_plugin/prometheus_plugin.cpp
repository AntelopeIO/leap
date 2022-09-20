#include <eosio/prometheus_plugin/prometheus_plugin.hpp>
#include <fc/log/logger.hpp>

namespace eosio {

   static appbase::abstract_plugin &_prometheus_plugin = app().register_plugin<prometheus_plugin>();

   struct prometheus_plugin_impl {

   };

   prometheus_plugin::prometheus_plugin() {
     my.reset(new prometheus_plugin_impl{});
   }

   prometheus_plugin::~prometheus_plugin() {}

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
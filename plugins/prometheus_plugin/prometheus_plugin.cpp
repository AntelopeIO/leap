#include <eosio/prometheus_plugin/prometheus_plugin.hpp>

#include <eosio/chain/plugin_interface.hpp>
#include <eosio/chain/thread_utils.hpp>
#include <eosio/http_plugin/macros.hpp>
#include <eosio/http_plugin/http_plugin.hpp>

#include <fc/log/logger.hpp>

#include "metrics.hpp"

namespace eosio { 

   using namespace prometheus;

   static auto _prometheus_plugin = application::register_plugin<prometheus_plugin>();

   struct prometheus_plugin_impl {

      eosio::chain::named_thread_pool<struct prom> _prometheus_thread_pool;
      boost::asio::io_context::strand              _prometheus_strand;
      metrics::catalog_type                        _catalog;
      fc::microseconds                             _max_response_time_us;

      prometheus_plugin_impl(): _prometheus_strand(_prometheus_thread_pool.get_executor()){ 
         _catalog.register_update_handlers(_prometheus_strand);
      }
   };

   prometheus_plugin::prometheus_plugin()
   : my(new prometheus_plugin_impl{}) {
   }

   prometheus_plugin::~prometheus_plugin() = default;

   void prometheus_plugin::set_program_options(options_description&, options_description& cfg) {
   }

   struct prometheus_api_handle {
      prometheus_plugin_impl* _impl;
      
      fc::time_point start() const {
         return fc::time_point::now() + _impl->_max_response_time_us;
      }

      void metrics(const fc::variant_object&, chain::plugin_interface::next_function<std::string> results) {
         _impl->_prometheus_strand.post([this, results=std::move(results)]() {
            results(_impl->_catalog.report());
         });
      }
   };
   using metrics_params = fc::variant_object;


   void prometheus_plugin::plugin_initialize(const variables_map& options) {

      auto& _http_plugin = app().get_plugin<http_plugin>();
      my->_max_response_time_us = _http_plugin.get_max_response_time();

      prometheus_api_handle handle{my.get()};
      app().get_plugin<http_plugin>().add_async_api({
        CALL_ASYNC_WITH_400(prometheus, prometheus, handle, eosio, metrics, std::string, 200, http_params_types::no_params)}
        , http_content_type::plaintext);
   }

   void prometheus_plugin::plugin_startup() {
      my->_prometheus_thread_pool.start(1, [](const fc::exception& e) { 
         elog("Prometheus exception ${e}", ("e", e)); }
      );

      my->_catalog.update_prometheus_info();

      ilog("Prometheus plugin started.");
   }

   void prometheus_plugin::plugin_shutdown() {
      my->_prometheus_thread_pool.stop();
      ilog("Prometheus plugin shutdown.");
   }
}
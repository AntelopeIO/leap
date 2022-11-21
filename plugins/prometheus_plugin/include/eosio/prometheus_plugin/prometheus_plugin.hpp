#pragma once

#include <appbase/application.hpp>
#include <eosio/http_plugin/http_plugin.hpp>
#include <eosio/chain_plugin/chain_plugin.hpp>
#include <eosio/net_plugin/net_plugin.hpp>
#include <eosio/producer_plugin/producer_plugin.hpp>

namespace eosio {

   using namespace appbase;

   class prometheus_plugin : public appbase::plugin<prometheus_plugin> {
   public:
      prometheus_plugin();
      ~prometheus_plugin();

      APPBASE_PLUGIN_REQUIRES((http_plugin) (chain_plugin) (net_plugin) (producer_plugin))

      virtual void set_program_options(options_description&, options_description& cfg) override;

      void plugin_initialize(const variables_map& options);
      void plugin_startup();
      void plugin_shutdown();

      std::string scrape();

   private:
      std::unique_ptr<struct prometheus_plugin_impl> my;
   };

}

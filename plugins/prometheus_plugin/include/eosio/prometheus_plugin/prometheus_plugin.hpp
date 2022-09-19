#pragma once

#include <appbase/application.hpp>

namespace eosio {

   using namespace appbase;

   class prometheus_plugin : public appbase::plugin<prometheus_plugin> {
   public:
      prometheus_plugin();
      ~prometheus_plugin();

      APPBASE_PLUGIN_REQUIRES()

      virtual void set_program_options(options_description&, options_description& cfg) override;

      void plugin_initialize(const variables_map& options);
      void plugin_startup();
      void plugin_shutdown();

   private:
      std::unique_ptr<struct prometheus_plugin_impl> my;
   };

}

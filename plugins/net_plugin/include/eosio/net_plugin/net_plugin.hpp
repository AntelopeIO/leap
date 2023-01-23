#pragma once
#include <appbase/application.hpp>
#include <eosio/chain_plugin/chain_plugin.hpp>
#include <eosio/net_plugin/protocol.hpp>

namespace eosio {
   using namespace appbase;

   struct connection_status {
      string            peer;
      bool              connecting = false;
      bool              syncing    = false;
      handshake_message last_handshake;
   };

   using namespace plugin_interface;

   struct net_plugin_metrics : plugin_metrics {
      runtime_metric num_peers{metric_type::gauge, "num_peers", "num_peers", 0};
      runtime_metric num_clients{metric_type::gauge, "num_clients", "num_clients", 0};
      runtime_metric dropped_trxs{metric_type::counter, "dropped_trxs", "dropped_trxs", 0};

      vector<runtime_metric> metrics() final {
         vector<runtime_metric> metrics {
            num_peers,
            num_clients,
            dropped_trxs
         };

         return metrics;
      }
   };

   class net_plugin : public appbase::plugin<net_plugin>
   {
      public:
        net_plugin();
        virtual ~net_plugin();

        APPBASE_PLUGIN_REQUIRES((chain_plugin))
        virtual void set_program_options(options_description& cli, options_description& cfg) override;
        void handle_sighup() override;

        void plugin_initialize(const variables_map& options);
        void plugin_startup();
        void plugin_shutdown();

        string                            connect( const string& endpoint );
        string                            disconnect( const string& endpoint );
        std::optional<connection_status>  status( const string& endpoint )const;
        vector<connection_status>         connections()const;

        void register_metrics_listener(metrics_listener listener);

      private:
        std::shared_ptr<class net_plugin_impl> my;
   };

}

FC_REFLECT( eosio::connection_status, (peer)(connecting)(syncing)(last_handshake) )

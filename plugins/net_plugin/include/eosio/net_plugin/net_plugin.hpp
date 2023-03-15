#pragma once

#include <eosio/chain/application.hpp>
#include <eosio/net_plugin/protocol.hpp>
#include <eosio/chain/plugin_metrics.hpp>
#include <eosio/chain_plugin/chain_plugin.hpp>

namespace eosio {
   using namespace appbase;

   struct net_plugin_metrics : chain::plugin_interface::plugin_metrics {
      chain::plugin_interface::runtime_metric num_peers{ chain::plugin_interface::metric_type::gauge, "num_peers", "num_peers", 0 };
      chain::plugin_interface::runtime_metric num_clients{ chain::plugin_interface::metric_type::gauge, "num_clients", "num_clients", 0 };
      chain::plugin_interface::runtime_metric dropped_trxs{ chain::plugin_interface::metric_type::counter, "dropped_trxs", "dropped_trxs", 0 };

      vector<chain::plugin_interface::runtime_metric> metrics() final {
         vector<chain::plugin_interface::runtime_metric> metrics {
            num_peers,
            num_clients,
            dropped_trxs
         };

         return metrics;
      }
   };

   struct connection_status {
      string            peer;
      bool              connecting = false;
      bool              syncing    = false;
      bool              is_bp_peer = false;
      handshake_message last_handshake;
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

        void register_metrics_listener(chain::plugin_interface::metrics_listener listener);

      private:
        std::shared_ptr<class net_plugin_impl> my;
   };

}

FC_REFLECT( eosio::connection_status, (peer)(connecting)(syncing)(is_bp_peer)(last_handshake) )

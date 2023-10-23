#pragma once

#include <eosio/chain/application.hpp>
#include <eosio/net_plugin/protocol.hpp>
#include <eosio/chain_plugin/chain_plugin.hpp>

namespace eosio {
   using namespace appbase;

   struct connection_status {
      string            peer;
      string            remote_ip;
      string            remote_port;
      bool              connecting           = false;
      bool              syncing              = false;
      bool              is_bp_peer           = false;
      bool              is_socket_open       = false;
      bool              is_blocks_only       = false;
      bool              is_transactions_only = false;
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

        struct p2p_per_connection_metrics {
            struct connection_metric {
               uint32_t connection_id{0};
               boost::asio::ip::address_v6::bytes_type address;
               unsigned short port{0};
               bool accepting_blocks{false};
               uint32_t last_received_block{0};
               uint32_t first_available_block{0};
               uint32_t last_available_block{0};
               size_t unique_first_block_count{0};
               uint64_t latency{0};
               size_t bytes_received{0};
               std::chrono::nanoseconds last_bytes_received{0};
               size_t bytes_sent{0};
               std::chrono::nanoseconds last_bytes_sent{0};
               size_t block_sync_bytes_received{0};
               size_t block_sync_bytes_sent{0};
               bool block_sync_throttling{false};
               std::chrono::nanoseconds connection_start_time{0};
               std::string p2p_address;
               std::string unique_conn_node_id;
            };
            explicit p2p_per_connection_metrics(size_t count) {
               peers.reserve(count);
            }
            p2p_per_connection_metrics(p2p_per_connection_metrics&& other)
               : peers{std::move(other.peers)}
            {}
            p2p_per_connection_metrics(const p2p_per_connection_metrics&) = delete;
            p2p_per_connection_metrics& operator=(const p2p_per_connection_metrics&) = delete;
            std::vector<connection_metric> peers;
        };
        struct p2p_connections_metrics {
           p2p_connections_metrics(std::size_t peers, std::size_t clients, p2p_per_connection_metrics&& statistics)
              : num_peers{peers}
              , num_clients{clients}
              , stats{std::move(statistics)}
           {}
           p2p_connections_metrics(p2p_connections_metrics&& statistics)
              : num_peers{std::move(statistics.num_peers)}
              , num_clients{std::move(statistics.num_clients)}
              , stats{std::move(statistics.stats)}
           {}
           p2p_connections_metrics(const p2p_connections_metrics&) = delete;
           std::size_t num_peers   = 0;
           std::size_t num_clients = 0;
           p2p_per_connection_metrics stats;
        };

        void register_update_p2p_connection_metrics(std::function<void(p2p_connections_metrics)>&&);
        void register_increment_failed_p2p_connections(std::function<void()>&&);
        void register_increment_dropped_trxs(std::function<void()>&&);

      private:
        std::shared_ptr<class net_plugin_impl> my;
   };

}

FC_REFLECT( eosio::connection_status, (peer)(remote_ip)(remote_port)(connecting)(syncing)(is_bp_peer)(is_socket_open)(is_blocks_only)(is_transactions_only)(last_handshake) )

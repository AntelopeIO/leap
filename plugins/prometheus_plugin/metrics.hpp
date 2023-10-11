#pragma once

#include <eosio/http_plugin/http_plugin.hpp>
#include <eosio/net_plugin/net_plugin.hpp>
#include <eosio/producer_plugin/producer_plugin.hpp>

#include <prometheus/counter.h>
#include <prometheus/info.h>
#include <prometheus/registry.h>
#include <prometheus/text_serializer.h>
#include <fc/log/logger.hpp>
namespace eosio::metrics {

struct catalog_type {

   using Gauge   = prometheus::Gauge;
   using Counter = prometheus::Counter;

   template <typename T>
   prometheus::Family<T>& family(const std::string& name, const std::string& help) {
      return prometheus::detail::Builder<T>{}.Name(name).Help(help).Register(registry);
   }

   template <typename T>
   T& build(const std::string& name, const std::string& help) {
      return family<T>(name, help).Add({});
   }

   prometheus::Registry registry;
   // nodeos
   prometheus::Family<prometheus::Info>& info;
   prometheus::Info info_details;
   // http plugin
   prometheus::Family<Counter>& http_request_counts;

   // net plugin failed p2p connection
   Counter& failed_p2p_connections;

   // net plugin dropped_trxs
   Counter& dropped_trxs_total;

   struct p2p_connection_metrics {
      Gauge& num_peers;
      Gauge& num_clients;

      prometheus::Family<Gauge>& addr; // Empty gauge; ipv6 address can't be transmitted as a double
      prometheus::Family<Gauge>& port;
      prometheus::Family<Gauge>& connection_number;
      prometheus::Family<Gauge>& accepting_blocks;
      prometheus::Family<Gauge>& last_received_block;
      prometheus::Family<Gauge>& first_available_block;
      prometheus::Family<Gauge>& last_available_block;
      prometheus::Family<Gauge>& unique_first_block_count;
      prometheus::Family<Gauge>& latency;
      prometheus::Family<Gauge>& bytes_received;
      prometheus::Family<Gauge>& last_bytes_received;
      prometheus::Family<Gauge>& bytes_sent;
      prometheus::Family<Gauge>& last_bytes_sent;
      prometheus::Family<Gauge>& connection_start_time;
      prometheus::Family<Gauge>& peer_addr; // Empty gauge; we only want the label
   };
   p2p_connection_metrics p2p_metrics;

   // producer plugin
   prometheus::Family<Counter>& cpu_usage_us;
   prometheus::Family<Counter>& net_usage_us;

   Gauge& last_irreversible;
   Gauge& head_block_num;

   struct block_metrics {
      Counter& num_blocks_created;
      Gauge&   current_block_num;
      Counter& block_total_time_us_block;
      Counter& block_idle_time_us_block;
      Counter& block_num_success_trx_block;
      Counter& block_success_trx_time_us_block;
      Counter& block_num_failed_trx_block;
      Counter& block_fail_trx_time_us_block;
      Counter& block_num_transient_trx_block;
      Counter& block_transient_trx_time_us_block;
      Counter& block_other_time_us_block;
   };

   // produced blocks
   Counter& unapplied_transactions_total;
   Counter& subjective_bill_account_size_total;
   Counter& scheduled_trxs_total;
   Counter& trxs_produced_total;
   Counter& cpu_usage_us_produced_block;
   Counter& total_elapsed_time_us_produced_block;
   Counter& total_time_us_produced_block;
   Counter& net_usage_us_produced_block;
   block_metrics produced_metrics;

   // speculative blocks
   block_metrics speculative_metrics;

   // incoming blocks
   Counter& trxs_incoming_total;
   Counter& cpu_usage_us_incoming_block;
   Counter& total_elapsed_time_us_incoming_block;
   Counter& total_time_us_incoming_block;
   Counter& net_usage_us_incoming_block;
   Counter& latency_us_incoming_block;
   Counter& blocks_incoming;

   // prometheus exporter
   Counter& bytes_transferred;
   Counter& num_scrapes;


   catalog_type()
       : info(family<prometheus::Info>("nodeos", "static information about the server"))
       , http_request_counts(family<Counter>("nodeos_http_requests_total", "number of HTTP requests"))
       , failed_p2p_connections(build<Counter>("nodeos_p2p_failed_connections", "total number of failed out-going p2p connections"))
       , dropped_trxs_total(build<Counter>("nodeos_p2p_dropped_trxs_total", "total number of dropped transactions by net plugin"))
       , p2p_metrics{
              .num_peers{build<Gauge>("nodeos_p2p_peers", "current number of connected outgoing peers")}
            , .num_clients{build<Gauge>("nodeos_p2p_clients", "current number of connected incoming clients")}
            , .addr{family<Gauge>("nodeos_p2p_addr", "ipv6 address")}
            , .port{family<Gauge>("nodeos_p2p_port", "port")}
            , .connection_number{family<Gauge>("nodeos_p2p_connection_number", "monatomic increasing connection number")}
            , .accepting_blocks{family<Gauge>("nodeos_p2p_accepting_blocks", "accepting blocks on connection")}
            , .last_received_block{family<Gauge>("nodeos_p2p_last_received_block", "last received block on connection")}
            , .first_available_block{family<Gauge>("nodeos_p2p_first_available_block", "first block available from connection")}
            , .last_available_block{family<Gauge>("nodeos_p2p_last_available_block", "last block available from connection")}
            , .unique_first_block_count{family<Gauge>("nodeos_p2p_unique_first_block_count", "number of blocks first received from any connection on this connection")}
            , .latency{family<Gauge>("nodeos_p2p_latency", "last calculated latency with connection")}
            , .bytes_received{family<Gauge>("nodeos_p2p_bytes_received", "total bytes received on connection")}
            , .last_bytes_received{family<Gauge>("nodeos_p2p_last_bytes_received", "last time anything received from peer")}
            , .bytes_sent{family<Gauge>("nodeos_p2p_bytes_sent", "total bytes sent to peer")}
            , .last_bytes_sent{family<Gauge>("nodeos_p2p_last_bytes_sent", "last time anything sent to peer")}
            , .connection_start_time{family<Gauge>("nodeos_p2p_connection_start_time", "time of last connection to peer")}
            , .peer_addr{family<Gauge>("nodeos_p2p_peer_addr", "peer address")}
         }
       , cpu_usage_us(family<Counter>("nodeos_cpu_usage_us_total", "total cpu usage in microseconds for blocks"))
       , net_usage_us(family<Counter>("nodeos_net_usage_us_total", "total net usage in microseconds for blocks"))
       , last_irreversible(build<Gauge>("nodeos_last_irreversible", "last irreversible block number"))
       , head_block_num(build<Gauge>("nodeos_head_block_num", "head block number"))
       , unapplied_transactions_total(build<Counter>("nodeos_unapplied_transactions_total",
                                                     "total number of unapplied transactions from produced blocks"))
       , subjective_bill_account_size_total(build<Counter>(
             "nodeos_subjective_bill_account_size_total", "total number of subjective bill account size from produced blocks"))
       , scheduled_trxs_total(
             build<Counter>("nodeos_scheduled_trxs_total", "total number of scheduled transactions from produced blocks"))
       , trxs_produced_total(build<Counter>("nodeos_trxs_produced_total", "number of transactions produced"))
       , cpu_usage_us_produced_block(cpu_usage_us.Add({{"block_type", "produced"}}))
       , total_elapsed_time_us_produced_block(build<Counter>("nodeos_produced_elapsed_us_total", "total produced blocks elapsed time"))
       , total_time_us_produced_block(build<Counter>("nodeos_produced_us_total", "total produced blocks total time"))
       , net_usage_us_produced_block(net_usage_us.Add({{"block_type", "produced"}}))
       , produced_metrics{ .num_blocks_created{build<Counter>("nodeos_blocks_produced", "number of blocks produced")}
                         , .current_block_num{build<Gauge>("nodeos_block_num", "current block number")}
                         , .block_total_time_us_block{build<Counter>("nodeos_total_time_us_produced_block", "total time for produced block")}
                         , .block_idle_time_us_block{build<Counter>("nodeos_idle_time_us_produced_block", "idle time for produced block")}
                         , .block_num_success_trx_block{build<Counter>("nodeos_num_success_trx_produced_block", "number of successful transactions in produced block")}
                         , .block_success_trx_time_us_block{build<Counter>("nodeos_success_trx_time_us_produced_block", "time for successful transactions in produced block")}
                         , .block_num_failed_trx_block{build<Counter>("nodeos_num_failed_trx_produced_block", "number of failed transactions during produced block")}
                         , .block_fail_trx_time_us_block{build<Counter>("nodeos_fail_trx_time_us_produced_block", "time for failed transactions during produced block")}
                         , .block_num_transient_trx_block{build<Counter>("nodeos_num_transient_trx_produced_block", "number of transient transactions during produced block")}
                         , .block_transient_trx_time_us_block{build<Counter>("nodeos_transient_trx_time_us_produced_block", "time for transient transactions during produced block")}
                         , .block_other_time_us_block{build<Counter>("nodeos_other_time_us_produced_block", "all other unaccounted time during produced block")} }
       , speculative_metrics{ .num_blocks_created{build<Counter>("nodeos_blocks_speculative_num", "number of speculative blocks created")}
                            , .current_block_num{build<Gauge>("nodeos_block_num", "current block number")}
                            , .block_total_time_us_block{build<Counter>("nodeos_total_time_us_speculative_block", "total time for speculative block")}
                            , .block_idle_time_us_block{build<Counter>("nodeos_idle_time_us_speculative_block", "idle time for speculative block")}
                            , .block_num_success_trx_block{build<Counter>("nodeos_num_success_trx_speculative_block", "number of successful transactions in speculative block")}
                            , .block_success_trx_time_us_block{build<Counter>("nodeos_success_trx_time_us_speculative_block", "time for successful transactions in speculative block")}
                            , .block_num_failed_trx_block{build<Counter>("nodeos_num_failed_trx_speculative_block", "number of failed transactions during speculative block")}
                            , .block_fail_trx_time_us_block{build<Counter>("nodeos_fail_trx_time_us_speculative_block", "time for failed transactions during speculative block")}
                            , .block_num_transient_trx_block{build<Counter>("nodeos_num_transient_trx_speculative_block", "number of transient transactions during speculative block")}
                            , .block_transient_trx_time_us_block{build<Counter>("nodeos_transient_trx_time_us_speculative_block", "time for transient transactions during speculative block")}
                            , .block_other_time_us_block{build<Counter>("nodeos_other_time_us_speculative_block", "all other unaccounted time during speculative block")} }
       , trxs_incoming_total(build<Counter>("nodeos_trxs_incoming_total", "number of incoming transactions"))
       , cpu_usage_us_incoming_block(cpu_usage_us.Add({{"block_type", "incoming"}}))
       , total_elapsed_time_us_incoming_block(build<Counter>("nodeos_incoming_elapsed_us_total", "total incoming blocks elapsed time"))
       , total_time_us_incoming_block(build<Counter>("nodeos_incoming_us_total", "total incoming blocks total time"))
       , net_usage_us_incoming_block(net_usage_us.Add({{"block_type", "incoming"}}))
       , latency_us_incoming_block(build<Counter>("nodeos_incoming_us_block_latency", "total incoming block latency"))
       , blocks_incoming(build<Counter>("nodeos_blocks_incoming", "number of incoming blocks"))
       , bytes_transferred(build<Counter>("exposer_transferred_bytes_total",
                                          "total number of bytes for responses to prometheus scrape requests"))
       , num_scrapes(build<Counter>("exposer_scrapes_total", "total number of prometheus scrape requests received")) {}

   std::string report() {
      const prometheus::TextSerializer serializer;
      auto                             result = serializer.Serialize(registry.Collect());
      bytes_transferred.Increment(result.size());
      num_scrapes.Increment(1);
      return result;
   }

   void update(const http_plugin::metrics& metrics) {
      http_request_counts.Add({{"handler", metrics.target}}).Increment(1);
   }

   void update(const net_plugin::p2p_connections_metrics& metrics) {
      p2p_metrics.num_peers.Set(metrics.num_peers);
      p2p_metrics.num_clients.Set(metrics.num_clients);
      for(size_t i = 0; i < metrics.stats.peers.size(); ++i) {
         auto& peer = metrics.stats.peers[i];
         auto& conn_id = peer.unique_conn_node_id;

         auto addr = boost::asio::ip::make_address_v6(peer.address).to_string();
         p2p_metrics.addr.Add({{"connid", conn_id},{"ipv6", addr},{"address", peer.p2p_address}});

         auto add_and_set_gauge = [&](auto& fam, const auto& value) {
            auto& gauge = fam.Add({{"connid", conn_id}});
            gauge.Set(value);
         };

         add_and_set_gauge(p2p_metrics.connection_number, peer.connection_id);
         add_and_set_gauge(p2p_metrics.port, peer.port);
         add_and_set_gauge(p2p_metrics.accepting_blocks, peer.accepting_blocks);
         add_and_set_gauge(p2p_metrics.last_received_block, peer.last_received_block);
         add_and_set_gauge(p2p_metrics.first_available_block, peer.first_available_block);
         add_and_set_gauge(p2p_metrics.last_available_block, peer.last_available_block);
         add_and_set_gauge(p2p_metrics.unique_first_block_count, peer.unique_first_block_count);
         add_and_set_gauge(p2p_metrics.latency, peer.latency);
         add_and_set_gauge(p2p_metrics.bytes_received, peer.bytes_received);
         add_and_set_gauge(p2p_metrics.last_bytes_received, peer.last_bytes_received.count());
         add_and_set_gauge(p2p_metrics.bytes_sent, peer.bytes_sent);
         add_and_set_gauge(p2p_metrics.last_bytes_sent, peer.last_bytes_sent.count());
         add_and_set_gauge(p2p_metrics.connection_start_time, peer.connection_start_time.count());
      }
   }

   void update(block_metrics& blk_metrics, const producer_plugin::speculative_block_metrics& metrics) {
      blk_metrics.num_blocks_created.Increment(1);
      blk_metrics.current_block_num.Set(metrics.block_num);
      blk_metrics.block_total_time_us_block.Increment(metrics.block_total_time_us);
      blk_metrics.block_idle_time_us_block.Increment(metrics.block_idle_us);
      blk_metrics.block_num_success_trx_block.Increment(metrics.num_success_trx);
      blk_metrics.block_success_trx_time_us_block.Increment(metrics.success_trx_time_us);
      blk_metrics.block_num_failed_trx_block.Increment(metrics.num_fail_trx);
      blk_metrics.block_fail_trx_time_us_block.Increment(metrics.fail_trx_time_us);
      blk_metrics.block_num_transient_trx_block.Increment(metrics.num_transient_trx);
      blk_metrics.block_transient_trx_time_us_block.Increment(metrics.transient_trx_time_us);
      blk_metrics.block_other_time_us_block.Increment(metrics.block_other_time_us);
   }

   void update(const producer_plugin::produced_block_metrics& metrics) {
      unapplied_transactions_total.Increment(metrics.unapplied_transactions_total);
      subjective_bill_account_size_total.Increment(metrics.subjective_bill_account_size_total);
      scheduled_trxs_total.Increment(metrics.scheduled_trxs_total);
      trxs_produced_total.Increment(metrics.trxs_produced_total);
      cpu_usage_us_produced_block.Increment(metrics.cpu_usage_us);
      total_elapsed_time_us_produced_block.Increment(metrics.total_elapsed_time_us);
      total_time_us_produced_block.Increment(metrics.total_time_us);
      net_usage_us_produced_block.Increment(metrics.net_usage_us);

      update(produced_metrics, metrics);

      last_irreversible.Set(metrics.last_irreversible);
      head_block_num.Set(metrics.head_block_num);
   }

   void update(const producer_plugin::speculative_block_metrics& metrics) {
      update(speculative_metrics, metrics);
   }

   void update(const producer_plugin::incoming_block_metrics& metrics) {
      trxs_incoming_total.Increment(metrics.trxs_incoming_total);
      blocks_incoming.Increment(1);
      cpu_usage_us_incoming_block.Increment(metrics.cpu_usage_us);
      total_elapsed_time_us_incoming_block.Increment(metrics.total_elapsed_time_us);
      total_time_us_incoming_block.Increment(metrics.total_time_us);
      net_usage_us_incoming_block.Increment(metrics.net_usage_us);
      latency_us_incoming_block.Increment(metrics.block_latency_us);

      last_irreversible.Set(metrics.last_irreversible);
      head_block_num.Set(metrics.head_block_num);
   }

   void update_prometheus_info() {
      info_details = info.Add({
            {"server_version", chain_apis::itoh(static_cast<uint32_t>(app().version()))},
            {"chain_id", app().get_plugin<chain_plugin>().get_chain_id()},
            {"server_version_string", app().version_string()},
            {"server_full_version_string", app().full_version_string()},
            {"earliest_available_block_num", to_string(app().get_plugin<chain_plugin>().chain().earliest_available_block_num())}
         });
   }
   void register_update_handlers(boost::asio::io_context::strand& strand) {
      auto& http = app().get_plugin<http_plugin>();
      http.register_update_metrics(
          [&strand, this](http_plugin::metrics metrics) { strand.post([metrics = std::move(metrics), this]() { update(metrics); }); });

      auto& net = app().get_plugin<net_plugin>();

      net.register_update_p2p_connection_metrics([&strand, this](net_plugin::p2p_connections_metrics&& metrics) {
         boost::asio::post(strand, [metrics = std::move(metrics), this]() mutable { update(std::move(metrics)); });
      });

      net.register_increment_failed_p2p_connections([this]() {
         // Increment is thread safe
         failed_p2p_connections.Increment(1);
      });
      net.register_increment_dropped_trxs([this]() { 
         // Increment is thread safe
         dropped_trxs_total.Increment(1);
      });

      auto& producer = app().get_plugin<producer_plugin>();
      producer.register_update_produced_block_metrics(
          [&strand, this](const producer_plugin::produced_block_metrics& metrics) {
             strand.post([metrics, this]() { update(metrics); });
          });
      producer.register_update_speculative_block_metrics(
              [&strand, this](const producer_plugin::speculative_block_metrics& metrics) {
                 strand.post([metrics, this]() { update(metrics); });
              });
      producer.register_update_incoming_block_metrics(
          [&strand, this](const producer_plugin::incoming_block_metrics& metrics) {
             strand.post([metrics, this]() { update(metrics); });
          });
   }
};

} // namespace eosio::metrics

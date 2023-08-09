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

   // net plugin p2p-connections
   prometheus::Family<Gauge>& p2p_connections;

   Gauge& num_peers;
   Gauge& num_clients;
   std::vector<std::string> addresses;
   std::vector<std::reference_wrapper<Gauge>> accepting_blocks_gauges;
   std::vector<std::reference_wrapper<Gauge>> last_received_blocks_gauges;
   std::vector<std::reference_wrapper<Gauge>> first_available_blocks_gauges;
   std::vector<std::reference_wrapper<Gauge>> last_available_blocks_gauges;
   std::vector<std::reference_wrapper<Gauge>> unique_first_block_counts_gauges;
   std::vector<std::reference_wrapper<Gauge>> latency_gauges;
   std::vector<std::reference_wrapper<Gauge>> bytes_received_gauges;
   std::vector<std::reference_wrapper<Gauge>> last_bytes_received_gauges;
   std::vector<std::reference_wrapper<Gauge>> bytes_sent_gauges;
   std::vector<std::reference_wrapper<Gauge>> last_bytes_sent_gauges;
   std::vector<std::reference_wrapper<Gauge>> connection_start_time_gauges;
   std::vector<std::reference_wrapper<Gauge>> log_p2p_address_gauges;

   // net plugin failed p2p connection
   Counter& failed_p2p_connections;

   // net plugin dropped_trxs
   Counter& dropped_trxs_total;

   // producer plugin
   prometheus::Family<Counter>& cpu_usage_us;
   prometheus::Family<Counter>& net_usage_us;

   Gauge& last_irreversible;
   Gauge& head_block_num;

   // produced blocks
   Counter& unapplied_transactions_total;
   Counter& blacklisted_transactions_total;
   Counter& subjective_bill_account_size_total;
   Counter& scheduled_trxs_total;
   Counter& trxs_produced_total;
   Counter& cpu_usage_us_produced_block;
   Counter& net_usage_us_produced_block;
   Counter& blocks_produced;

   // incoming blocks
   Counter& trxs_incoming_total;
   Counter& cpu_usage_us_incoming_block;
   Counter& net_usage_us_incoming_block;
   Counter& blocks_incoming;

   // prometheus exporter
   Counter& bytes_transferred;
   Counter& num_scrapes;


   catalog_type()
       : info(family<prometheus::Info>("nodeos", "static information about the server"))
       , http_request_counts(family<Counter>("nodeos_http_requests_total", "number of HTTP requests"))
       , p2p_connections(family<Gauge>("nodeos_p2p_connections", "current number of connected p2p connections"))
       , num_peers(p2p_connections.Add({{"direction", "out"}}))
       , num_clients(p2p_connections.Add({{"direction", "in"}}))
       , failed_p2p_connections(
             build<Counter>("nodeos_failed_p2p_connections", "total number of failed out-going p2p connections"))
       , dropped_trxs_total(build<Counter>("nodeos_dropped_trxs_total", "total number of dropped transactions by net plugin"))
       , cpu_usage_us(family<Counter>("nodeos_cpu_usage_us_total", "total cpu usage in microseconds for blocks"))
       , net_usage_us(family<Counter>("nodeos_net_usage_us_total", "total net usage in microseconds for blocks"))
       , last_irreversible(build<Gauge>("nodeos_last_irreversible", "last irreversible block number"))
       , head_block_num(build<Gauge>("nodeos_head_block_num", "head block number"))
       , unapplied_transactions_total(build<Counter>("nodeos_unapplied_transactions_total",
                                                     "total number of unapplied transactions from produced blocks"))
       , blacklisted_transactions_total(build<Counter>("nodeos_blacklisted_transactions_total",
                                                       "total number of blacklisted transactions from produced blocks"))
       , subjective_bill_account_size_total(build<Counter>(
             "nodeos_subjective_bill_account_size_total", "total number of subjective bill account size from produced blocks"))
       , scheduled_trxs_total(
             build<Counter>("nodeos_scheduled_trxs_total", "total number of scheduled transactions from produced blocks"))
       , trxs_produced_total(build<Counter>("nodeos_trxs_produced_total", "number of transactions produced"))
       , cpu_usage_us_produced_block(cpu_usage_us.Add({{"block_type", "produced"}}))
       , net_usage_us_produced_block(net_usage_us.Add({{"block_type", "produced"}}))
       , blocks_produced(build<Counter>("nodeos_blocks_produced", "number of blocks produced"))
       , trxs_incoming_total(build<Counter>("nodeos_trxs_incoming_total", "number of incoming transactions"))
       , cpu_usage_us_incoming_block(cpu_usage_us.Add({{"block_type", "incoming"}}))
       , net_usage_us_incoming_block(net_usage_us.Add({{"block_type", "incoming"}}))
       , blocks_incoming(build<Counter>("nodeos_blocks_incoming", "number of incoming blocks"))
       , bytes_transferred(build<Counter>("nodeos_exposer_transferred_bytes_total",
                                          "total number of bytes for responses to prometheus scape requests"))
       , num_scrapes(build<Counter>("nodeos_exposer_scrapes_total", "total number of prometheus scape requests received"))
       {}

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
      num_peers.Set(metrics.num_peers);
      num_clients.Set(metrics.num_clients);
      for(size_t i = 0; i < metrics.stats.peers.size(); ++i) {
         auto addr = boost::asio::ip::make_address_v6(metrics.stats.peers[i].address).to_string();
         boost::replace_all(addr, ":", "_COLON_");
         boost::replace_all(addr, ".", "_DOT_");
         addr.insert(0, "ip_");
         addr.append("_");
         addr.append(to_string(metrics.stats.peers[i].port));
         addresses.push_back(addr);
         auto add_and_set_gauge = [&](std::string label_value, 
                                      std::vector<std::reference_wrapper<Gauge>>& gauges,
                                      auto value) {
            auto& gauge = p2p_connections.Add({{addr, label_value}});
            gauge.Set(value);
            gauges.push_back(gauge);
         };
         auto& peer = metrics.stats.peers[i];
         add_and_set_gauge("accepting_blocks", accepting_blocks_gauges, peer.accepting_blocks);
         add_and_set_gauge("last_received_block", last_received_blocks_gauges, peer.last_received_block);
         add_and_set_gauge("first_available_block", first_available_blocks_gauges, peer.first_available_block);
         add_and_set_gauge("last_available_block", last_available_blocks_gauges, peer.last_available_block);
         add_and_set_gauge("unique_first_block_count", unique_first_block_counts_gauges, peer.unique_first_block_count);
         add_and_set_gauge("latency", latency_gauges, peer.latency);
         add_and_set_gauge("bytes_received", bytes_received_gauges, peer.bytes_received);
         add_and_set_gauge("last_bytes_received", last_bytes_received_gauges, peer.last_bytes_received);
         add_and_set_gauge("bytes_sent", bytes_sent_gauges, peer.bytes_sent);
         add_and_set_gauge("last_bytes_sent", last_bytes_sent_gauges, peer.last_bytes_sent);
         add_and_set_gauge("connection_start_time", connection_start_time_gauges, peer.connection_start_time.count());
         add_and_set_gauge(peer.log_p2p_address, log_p2p_address_gauges, 0); // Empty gauge; we only want the label
      }
   }

   void update(const producer_plugin::produced_block_metrics& metrics) {
      unapplied_transactions_total.Increment(metrics.unapplied_transactions_total);
      blacklisted_transactions_total.Increment(metrics.blacklisted_transactions_total);
      subjective_bill_account_size_total.Increment(metrics.subjective_bill_account_size_total);
      scheduled_trxs_total.Increment(metrics.scheduled_trxs_total);
      trxs_produced_total.Increment(metrics.trxs_produced_total);
      blocks_produced.Increment(1);
      cpu_usage_us_produced_block.Increment(metrics.cpu_usage_us);
      net_usage_us_produced_block.Increment(metrics.net_usage_us);

      last_irreversible.Set(metrics.last_irreversible);
      head_block_num.Set(metrics.head_block_num);
   }

   void update(const producer_plugin::incoming_block_metrics& metrics) {
      trxs_incoming_total.Increment(metrics.trxs_incoming_total);
      blocks_incoming.Increment(1);
      cpu_usage_us_incoming_block.Increment(metrics.cpu_usage_us);
      net_usage_us_incoming_block.Increment(metrics.net_usage_us);

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
      producer.register_update_incoming_block_metrics(
          [&strand, this](const producer_plugin::incoming_block_metrics& metrics) {
             strand.post([metrics, this]() { update(metrics); });
          });
   }
};

} // namespace eosio::metrics
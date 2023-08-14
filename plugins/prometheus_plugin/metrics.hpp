#pragma once

#include <eosio/http_plugin/http_plugin.hpp>
#include <eosio/net_plugin/net_plugin.hpp>
#include <eosio/producer_plugin/producer_plugin.hpp>

#include <prometheus/counter.h>
#include <prometheus/registry.h>
#include <prometheus/text_serializer.h>

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
   // http plugin
   prometheus::Family<Counter>& http_request_counts;

   // net plugin p2p-connections
   prometheus::Family<Gauge>& p2p_connections;

   Gauge& num_peers;
   Gauge& num_clients;

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
       : http_request_counts(family<Counter>("http_requests_total", "number of HTTP requests"))
       , p2p_connections(family<Gauge>("p2p_connections", "current number of connected p2p connections"))
       , num_peers(p2p_connections.Add({{"direction", "out"}}))
       , num_clients(p2p_connections.Add({{"direction", "in"}}))
       , failed_p2p_connections(
             build<Counter>("failed_p2p_connections", "total number of failed out-going p2p connections"))
       , dropped_trxs_total(build<Counter>("dropped_trxs_total", "total number of dropped transactions by net plugin"))
       , cpu_usage_us(family<Counter>("cpu_usage_us_total", "total cpu usage in microseconds for blocks"))
       , net_usage_us(family<Counter>("net_usage_us_total", "total net usage in microseconds for blocks"))
       , last_irreversible(build<Gauge>("last_irreversible", "last irreversible block number"))
       , head_block_num(build<Gauge>("head_block_num", "head block number"))
       , unapplied_transactions_total(build<Counter>("unapplied_transactions_total",
                                                     "total number of unapplied transactions from produced blocks"))
       , blacklisted_transactions_total(build<Counter>("blacklisted_transactions_total",
                                                       "total number of blacklisted transactions from produced blocks"))
       , subjective_bill_account_size_total(build<Counter>(
             "subjective_bill_account_size_total", "total number of subjective bill account size from produced blocks"))
       , scheduled_trxs_total(
             build<Counter>("scheduled_trxs_total", "total number of scheduled transactions from produced blocks"))
       , trxs_produced_total(build<Counter>("trxs_produced_total", "number of transactions produced"))
       , cpu_usage_us_produced_block(cpu_usage_us.Add({{"block_type", "produced"}}))
       , net_usage_us_produced_block(net_usage_us.Add({{"block_type", "produced"}}))
       , blocks_produced(build<Counter>("blocks_produced", "number of blocks produced"))
       , trxs_incoming_total(build<Counter>("trxs_incoming_total", "number of incoming transactions"))
       , cpu_usage_us_incoming_block(cpu_usage_us.Add({{"block_type", "incoming"}}))
       , net_usage_us_incoming_block(net_usage_us.Add({{"block_type", "incoming"}}))
       , blocks_incoming(build<Counter>("blocks_incoming", "number of incoming blocks"))
       , bytes_transferred(build<Counter>("exposer_transferred_bytes_total",
                                          "total number of bytes for responses to prometheus scape requests"))
       , num_scrapes(build<Counter>("exposer_scrapes_total", "total number of prometheus scape requests received")) {}

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

   void register_update_handlers(boost::asio::io_context::strand& strand) {
      auto& http = app().get_plugin<http_plugin>();
      http.register_update_metrics(
          [&strand, this](http_plugin::metrics metrics) { strand.post([metrics = std::move(metrics), this]() { update(metrics); }); });

      auto& net = app().get_plugin<net_plugin>();

      net.register_update_p2p_connection_metrics([&strand, this](net_plugin::p2p_connections_metrics metrics) {
         strand.post([metrics, this]() { update(metrics); });
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
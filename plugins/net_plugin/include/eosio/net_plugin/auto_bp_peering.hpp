#pragma once

#include <eosio/chain/producer_schedule.hpp>

namespace eosio::auto_bp_peering {

///
/// This file implements the functionality for block producers automatically establishing p2p connections to their
/// neighbors on the producer schedule.
///

using chain::account_name;
using chain::flat_map;
using chain::flat_set;

template <typename Derived, typename Connection>
class connection_manager {
#ifdef BOOST_TEST_MODULE
 public:
#endif

   flat_map<account_name, std::string> bp_peer_addresses;  // thread safe only because modified at plugin startup currently
   flat_map<std::string, account_name> bp_peer_accounts;   // thread safe only because modified at plugin startup currently
   flat_set<account_name>              my_accounts;
   flat_set<account_name>              pending_neighbors;
   flat_set<account_name>              active_neighbors;
   uint32_t                            pending_schedule_version = 0;
   uint32_t                            active_schedule_version  = 0;

   Derived*       self() { return static_cast<Derived*>(this); }
   const Derived* self() const { return static_cast<const Derived*>(this); }

   template <template<typename...> typename Container, typename... Rest >
   static std::string to_string(const Container<account_name, Rest...>& peers) {
      std::stringstream result;
      const char*       sep = "";
      for (auto p : peers) {
         result << sep << p.to_string();
         sep = ", ";
      }
      return result.str();
   }

   class neighbor_finder_type {

      const flat_map<account_name, std::string>&    bp_peer_addresses;
      const flat_set<account_name>&                 my_accounts;
      const std::vector<chain::producer_authority>& schedule;
      chain::flat_set<std::size_t>                  indices;
    public:
      neighbor_finder_type(const flat_map<account_name, std::string>&    bp_peer_addresses,
                           const flat_set<account_name>&                 my_accounts,
                           const std::vector<chain::producer_authority>& schedule)
          : bp_peer_addresses(bp_peer_addresses), my_accounts(my_accounts), schedule(schedule) {
         for (auto account : my_accounts) {
            auto itr = std::find_if(schedule.begin(), schedule.end(),
                                    [account](auto& e) { return e.producer_name == account; });
            if (itr != schedule.end())
               indices.insert(itr - schedule.begin());
         }
      }

      void add_neighbors_with_distance(chain::flat_set<account_name>& result, int distance) const {
         for (auto schedule_index : indices) {
            auto i = (schedule_index + distance) % schedule.size();
            if (!indices.count(i)) {
               auto name = schedule[i].producer_name;
               if (bp_peer_addresses.count(name))
                  result.insert(name);
            }
         }
      }

      flat_set<account_name> downstream_neighbors() const {
         chain::flat_set<account_name> result;
         for (int i = 0; i < proximity_count; ++i) { add_neighbors_with_distance(result, i + 1); }
         return result;
      }

      void add_upstream_neighbors(chain::flat_set<account_name>& result) const {
         for (int i = 0; i < proximity_count; ++i) { add_neighbors_with_distance(result, -1 - i); }
      }

      flat_set<account_name> neighbors() const {
         flat_set<account_name> result = downstream_neighbors();
         add_upstream_neighbors(result);
         return result;
      }
   };

 public:
   const static std::size_t proximity_count = 2;

   bool auto_bp_peering_enabled() const { return bp_peer_addresses.size() && my_accounts.size(); }

   // Only called at plugin startup
   void set_producer_accounts(const std::set<account_name>& accounts) {
      my_accounts.insert(accounts.begin(), accounts.end());
   }

   // Only called at plugin startup
   void set_bp_peers(const std::vector<std::string>& peers) {
      try {
         for (auto& entry : peers) {
            auto comma_pos = entry.find(',');
            EOS_ASSERT(comma_pos != std::string::npos, chain::plugin_config_exception,
                       "auto-bp-peer must consists an account name and server address separated by a comma token");
            auto         addr = entry.substr(comma_pos + 1);
            account_name account(entry.substr(0, comma_pos));

            bp_peer_addresses[account] = addr;
            bp_peer_accounts[addr]     = account;
         }
      } catch (eosio::chain::name_type_exception&) {
         EOS_ASSERT(false, chain::plugin_config_exception, "the account supplied by --auto-bp-peer option is invalid");
      }
   }

   // Only called at plugin startup
   template <typename T>
   void for_each_bp_peer_address(T&& fun) const {
      for (auto& [_, addr] : bp_peer_addresses) { fun(addr); }
   }

   // Only called from connection strand and the connection constructor
   void mark_bp_connection(Connection* conn) const {
      /// mark an connection as a bp connection if it connects to an address in the bp peer list, so that the connection
      /// won't be subject to the limit of max_client_count.

      if (auto itr = bp_peer_accounts.find(conn->log_p2p_address); itr != bp_peer_accounts.end()) {
         conn->is_bp_connection = true;
      }
   }

    // Only called from connection strand
   template <typename Conn>
   static bool established_client_connection(Conn&& conn) {
      return !conn->is_bp_connection && conn->socket_is_open() && conn->incoming_and_handshake_received();
   }

    // Only called from connection strand
   std::size_t num_established_clients() const {
      uint32_t num_clients = 0;
      self()->for_each_connection([&num_clients](auto&& conn) {
         if (established_client_connection(conn)) {
            ++num_clients;
         }
         return true;
      });
      return num_clients;
   }

   // Only called from connection strand
   // This should only be called after the first handshake message is received to check if an incoming connection
   // has exceeded the pre-configured max_client_count limit.
   bool exceeding_connection_limit(Connection* new_connection) const {
      return auto_bp_peering_enabled() && self()->max_client_count != 0 &&
             established_client_connection(new_connection) && num_established_clients() > self()->max_client_count;
   }


   // Only called from main thread
   neighbor_finder_type neighbor_finder(const std::vector<chain::producer_authority>& schedule) const {
      return neighbor_finder_type(bp_peer_addresses, my_accounts, schedule);
   }

   // Only called from main thread
   void on_pending_schedule(const chain::producer_authority_schedule& schedule) {
      if (auto_bp_peering_enabled() && self()->in_sync()) {
         if (schedule.producers.size()) {
            if (pending_schedule_version != schedule.version) {
               /// establish connection to the BPs within our pending scheduling proximity

               fc_dlog(self()->get_logger(), "pending producer schedule switches from version ${old} to ${new}",
                       ("old", pending_schedule_version)("new", schedule.version));

               auto finder                       = neighbor_finder(schedule.producers);
               auto pending_downstream_neighbors = finder.downstream_neighbors();

               fc_dlog(self()->get_logger(), "pending_downstream_neighbors: ${pending_downstream_neighbors}",
                       ("pending_downstream_neighbors", to_string(pending_downstream_neighbors)));
               for (auto neighbor : pending_downstream_neighbors) { self()->connect(bp_peer_addresses[neighbor]); }

               pending_neighbors = pending_downstream_neighbors;
               finder.add_upstream_neighbors(pending_neighbors);

               pending_schedule_version = schedule.version;
            }
         } else {
            fc_dlog(self()->get_logger(), "pending producer schedule version ${v} has being cleared",
                    ("v", schedule.version));
            pending_neighbors.clear();
         }
      }
   }

   // Only called from main thread
   void on_active_schedule(const chain::producer_authority_schedule& schedule) {
      if (auto_bp_peering_enabled() && active_schedule_version != schedule.version && self()->in_sync()) {
         /// drops any BP connection which is no longer within our scheduling proximity

         fc_dlog(self()->get_logger(), "active producer schedule switches from version ${old} to ${new}",
                 ("old", active_schedule_version)("new", schedule.version));

         auto old_neighbors = active_neighbors;
         active_neighbors   = neighbor_finder(schedule.producers).neighbors();

         fc_dlog(self()->get_logger(), "active_neighbors: ${active_neighbors}",
                 ("active_neighbors", to_string(active_neighbors)));

         flat_set<account_name> peers_to_stay;
         std::set_union(active_neighbors.begin(), active_neighbors.end(), pending_neighbors.begin(),
                        pending_neighbors.end(), std::inserter(peers_to_stay, peers_to_stay.begin()));

         fc_dlog(self()->get_logger(), "peers_to_stay: ${peers_to_stay}", ("peers_to_stay", to_string(peers_to_stay)));

         std::vector<account_name> peers_to_drop;
         std::set_difference(old_neighbors.begin(), old_neighbors.end(), peers_to_stay.begin(), peers_to_stay.end(),
                             std::back_inserter(peers_to_drop));
         fc_dlog(self()->get_logger(), "peers to drop: ${peers_to_drop}", ("peers_to_drop", to_string(peers_to_drop)));

         for (auto account : peers_to_drop) { self()->disconnect(bp_peer_addresses[account]); }
         active_schedule_version = schedule.version;
      }
   }
};
} // namespace eosio::auto_bp_peering
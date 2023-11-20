#include <boost/test/unit_test.hpp>
#include <eosio/net_plugin/auto_bp_peering.hpp>

struct mock_connection {
   bool is_bp_connection   = false;
   bool is_open            = false;
   bool handshake_received = false;
   mock_connection(bool bp_connection, bool open, bool received)
     : is_bp_connection(bp_connection)
     , is_open(open)
     , handshake_received(received)
   {}

   bool socket_is_open() const { return is_open; }
   bool incoming_and_handshake_received() const { return handshake_received; }
};

using namespace eosio::chain::literals;
using namespace std::literals::string_literals;

struct mock_connections_manager {
   uint32_t                     max_client_count = 0;
   std::vector<std::shared_ptr<mock_connection>> connections;

   std::function<void(std::string, std::string)> resolve_and_connect;
   std::function<void(std::string)> disconnect;

   uint32_t get_max_client_count() const { return max_client_count; }

   template <typename Function>
   void for_each_connection(Function&& func) const {
      for (auto c : connections) {
         if (!func(c))
            return;
      }
   }
};

struct mock_net_plugin : eosio::auto_bp_peering::bp_connection_manager<mock_net_plugin, mock_connection> {

   bool                         is_in_sync = false;
   mock_connections_manager     connections;
   std::vector<std::string>     p2p_addresses{"0.0.0.0:9876"};
   const std::string&           get_first_p2p_address() const { return *p2p_addresses.begin(); }

   bool in_sync() { return is_in_sync; }

   void setup_test_peers() {
      set_bp_peers({ "proda,127.0.0.1:8001:blk"s, "prodb,127.0.0.1:8002:trx"s, "prodc,127.0.0.1:8003"s,
                     "prodd,127.0.0.1:8004"s, "prode,127.0.0.1:8005"s, "prodf,127.0.0.1:8006"s, "prodg,127.0.0.1:8007"s,
                     "prodh,127.0.0.1:8008"s, "prodi,127.0.0.1:8009"s, "prodj,127.0.0.1:8010"s,
                     // prodk is intentionally skipped
                     "prodl,127.0.0.1:8012"s, "prodm,127.0.0.1:8013"s, "prodn,127.0.0.1:8014"s, "prodo,127.0.0.1:8015"s,
                     "prodp,127.0.0.1:8016"s, "prodq,127.0.0.1:8017"s, "prodr,127.0.0.1:8018"s, "prods,127.0.0.1:8019"s,
                     "prodt,127.0.0.1:8020"s, "produ,127.0.0.1:8021"s });
   }

   fc::logger get_logger() { return fc::logger::get(DEFAULT_LOGGER); }
};

BOOST_AUTO_TEST_CASE(test_set_bp_peers) {

   mock_net_plugin plugin;
   BOOST_CHECK_THROW(plugin.set_bp_peers({ "producer17,127.0.0.1:8888"s }), eosio::chain::plugin_config_exception);
   BOOST_CHECK_THROW(plugin.set_bp_peers({ "producer1"s }), eosio::chain::plugin_config_exception);

   plugin.set_bp_peers({
         "producer1,127.0.0.1:8888:blk"s,
         "producer2,127.0.0.1:8889:trx"s,
         "producer3,127.0.0.1:8890"s,
         "producer4,127.0.0.1:8891"s
   });

   BOOST_CHECK_EQUAL(plugin.config.bp_peer_addresses["producer1"_n], "127.0.0.1:8888:blk"s);
   BOOST_CHECK_EQUAL(plugin.config.bp_peer_addresses["producer2"_n], "127.0.0.1:8889:trx"s);
   BOOST_CHECK_EQUAL(plugin.config.bp_peer_addresses["producer3"_n], "127.0.0.1:8890"s);
   BOOST_CHECK_EQUAL(plugin.config.bp_peer_addresses["producer4"_n], "127.0.0.1:8891"s);

   BOOST_CHECK_EQUAL(plugin.config.bp_peer_accounts["127.0.0.1:8888:blk"], "producer1"_n);
   BOOST_CHECK_EQUAL(plugin.config.bp_peer_accounts["127.0.0.1:8889:trx"], "producer2"_n);
   BOOST_CHECK_EQUAL(plugin.config.bp_peer_accounts["127.0.0.1:8890"], "producer3"_n);
   BOOST_CHECK_EQUAL(plugin.config.bp_peer_accounts["127.0.0.1:8891"], "producer4"_n);
}

bool operator==(const fc::flat_set<eosio::chain::account_name>& a, const fc::flat_set<eosio::chain::account_name>& b) {
   return std::equal(a.begin(), a.end(), b.begin(), b.end());
}

bool operator==(const std::vector<std::string>& a, const std::vector<std::string>& b) {
   return std::equal(a.begin(), a.end(), b.begin(), b.end());
}

namespace boost::container {
std::ostream& boost_test_print_type(std::ostream& os, const flat_set<eosio::chain::account_name>& accounts) {
   os << "{";
   const char* sep = "";
   for (auto e : accounts) {
      os << sep << e.to_string();
      sep = ", ";
   }
   os << "}";
   return os;
}
} // namespace boost::container

namespace std {
std::ostream& boost_test_print_type(std::ostream& os, const std::vector<std::string>& content) {
   os << "{";
   const char* sep = "";
   for (auto e : content) {
      os << sep << e;
      sep = ", ";
   }
   os << "}";
   return os;
}
} // namespace std

const eosio::chain::producer_authority_schedule test_schedule1{
   1,
   { { "proda"_n, {} }, { "prodb"_n, {} }, { "prodc"_n, {} }, { "prodd"_n, {} }, { "prode"_n, {} }, { "prodf"_n, {} },
     { "prodg"_n, {} }, { "prodh"_n, {} }, { "prodi"_n, {} }, { "prodj"_n, {} }, { "prodk"_n, {} }, { "prodl"_n, {} },
     { "prodm"_n, {} }, { "prodn"_n, {} }, { "prodo"_n, {} }, { "prodp"_n, {} }, { "prodq"_n, {} }, { "prodr"_n, {} },
     { "prods"_n, {} }, { "prodt"_n, {} }, { "produ"_n, {} } }
};

const eosio::chain::producer_authority_schedule test_schedule2{
   2,
   { { "proda"_n, {} }, { "prode"_n, {} }, { "prodi"_n, {} }, { "prodm"_n, {} }, { "prodp"_n, {} }, { "prods"_n, {} },
     { "prodb"_n, {} }, { "prodf"_n, {} }, { "prodj"_n, {} }, { "prodn"_n, {} }, { "prodq"_n, {} }, { "prodt"_n, {} },
     { "prodc"_n, {} }, { "prodg"_n, {} }, { "prodk"_n, {} }, { "prodo"_n, {} }, { "prodr"_n, {} }, { "produ"_n, {} },
     { "prodd"_n, {} }, { "prodh"_n, {} }, { "prodl"_n, {} } }
};

const eosio::chain::producer_authority_schedule reset_schedule1{ 1, {} };

BOOST_AUTO_TEST_CASE(test_neighbor_finder) {

   {
      mock_net_plugin plugin;
      plugin.setup_test_peers();

      plugin.config.my_bp_accounts = { "prodd"_n, "produ"_n };
      BOOST_CHECK_EQUAL(plugin.neighbor_finder(test_schedule1.producers).downstream_neighbors(),
                        (fc::flat_set<eosio::chain::account_name>{ "proda"_n, "prodb"_n, "prode"_n, "prodf"_n }));

      BOOST_CHECK_EQUAL(plugin.neighbor_finder(test_schedule1.producers).neighbors(),
                        (fc::flat_set<eosio::chain::account_name>{ "proda"_n, "prodb"_n, "prodc"_n, "prode"_n,
                                                                   "prodf"_n, "prods"_n, "prodt"_n }));
   }
   {
      mock_net_plugin plugin;
      plugin.setup_test_peers();

      plugin.config.my_bp_accounts = { "prodj"_n };
      // make sure it doesn't return any producer not on the bp peer list
      BOOST_CHECK_EQUAL(plugin.neighbor_finder(test_schedule1.producers).downstream_neighbors(),
                        (fc::flat_set<eosio::chain::account_name>{ "prodl"_n }));

      BOOST_CHECK_EQUAL(plugin.neighbor_finder(test_schedule1.producers).neighbors(),
                        (fc::flat_set<eosio::chain::account_name>{ "prodh"_n, "prodi"_n, "prodl"_n }));
   }
}

BOOST_AUTO_TEST_CASE(test_on_pending_schedule) {

   mock_net_plugin plugin;
   plugin.setup_test_peers();
   plugin.config.my_bp_accounts = { "prodd"_n, "produ"_n };
   plugin.pending_neighbors     = { "prodj"_n, "prodm"_n };

   std::vector<std::string> connected_hosts;

   plugin.connections.resolve_and_connect = [&connected_hosts](std::string host, std::string p2p_address) { connected_hosts.push_back(host); };

   // make sure nothing happens when it is not in_sync
   plugin.is_in_sync = false;
   plugin.on_pending_schedule(test_schedule1);

   BOOST_CHECK_EQUAL(connected_hosts, (std::vector<std::string>{}));
   BOOST_CHECK_EQUAL(plugin.pending_neighbors, (fc::flat_set<eosio::chain::account_name>{ "prodj"_n, "prodm"_n }));
   BOOST_CHECK_EQUAL(plugin.pending_schedule_version, 0u);

   // when it is in sync and on_pending_schedule is called
   plugin.is_in_sync = true;
   plugin.on_pending_schedule(test_schedule1);

   // the downstream neighbors
   BOOST_CHECK_EQUAL(plugin.pending_neighbors,
                     (fc::flat_set<eosio::chain::account_name>{ "proda"_n, "prodb"_n, "prodc"_n, "prode"_n, "prodf"_n,
                                                                "prods"_n, "prodt"_n }));

   // all connect to downstream bp peers should be invoked
   BOOST_CHECK_EQUAL(connected_hosts, (std::vector<std::string>{ "127.0.0.1:8001:blk"s, "127.0.0.1:8002:trx"s,
                                                                 "127.0.0.1:8005"s, "127.0.0.1:8006"s }));

   BOOST_CHECK_EQUAL(plugin.pending_schedule_version, 1u);

   // make sure we don't change the active_schedule_version
   BOOST_CHECK_EQUAL(plugin.active_schedule_version, 0u);

   // Let's call on_pending_schedule() again, and connect shouldn't be called again
   connected_hosts.clear();
   plugin.on_pending_schedule(test_schedule1);
   BOOST_CHECK_EQUAL(connected_hosts, (std::vector<std::string>{}));

   plugin.on_pending_schedule(reset_schedule1);
   BOOST_CHECK_EQUAL(plugin.pending_neighbors, (fc::flat_set<eosio::chain::account_name>{}));
}

BOOST_AUTO_TEST_CASE(test_on_active_schedule1) {

   mock_net_plugin plugin;
   plugin.setup_test_peers();
   plugin.config.my_bp_accounts = { "prodd"_n, "produ"_n };

   plugin.active_neighbors = { "proda"_n, "prodh"_n, "prodn"_n };
   plugin.connections.resolve_and_connect = [](std::string host, std::string p2p_address) {};

   std::vector<std::string> disconnected_hosts;
   plugin.connections.disconnect = [&disconnected_hosts](std::string host) { disconnected_hosts.push_back(host); };

   // make sure nothing happens when it is not in_sync
   plugin.is_in_sync = false;
   plugin.on_active_schedule(test_schedule1);

   BOOST_CHECK_EQUAL(disconnected_hosts, (std::vector<std::string>{}));
   BOOST_CHECK_EQUAL(plugin.active_neighbors,
                     (fc::flat_set<eosio::chain::account_name>{ "proda"_n, "prodh"_n, "prodn"_n }));
   BOOST_CHECK_EQUAL(plugin.active_schedule_version, 0u);

   // when it is in sync and on_active_schedule is called
   plugin.is_in_sync = true;
   plugin.on_pending_schedule(test_schedule1);
   plugin.on_active_schedule(test_schedule1);
   // then disconnect to prodh and prodn should be invoked
   BOOST_CHECK_EQUAL(disconnected_hosts, (std::vector<std::string>{ "127.0.0.1:8008"s, "127.0.0.1:8014"s }));

   BOOST_CHECK_EQUAL(plugin.active_neighbors,
                     (fc::flat_set<eosio::chain::account_name>{ "proda"_n, "prodb"_n, "prodc"_n, "prode"_n, "prodf"_n,
                                                                "prods"_n, "prodt"_n }));

   // make sure we change the active_schedule_version
   BOOST_CHECK_EQUAL(plugin.active_schedule_version, 1u);
}

BOOST_AUTO_TEST_CASE(test_on_active_schedule2) {

   mock_net_plugin plugin;
   plugin.setup_test_peers();
   plugin.config.my_bp_accounts = { "prodd"_n, "produ"_n };

   plugin.active_neighbors = { "proda"_n, "prodh"_n, "prodn"_n };
   plugin.connections.resolve_and_connect = [](std::string host, std::string p2p_address) {};
   std::vector<std::string> disconnected_hosts;
   plugin.connections.disconnect = [&disconnected_hosts](std::string host) { disconnected_hosts.push_back(host); };

   // when pending and active schedules are changed simultaneosly
   plugin.is_in_sync = true;
   plugin.on_pending_schedule(test_schedule2);
   plugin.on_active_schedule(test_schedule1);
   // then disconnect to  prodn should be invoked while prodh shouldn't, because prodh is in the
   // pending_neighbors
   BOOST_CHECK_EQUAL(disconnected_hosts, (std::vector<std::string>{ "127.0.0.1:8014"s }));

   BOOST_CHECK_EQUAL(plugin.active_neighbors,
                     (fc::flat_set<eosio::chain::account_name>{ "proda"_n, "prodb"_n, "prodc"_n, "prode"_n, "prodf"_n,
                                                                "prods"_n, "prodt"_n }));

   // make sure we change the active_schedule_version
   BOOST_CHECK_EQUAL(plugin.active_schedule_version, 1u);
}

BOOST_AUTO_TEST_CASE(test_exceeding_connection_limit) {
   mock_net_plugin plugin;
   plugin.setup_test_peers();
   plugin.config.my_bp_accounts = { "prodd"_n, "produ"_n };
   plugin.connections.max_client_count = 1;
   plugin.connections.connections = {
      std::make_shared<mock_connection>( true, true, true ),   // 0
      std::make_shared<mock_connection>( true, true, false ),  // 1
      std::make_shared<mock_connection>( true, false, true ),  // 2
      std::make_shared<mock_connection>( true, false, false ), // 3
      std::make_shared<mock_connection>( false, true, true ),  // 4
      std::make_shared<mock_connection>( false, true, false ), // 5
      std::make_shared<mock_connection>( false, true, true ),  // 6
      std::make_shared<mock_connection>( false, false, false ) // 7
   };

   BOOST_CHECK_EQUAL(plugin.num_established_clients(), 2u);

   BOOST_CHECK(!plugin.exceeding_connection_limit(plugin.connections.connections[0]));
   BOOST_CHECK(!plugin.exceeding_connection_limit(plugin.connections.connections[1]));
   BOOST_CHECK(!plugin.exceeding_connection_limit(plugin.connections.connections[2]));
   BOOST_CHECK(!plugin.exceeding_connection_limit(plugin.connections.connections[3]));
   BOOST_CHECK(plugin.exceeding_connection_limit(plugin.connections.connections[4]));
   BOOST_CHECK(!plugin.exceeding_connection_limit(plugin.connections.connections[5]));
   BOOST_CHECK(plugin.exceeding_connection_limit(plugin.connections.connections[6]));
   BOOST_CHECK(!plugin.exceeding_connection_limit(plugin.connections.connections[7]));
}

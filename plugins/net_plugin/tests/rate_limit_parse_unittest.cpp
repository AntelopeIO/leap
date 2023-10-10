#define BOOST_TEST_MODULE rate_limit_parsing
#include <boost/test/included/unit_test.hpp>
#include "../net_plugin.cpp"

BOOST_AUTO_TEST_CASE(test_parse_rate_limit) {
   eosio::net_plugin_impl plugin_impl;
   std::vector<std::string> p2p_addresses = {
        "0.0.0.0:9876"
      , "0.0.0.0:9776:0"
      , "0.0.0.0:9877:640KB/s"
      , "192.168.0.1:9878:20MiB/s"
      , "localhost:9879:0.5KB/s"
      , "[2001:db8:85a3:8d3:1319:8a2e:370:7348]:9876:250KB/s"
      , "[::1]:9876:250KB/s"
      , "2001:db8:85a3:8d3:1319:8a2e:370:7348:9876:250KB/s"
      , "[::1]:9876:-250KB/s"
      , "0.0.0.0:9877:640Kb/s"
      , "0.0.0.0:9877:999999999999999999999999999TiB/s"
   };
   size_t which = 0;
   auto [listen_addr, block_sync_rate_limit] = plugin_impl.parse_listen_address(p2p_addresses[which++]);
   BOOST_CHECK_EQUAL(listen_addr, "0.0.0.0:9876");
   BOOST_CHECK_EQUAL(block_sync_rate_limit, 0);
   std::tie(listen_addr, block_sync_rate_limit) = plugin_impl.parse_listen_address(p2p_addresses[which++]);
   BOOST_CHECK_EQUAL(listen_addr, "0.0.0.0:9776");
   BOOST_CHECK_EQUAL(block_sync_rate_limit, 0);
   std::tie(listen_addr, block_sync_rate_limit) = plugin_impl.parse_listen_address(p2p_addresses[which++]);
   BOOST_CHECK_EQUAL(listen_addr, "0.0.0.0:9877");
   BOOST_CHECK_EQUAL(block_sync_rate_limit, 640000);
   std::tie(listen_addr, block_sync_rate_limit) = plugin_impl.parse_listen_address(p2p_addresses[which++]);
   BOOST_CHECK_EQUAL(listen_addr, "192.168.0.1:9878");
   BOOST_CHECK_EQUAL(block_sync_rate_limit, 20971520);
   std::tie(listen_addr, block_sync_rate_limit) = plugin_impl.parse_listen_address(p2p_addresses[which++]);
   BOOST_CHECK_EQUAL(listen_addr, "localhost:9879");
   BOOST_CHECK_EQUAL(block_sync_rate_limit, 500);
   std::tie(listen_addr, block_sync_rate_limit) = plugin_impl.parse_listen_address(p2p_addresses[which++]);
   BOOST_CHECK_EQUAL(listen_addr, "[2001:db8:85a3:8d3:1319:8a2e:370:7348]:9876");
   BOOST_CHECK_EQUAL(block_sync_rate_limit, 250000);
   std::tie(listen_addr, block_sync_rate_limit) = plugin_impl.parse_listen_address(p2p_addresses[which++]);
   BOOST_CHECK_EQUAL(listen_addr, "[::1]:9876");
   BOOST_CHECK_EQUAL(block_sync_rate_limit, 250000);
   BOOST_CHECK_EXCEPTION(plugin_impl.parse_listen_address(p2p_addresses[which++]), eosio::chain::plugin_config_exception,
                         [](const eosio::chain::plugin_config_exception& e)
                         {return std::strstr(e.top_message().c_str(), "IPv6 addresses must be enclosed in square brackets");});
   BOOST_CHECK_EXCEPTION(plugin_impl.parse_listen_address(p2p_addresses[which++]), eosio::chain::plugin_config_exception,
                         [](const eosio::chain::plugin_config_exception& e)
                         {return std::strstr(e.top_message().c_str(), "block sync rate limit must not be negative");});
   BOOST_CHECK_EXCEPTION(plugin_impl.parse_listen_address(p2p_addresses[which++]), eosio::chain::plugin_config_exception,
                         [](const eosio::chain::plugin_config_exception& e)
                         {return std::strstr(e.top_message().c_str(), "invalid block sync rate limit specification");});
   BOOST_CHECK_EXCEPTION(plugin_impl.parse_listen_address(p2p_addresses[which++]), eosio::chain::plugin_config_exception,
                         [](const eosio::chain::plugin_config_exception& e)
                         {return std::strstr(e.top_message().c_str(), "block sync rate limit specification overflowed");});
}

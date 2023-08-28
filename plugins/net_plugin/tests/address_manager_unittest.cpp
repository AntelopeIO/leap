#define BOOST_TEST_MODULE address_manager_unittest

#include <boost/test/included/unit_test.hpp>
#include <eosio/net_plugin/protocol.hpp>
#include <eosio/net_plugin/address_manager.hpp>

#include <thread>
#include <random>


using namespace eosio;

peer_address address = peer_address::from_str("127.0.0.1:1234:all");

std::unordered_set<std::string> gen_addresses(const std::string &host, uint32_t count) {
   std::random_device rd;
   std::mt19937 gen(rd());
   std::uniform_int_distribution<> distrib(1, 65535);
   std::unordered_set<std::string> addresses;

   for (int i = 0; i < count; ++i) {
      int port = distrib(gen);
      std::string address = host + ":" + std::to_string(port);
      addresses.insert(address);
   }
   return addresses;
}


BOOST_AUTO_TEST_SUITE(test_peer_address)

   BOOST_AUTO_TEST_CASE(CheckEnumValues) {
      BOOST_CHECK_EQUAL(static_cast<int>(address_type_enum::blk), 1);
      BOOST_CHECK_EQUAL(static_cast<int>(address_type_enum::trx), 2);
      BOOST_CHECK_EQUAL(static_cast<int>(address_type_enum::peer), 4);
      BOOST_CHECK_EQUAL(static_cast<int>(address_type_enum::both), 3);  // 1 | 2
      BOOST_CHECK_EQUAL(static_cast<int>(address_type_enum::bnp), 5);   // 1 | 4
      BOOST_CHECK_EQUAL(static_cast<int>(address_type_enum::tnp), 6);   // 2 | 4
      BOOST_CHECK_EQUAL(static_cast<int>(address_type_enum::all), 7);   // 1 | 2 | 4

      int addressType = static_cast<int>(address_type_enum::bnp);

      BOOST_REQUIRE(addressType & static_cast<int>(address_type_enum::blk));
      BOOST_REQUIRE(addressType & static_cast<int>(address_type_enum::peer));
      BOOST_REQUIRE(!(addressType & static_cast<int>(address_type_enum::trx)));
   }

   BOOST_AUTO_TEST_CASE(TestMinusOperator) {
      address_type_enum all = address_type_enum::all;
      address_type_enum bnp = address_type_enum::bnp;
      address_type_enum blk = address_type_enum::blk;
      address_type_enum tnp = address_type_enum::tnp;


      address_type_enum result = bnp - blk;
      address_type_enum result2 = tnp - blk;
      address_type_enum result3 = all - tnp;
      address_type_enum result4 = blk - blk;

      BOOST_REQUIRE(result == address_type_enum::peer);
      BOOST_REQUIRE(result2 == address_type_enum::tnp);
      BOOST_REQUIRE(result3 == address_type_enum::blk);
      BOOST_REQUIRE(result4 == address_type_enum::unknown);
   }

   BOOST_AUTO_TEST_CASE(TestAddOperator) {
      address_type_enum blk = address_type_enum::blk;
      address_type_enum trx = address_type_enum::trx;
      address_type_enum peer = address_type_enum::peer;

      address_type_enum result = blk + trx;
      address_type_enum result2 = blk + peer;
      address_type_enum result3 = trx + peer;
      address_type_enum result4 = blk + trx + peer;
      address_type_enum result5 = blk + blk;

      BOOST_REQUIRE(result == address_type_enum::both);
      BOOST_REQUIRE(result2 == address_type_enum::bnp);
      BOOST_REQUIRE(result3 == address_type_enum::tnp);
      BOOST_REQUIRE(result4 == address_type_enum::all);
      BOOST_REQUIRE(result5 == address_type_enum::blk);
   }

   BOOST_AUTO_TEST_CASE(TestContains) {
      address_type_enum blk = address_type_enum::blk;
      address_type_enum trx = address_type_enum::trx;
      address_type_enum peer = address_type_enum::peer;
      address_type_enum both = address_type_enum::both;
      address_type_enum tnp = address_type_enum::tnp;
      address_type_enum all = address_type_enum::all;

      BOOST_REQUIRE(address_type_contains(both, trx));
      BOOST_REQUIRE(!address_type_contains(both, peer));
      BOOST_REQUIRE(address_type_contains(all, tnp));
      BOOST_REQUIRE(address_type_contains(all, both));
      BOOST_REQUIRE(!address_type_contains(tnp, blk));
   }

   BOOST_AUTO_TEST_CASE(TestContainsOnly) {
      address_type_enum blk = address_type_enum::blk;
      address_type_enum trx = address_type_enum::trx;
      address_type_enum peer = address_type_enum::peer;
      address_type_enum both = address_type_enum::both;
      address_type_enum tnp = address_type_enum::tnp;
      address_type_enum all = address_type_enum::all;

      BOOST_REQUIRE(address_type_contains_only(trx, trx));
      BOOST_REQUIRE(!address_type_contains_only(both, peer));
      BOOST_REQUIRE(!address_type_contains_only(all, peer));
      BOOST_REQUIRE(!address_type_contains_only(all, both));
      BOOST_REQUIRE(address_type_contains_only(tnp, tnp));
      BOOST_REQUIRE(!address_type_contains_only(tnp, blk));
      BOOST_REQUIRE(address_type_contains_only(all, all));
   }

   BOOST_AUTO_TEST_CASE(test_from_str) {

      fc::time_point current_time = fc::time_point::now();

      peer_address address1 = peer_address::from_str("127.0.0.1:1234:all");
      peer_address address2 = peer_address::from_str("example.com:80:both");
      peer_address address3 = peer_address::from_str("eosproducer1,127.0.0.1:1234:trx");
      peer_address address4 = peer_address::from_str("127.0.0.1:1234:blk - 012345");
      peer_address address5 = peer_address::from_str("127.0.0.1:1234:peer:012345");
      peer_address address6 = peer_address::from_str("127.0.0.1:1234:all", false);
      peer_address address7 = peer_address::from_str("127.0.0.1:1234:all", true);

      peer_address address8 = peer_address::from_str("127.0.0.1:1234");
      peer_address address9 = peer_address::from_str("eosproducer1,127.0.0.1:1234");
      peer_address address10 = peer_address::from_str("127.0.0.1:1234 - 012345");

      peer_address address11 = peer_address::from_str("host1:100 - 012345");

      peer_address address12 = peer_address::from_str("");

      BOOST_REQUIRE(address1.host == "127.0.0.1");
      BOOST_REQUIRE(address1.port == "1234");
      BOOST_REQUIRE(address1.address_type == str_to_address_type("all"));
      BOOST_REQUIRE(address1.receive >= current_time);
      BOOST_REQUIRE(address1.last_active == fc::time_point::min());
      BOOST_REQUIRE(address1.manual == false);
      BOOST_REQUIRE(address2.host == "example.com");
      BOOST_REQUIRE(address2.port == "80");
      // if type not recognized, use all as default
      BOOST_REQUIRE(address2.address_type == str_to_address_type("all"));
      BOOST_REQUIRE(address3.host == "127.0.0.1");
      BOOST_REQUIRE(address3.port == "1234");
      BOOST_REQUIRE(address3.address_type == str_to_address_type("trx"));
      BOOST_REQUIRE(address4.host == "127.0.0.1");
      BOOST_REQUIRE(address4.port == "1234");
      BOOST_REQUIRE(address4.address_type == str_to_address_type("blk"));
      BOOST_REQUIRE(address5.host == "127.0.0.1");
      BOOST_REQUIRE(address5.port == "1234");
      BOOST_REQUIRE(address5.address_type == str_to_address_type("peer"));

      BOOST_REQUIRE(address6.manual == false);
      BOOST_REQUIRE(address7.manual == true);

      BOOST_REQUIRE(address8.address_type == str_to_address_type(""));
      BOOST_REQUIRE(address9.address_type == str_to_address_type(""));
      BOOST_REQUIRE(address10.address_type == str_to_address_type(""));

      BOOST_REQUIRE(address11.host == "host1");

      BOOST_REQUIRE(address12.host == "");
      BOOST_REQUIRE(address12.port == "");
      BOOST_REQUIRE(address12.to_str() == "");

      BOOST_CHECK_EXCEPTION(peer_address::from_str("invalid_address_string"),
                            std::invalid_argument,
                            [](const std::invalid_argument &e) {
                               return std::string(e.what()) == "Invalid peer address string: invalid_address_string";
                            });

      BOOST_CHECK_EXCEPTION(peer_address::from_str("example.com"),
                            std::invalid_argument,
                            [](const std::invalid_argument &e) {
                               return std::string(e.what()) == "Invalid peer address string: example.com";
                            });

      BOOST_CHECK_EXCEPTION(peer_address::from_str(":80"),
                            std::invalid_argument,
                            [](const std::invalid_argument &e) {
                               return std::string(e.what()) == "Invalid peer address string: :80";
                            });

      BOOST_CHECK_EXCEPTION(peer_address::from_str("example.com:"),
                            std::invalid_argument,
                            [](const std::invalid_argument &e) {
                               return std::string(e.what()) == "Invalid peer address string: example.com:";
                            });

      BOOST_CHECK_EXCEPTION(peer_address::from_str("example.com:xxx"),
                            std::invalid_argument,
                            [](const std::invalid_argument &e) {
                               return std::string(e.what()) == "Invalid peer address string: port number xxx";
                            });
   }

   BOOST_AUTO_TEST_CASE(test_equal) {

      peer_address address1 = peer_address::from_str("127.0.0.1:1234:all");
      peer_address address2 = peer_address::from_str("127.0.0.1:1234");
      peer_address address3 = peer_address::from_str("eosproducer1,127.0.0.1:1234");
      peer_address address4 = peer_address::from_str("127.0.0.1:1234 - 012345");

      BOOST_REQUIRE(address == address1);
      BOOST_REQUIRE(address == address2);
      // address_type is ignored in == comparison
      BOOST_REQUIRE(address.address_type != address2.address_type);
      BOOST_REQUIRE(address == address3);
      BOOST_REQUIRE(address == address4);
   }

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(test_address_manager)

   BOOST_AUTO_TEST_CASE(test_add_address) {
      eosio::address_manager manager;

      peer_address address1 = peer_address::from_str("127.0.0.1:1234:all");
      peer_address address2 = peer_address::from_str("example.com:80:both");
      peer_address address3 = peer_address::from_str("eosproducer1,127.0.0.1:1234:trx");
      peer_address address4 = peer_address::from_str("127.0.0.1:1234:blk - 012345");

      manager.add_address(address1);
      manager.add_address(address2);
      manager.add_address(address3);
      manager.add_address(address4);

      std::unordered_set<std::string> addresses = manager.get_addresses();
      BOOST_REQUIRE(addresses.size() == 2);
      BOOST_REQUIRE(addresses.count("127.0.0.1:1234:all") == 1);
      BOOST_REQUIRE(addresses.count("example.com:80:all") == 1);
   }

   BOOST_AUTO_TEST_CASE(test_add_addresses)
   {
      address_manager manager;
      std::unordered_set<std::string> addresses_to_add{"192.168.0.1:9876:peer", "10.0.0.2:8888", "example.com:443:trx"};
      std::unordered_set<std::string> addresses_to_add2{"192.168.0.1:9877:peer", "10.0.0.2:8888", "example.com:444"};

      manager.add_addresses(addresses_to_add, false);
      manager.add_addresses(addresses_to_add2, true);

      std::unordered_set<string> retrieved_addresses = manager.get_addresses();
      BOOST_REQUIRE(retrieved_addresses.size() == 5);
      BOOST_REQUIRE(retrieved_addresses.count("192.168.0.1:9876:peer") == 1);
      BOOST_REQUIRE(retrieved_addresses.count("10.0.0.2:8888") == 1);
      BOOST_REQUIRE(retrieved_addresses.count("example.com:443:trx") == 1);
      BOOST_REQUIRE(retrieved_addresses.count("192.168.0.1:9877:peer") == 1);
      BOOST_REQUIRE(retrieved_addresses.count("example.com:444") == 1);
      BOOST_REQUIRE(manager.get_addresses_map()["192.168.0.1:9876"].manual == false);
      BOOST_REQUIRE(manager.get_addresses_map()["10.0.0.2:8888"].manual == false);
      BOOST_REQUIRE(manager.get_addresses_map()["example.com:443"].manual == false);
      BOOST_REQUIRE(manager.get_addresses_map()["192.168.0.1:9877"].manual == true);
      BOOST_REQUIRE(manager.get_addresses_map()["example.com:444"].manual == true);

   }

   BOOST_AUTO_TEST_CASE(test_update_address) {
      address_manager manager;

      peer_address address = peer_address::from_str("127.0.0.1:9876:trx");
      address.manual = true;
      fc::time_point time1 = fc::time_point::now();
      address.receive = time1;
      address.last_active = time1;

      manager.add_address(address);

      BOOST_REQUIRE(manager.get_addresses().size() == 1);
      BOOST_REQUIRE(manager.get_addresses_map()["127.0.0.1:9876"].address_type == address_type_enum::trx);
      BOOST_REQUIRE(manager.get_addresses_map()["127.0.0.1:9876"].receive == time1);
      BOOST_REQUIRE(manager.get_addresses_map()["127.0.0.1:9876"].last_active == time1);
      BOOST_REQUIRE(manager.get_addresses_map()["127.0.0.1:9876"].manual == true);


      peer_address new_address = peer_address::from_str("127.0.0.1:9876:peer");
      std::this_thread::sleep_for(std::chrono::milliseconds(1000));
      fc::time_point time2 = fc::time_point::now();
      new_address.receive = time2;
      new_address.last_active = time2;
      new_address.manual = false;

      manager.update_address(new_address);

      BOOST_REQUIRE(manager.get_addresses().size() == 1);
      BOOST_REQUIRE(manager.get_addresses_map()["127.0.0.1:9876"].address_type == address_type_enum::peer);
      BOOST_REQUIRE(manager.get_addresses_map()["127.0.0.1:9876"].receive == time2);
      BOOST_REQUIRE(manager.get_addresses_map()["127.0.0.1:9876"].last_active == time2);
      BOOST_REQUIRE(manager.get_addresses_map()["127.0.0.1:9876"].manual == false);


   }

   BOOST_AUTO_TEST_CASE(test_get_manual_addresses) {
      address_manager manager;
      std::unordered_set<std::string> addresses_to_add{"192.168.0.1:9876:peer", "10.0.0.2:8888", "example.com:443:trx"};
      std::unordered_set<std::string> addresses_to_add2{"192.168.0.1:9877:peer", "10.0.0.2:8888", "example.com:444"};

      manager.add_addresses(addresses_to_add, false);
      manager.add_addresses(addresses_to_add2, true);

      std::unordered_set<string> manual_addresses = manager.get_manual_addresses();
      BOOST_CHECK_EQUAL(manual_addresses.size(), 2);
      BOOST_REQUIRE(manual_addresses.count("192.168.0.1:9877:peer") == 1);
      BOOST_REQUIRE(manual_addresses.count("example.com:444") == 1);

   }

   BOOST_AUTO_TEST_CASE(test_get_diff_addresses) {
      address_manager manager;
      std::unordered_set<std::string> addresses_to_add{"192.168.0.1:9876:peer", "10.0.0.2:8888", "example.com:443:trx"};
      std::unordered_set<std::string> addresses_diff{"192.168.0.1:9877:peer", "10.0.0.2:8888", "example.com:444"};

      manager.add_addresses(addresses_to_add, false);

      std::unordered_set<string> diff_addresses = manager.get_diff_addresses(addresses_diff);

      BOOST_REQUIRE(diff_addresses.size() == 2);
      BOOST_REQUIRE(diff_addresses.count("192.168.0.1:9876:peer") == 1);
      BOOST_REQUIRE(diff_addresses.count("example.com:443:trx") == 1);

   }

   BOOST_AUTO_TEST_CASE(test_get_latest_active_addresses) {
      address_manager manager;
      std::unordered_set<std::string> addresses_to_add{"192.168.0.1:9876:peer", "10.0.0.2:8888", "example.com:443:trx"};
      std::unordered_set<std::string> addresses_to_add2{"192.168.0.1:9877:peer", "10.0.0.2:8888", "example.com:444"};

      manager.add_addresses(addresses_to_add, false);
      manager.add_addresses(addresses_to_add2, true);

      // set last_active for some addresses to more than 10 minutes ago
      auto old_last_active = fc::time_point::now() - fc::seconds(700);
      auto new_last_active = fc::time_point::now() - fc::seconds(500);


      peer_address pa1 = peer_address::from_str("192.168.0.1:9876:peer");
      pa1.last_active = new_last_active;
      manager.update_address(pa1);

      peer_address pa2 = peer_address::from_str("192.168.0.1:9877:peer");
      pa2.last_active = new_last_active;
      pa2.manual = true;
      manager.update_address(pa2);

      peer_address pa3 = peer_address::from_str("10.0.0.2:8888");
      pa3.last_active = old_last_active;
      manager.update_address(pa3);

      peer_address pa4 = peer_address::from_str("example.com:444");
      pa4.last_active = old_last_active;
      pa4.manual = true;
      manager.update_address(pa4);


      std::unordered_set<string> latest_active_addresses = manager.get_latest_active_addresses(
              std::chrono::seconds(600));

      BOOST_REQUIRE(latest_active_addresses.size() == 2);
      BOOST_REQUIRE(latest_active_addresses.count("192.168.0.1:9876:peer") == 1);
      BOOST_REQUIRE(latest_active_addresses.count("192.168.0.1:9877:peer") == 1);


      std::unordered_set<string> latest_active_addresses_manual = manager.get_latest_active_addresses(
              std::chrono::seconds(600), true);
      BOOST_REQUIRE(latest_active_addresses_manual.size() == 1);
      BOOST_REQUIRE(latest_active_addresses_manual.count("192.168.0.1:9877:peer") == 1);

      std::unordered_set<string> latest_active_addresses2 = manager.get_latest_active_addresses(
              std::chrono::seconds(800));

      BOOST_REQUIRE(latest_active_addresses2.size() == 4);
      BOOST_REQUIRE(latest_active_addresses2.count("192.168.0.1:9876:peer") == 1);
      BOOST_REQUIRE(latest_active_addresses2.count("192.168.0.1:9877:peer") == 1);
      BOOST_REQUIRE(latest_active_addresses2.count("10.0.0.2:8888") == 1);
      BOOST_REQUIRE(latest_active_addresses2.count("example.com:444") == 1);

      std::unordered_set<string> latest_active_addresses_manual2 = manager.get_latest_active_addresses(
              std::chrono::seconds(800), true);
      BOOST_REQUIRE(latest_active_addresses_manual2.size() == 2);
      BOOST_REQUIRE(latest_active_addresses_manual2.count("192.168.0.1:9877:peer") == 1);
      BOOST_REQUIRE(latest_active_addresses_manual2.count("example.com:444") == 1);


   }

BOOST_AUTO_TEST_SUITE_END()


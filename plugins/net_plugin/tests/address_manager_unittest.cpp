#define BOOST_TEST_MODULE address_manager_unittest
#include <boost/test/included/unit_test.hpp>
#include <eosio/net_plugin/protocol.hpp>
#include <eosio/net_plugin/address_manager.hpp>

#include <thread>
#include <random>


using namespace eosio;

peer_address address = peer_address::from_str("127.0.0.1:1234:all");

std::vector<string> gen_addresses(string host, uint32_t count) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(1, 65535);
    std::vector<string> addresses;

    for (int i = 0; i < count; ++i) {
        int port = distrib(gen);
        string address = host + ":" + std::to_string(port);
        addresses.push_back(address);
    }
    return addresses;
}

BOOST_AUTO_TEST_SUITE(test_peer_address)



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

        BOOST_CHECK_EXCEPTION(peer_address::from_str("invalid_address_string"),
                              std::invalid_argument,
                              [](const std::invalid_argument& e) {
                                  return std::string(e.what()) == "Invalid peer address string: invalid_address_string";
                              });

        BOOST_CHECK_EXCEPTION(peer_address::from_str("example.com"),
                              std::invalid_argument,
                              [](const std::invalid_argument& e) {
                                  return std::string(e.what()) == "Invalid peer address string: example.com";
                              });

        BOOST_CHECK_EXCEPTION(peer_address::from_str(":80"),
                              std::invalid_argument,
                              [](const std::invalid_argument& e) {
                                  return std::string(e.what()) == "Invalid peer address string: :80";
                              });

        BOOST_CHECK_EXCEPTION(peer_address::from_str("example.com:"),
                              std::invalid_argument,
                              [](const std::invalid_argument& e) {
                                  return std::string(e.what()) == "Invalid peer address string: example.com:";
                              });

        BOOST_CHECK_EXCEPTION(peer_address::from_str("example.com:xxx"),
                              std::invalid_argument,
                              [](const std::invalid_argument& e) {
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
        eosio::address_manager manager(60);

        peer_address address1 = peer_address::from_str("127.0.0.1:1234:all");
        peer_address address2 = peer_address::from_str("example.com:80:both");
        peer_address address3 = peer_address::from_str("eosproducer1,127.0.0.1:1234:trx");
        peer_address address4 = peer_address::from_str("127.0.0.1:1234:blk - 012345");

        manager.add_address(address1);
        manager.add_address(address2);
        manager.add_address(address3);
        manager.add_address(address4);

        std::vector<peer_address> addresses = manager.get_addresses();
        BOOST_REQUIRE(addresses.size() == 2);
        BOOST_REQUIRE(addresses[0].host  == "127.0.0.1");
        BOOST_REQUIRE(addresses[0].port == "1234");
        BOOST_REQUIRE(addresses[0].address_type == eosio::address_type_enum::all);
        BOOST_REQUIRE(addresses[1].host == "example.com");
        BOOST_REQUIRE(addresses[1].port == "80");
        BOOST_REQUIRE(addresses[1].address_type == eosio::address_type_enum::all);
    }

    BOOST_AUTO_TEST_CASE(test_add_addresses)
    {
        address_manager manager(60);
        std::vector<std::string> addresses_to_add{"192.168.0.1:9876:peer", "10.0.0.2:8888", "example.com:443:trx"};
        std::vector<std::string> addresses_to_add2{"192.168.0.1:9877:peer", "10.0.0.2:8888", "example.com:444"};

        manager.add_addresses(addresses_to_add, false);
        manager.add_addresses(addresses_to_add2, true);
        
        std::vector<peer_address> retrieved_addresses = manager.get_addresses();
        BOOST_REQUIRE(retrieved_addresses.size() == 5);
        BOOST_REQUIRE(retrieved_addresses[0].to_str() == "192.168.0.1:9876:peer");
        BOOST_REQUIRE(retrieved_addresses[1].to_str() == "10.0.0.2:8888");
        BOOST_REQUIRE(retrieved_addresses[2].to_str() == "example.com:443:trx");
        BOOST_REQUIRE(retrieved_addresses[3].to_str() == "192.168.0.1:9877:peer");
        BOOST_REQUIRE(retrieved_addresses[4].to_str() == "example.com:444");
        BOOST_REQUIRE(retrieved_addresses[0].manual == false);
        BOOST_REQUIRE(retrieved_addresses[1].manual == false);
        BOOST_REQUIRE(retrieved_addresses[2].manual == false);
        BOOST_REQUIRE(retrieved_addresses[3].manual == true);
        BOOST_REQUIRE(retrieved_addresses[4].manual == true);

    }

    BOOST_AUTO_TEST_CASE(test_update_address) {
        address_manager manager(60);

        peer_address address = peer_address::from_str("127.0.0.1:9876:trx");
        address.manual = true;
        fc::time_point time1 = fc::time_point::now();
        address.receive = time1;
        address.last_active = time1;

        manager.add_address(address);

        BOOST_REQUIRE(manager.get_addresses().size() == 1);
        BOOST_REQUIRE(manager.get_addresses()[0].address_type == address_type_enum::trx);
        BOOST_REQUIRE(manager.get_addresses()[0].receive == time1);
        BOOST_REQUIRE(manager.get_addresses()[0].last_active == time1);
        BOOST_REQUIRE(manager.get_addresses()[0].manual == true);


        peer_address new_address = peer_address::from_str("127.0.0.1:9876:peer");
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        fc::time_point time2 = fc::time_point::now();
        new_address.receive = time2;
        new_address.last_active = time2;
        new_address.manual = false;

        manager.update_address(new_address);

        BOOST_REQUIRE(manager.get_addresses().size() == 1);
        BOOST_REQUIRE(manager.get_addresses()[0].address_type == address_type_enum::peer);
        BOOST_REQUIRE(manager.get_addresses()[0].receive == time2);
        BOOST_REQUIRE(manager.get_addresses()[0].last_active == time2);
        BOOST_REQUIRE(manager.get_addresses()[0].manual == false);


    }

    BOOST_AUTO_TEST_CASE(test_get_manual_addresses) {
        address_manager manager(60);
        std::vector<std::string> addresses_to_add{"192.168.0.1:9876:peer", "10.0.0.2:8888", "example.com:443:trx"};
        std::vector<std::string> addresses_to_add2{"192.168.0.1:9877:peer", "10.0.0.2:8888", "example.com:444"};

        manager.add_addresses(addresses_to_add, false);
        manager.add_addresses(addresses_to_add2, true);

        std::vector<peer_address> manual_addresses = manager.get_manual_addresses();
        BOOST_CHECK_EQUAL(manual_addresses.size(), 2);
        for (const auto& address : manual_addresses) {
            BOOST_CHECK(address.manual);
        }
    }

    BOOST_AUTO_TEST_CASE(test_get_diff_addresses) {
        address_manager manager(60);
        std::vector<std::string> addresses_to_add{"192.168.0.1:9876:peer", "10.0.0.2:8888", "example.com:443:trx"};
        std::vector<std::string> addresses_diff{"192.168.0.1:9877:peer", "10.0.0.2:8888", "example.com:444"};

        manager.add_addresses(addresses_to_add, false);

        std::vector<peer_address> diff_addresses = manager.get_diff_addresses(addresses_diff);

        BOOST_REQUIRE(diff_addresses.size() == 2);
        BOOST_REQUIRE(diff_addresses[0].to_str() == "192.168.0.1:9876:peer");
        BOOST_REQUIRE(diff_addresses[1].to_str() == "example.com:443:trx");

    }

    BOOST_AUTO_TEST_CASE(test_address_manager_concurrency) {

        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> distrib(1, 65535);

        address_manager manager1(60);
        address_manager manager2(60);
        address_manager manager3(60);
        address_manager manager4(60);
        address_manager manager5(60);
        address_manager manager6(60);

        uint32_t exist_address_count = 30000;
        uint32_t add_threads_count = 1000;
        uint32_t remove_threads_count = 300;
        uint32_t add_address_count = 10;
        uint32_t remove_address_count = 10;

        // Create a vector of peer_address objects
        std::vector<std::string> exist_addresses = gen_addresses("127.0.0.1",exist_address_count);

        //init addresses to add, add_threads_count * add_address_count
        std::vector<std::vector<std::string>> all_add_addresses;
        for (int i = 0; i < add_threads_count; ++i) {
            std::vector<std::string> addresses = gen_addresses("127.0.0.1",add_address_count);
            all_add_addresses.push_back(addresses);
        }

        //init addresses to remove, remove_threads_count * remove_address_count
        std::vector<std::vector<std::string>> all_remove_addresses;
        for (int i = 0; i < remove_threads_count; ++i) {
            // addresses to remove are different from addresses to add
            std::vector<std::string> addresses = gen_addresses("127.0.0.2", remove_address_count);
            all_remove_addresses.push_back(addresses);
            //add all address_to_remove to exist_addresses
            exist_addresses.insert(exist_addresses.end(), addresses.begin(), addresses.end());
        }

        // shuffle exist_addresses
        std::shuffle(exist_addresses.begin(), exist_addresses.end(), gen);

        BOOST_REQUIRE(exist_addresses.size() == exist_address_count + remove_threads_count * remove_address_count);

        manager1.add_addresses(exist_addresses, false);
        std::vector<std::thread> add_threads1;
        auto start1 = fc::time_point::now().time_since_epoch().count();
        for (auto& addresses : all_add_addresses) {
            add_threads1.emplace_back([&]() {
                manager1.add_addresses(addresses, false);
            });
        }
        // Join all the threads
        for (auto& t : add_threads1) {
            t.join();
        }
        auto end1 = fc::time_point::now().time_since_epoch().count();
        ilog(std::to_string(end1 - start1));

        manager2.add_addresses(exist_addresses, false);
        std::vector<std::thread> add_threads2;
        auto start2 = fc::time_point::now().time_since_epoch().count();
        for (auto& addresses : all_add_addresses) {
            add_threads2.emplace_back([&]() {
                manager2.add_addresses2(addresses, false);
            });
        }
        // Join all the threads
        for (auto& t : add_threads2) {
            t.join();
        }
        auto end2 = fc::time_point::now().time_since_epoch().count();
        ilog(std::to_string(end2 - start2));

        manager3.add_addresses(exist_addresses, false);
        std::vector<std::thread> add_remove_threads1;
        auto start3 = fc::time_point::now().time_since_epoch().count();
        for (auto& addresses : all_add_addresses) {
            add_remove_threads1.emplace_back([&]() {
                manager3.add_addresses(addresses, false);
            });
        }
        for (auto& addresses : all_remove_addresses) {
            add_remove_threads1.emplace_back([&]() {
                manager3.remove_addresses_str(addresses);
            });
        }
        // Join all the threads
        for (auto& t : add_remove_threads1) {
            t.join();
        }
        auto end3 = fc::time_point::now().time_since_epoch().count();
        ilog(std::to_string(end3 - start3));

        manager4.add_addresses(exist_addresses, false);
        std::vector<std::thread> add_remove_threads2;
        auto start4 = fc::time_point::now().time_since_epoch().count();
        for (auto& addresses : all_add_addresses) {
            add_remove_threads2.emplace_back([&]() {
                manager4.add_addresses2(addresses, false);
            });
        }
        for (auto& addresses : all_remove_addresses) {
            add_remove_threads2.emplace_back([&]() {
                manager4.remove_addresses_str2(addresses);
            });
        }
        // Join all the threads
        for (auto& t : add_remove_threads2) {
            t.join();
        }
        auto end4 = fc::time_point::now().time_since_epoch().count();
        ilog(std::to_string(end4 - start4));

        manager5.add_addresses(exist_addresses, false);
        std::vector<std::thread> remove_threads1;
        auto start5 = fc::time_point::now().time_since_epoch().count();
        for (auto& addresses : all_remove_addresses) {
            remove_threads1.emplace_back([&]() {
                manager5.remove_addresses_str(addresses);
            });
        }
        // Join all the threads
        for (auto& t : remove_threads1) {
            t.join();
        }
        auto end5 = fc::time_point::now().time_since_epoch().count();
        ilog(std::to_string(end5 - start5));

        manager6.add_addresses(exist_addresses, false);
        std::vector<std::thread> remove_threads2;
        auto start6 = fc::time_point::now().time_since_epoch().count();
        for (auto& addresses : all_remove_addresses) {
            remove_threads2.emplace_back([&]() {
                manager6.remove_addresses_str2(addresses);
            });
        }
        // Join all the threads
        for (auto& t : remove_threads2) {
            t.join();
        }
        auto end6 = fc::time_point::now().time_since_epoch().count();
        ilog(std::to_string(end6 - start6));

        BOOST_REQUIRE(manager1.get_addresses().size() == manager2.get_addresses().size());
        BOOST_REQUIRE(manager1.get_diff_addresses(manager2.get_addresses_str()).size() == 0);
        BOOST_REQUIRE(manager3.get_addresses().size() == manager4.get_addresses().size());
        BOOST_REQUIRE(manager3.get_diff_addresses(manager4.get_addresses_str()).size() == 0);
        BOOST_REQUIRE(manager5.get_addresses().size() == manager6.get_addresses().size());
        BOOST_REQUIRE(manager5.get_diff_addresses(manager6.get_addresses_str()).size() == 0);

    }

BOOST_AUTO_TEST_SUITE_END()


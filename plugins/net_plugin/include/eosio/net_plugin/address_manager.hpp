#pragma once
#include <regex>
#include <eosio/net_plugin/protocol.hpp>

namespace eosio {

    enum address_type_enum {
        blk = 1,
        trx = 2,
        peer = 4,
        both = blk | trx,
        all = blk | trx | peer
    };

    constexpr auto address_type_str( address_type_enum t ) {
        switch( t ) {
            case blk : return ":blk";
            case trx : return ":trx";
            case both : return "";
            case peer : return ":peer";
            default: return ":all";
        }
    }

    // determine address type
    inline address_type_enum str_to_address_type(const std::string& address_type_str) {
        static const std::unordered_map<std::string, address_type_enum> address_type_map = {
                {"blk", blk},
                {"trx", trx},
                {"peer", peer},
                {"", both},
                {"all", all},
        };
        const auto it = address_type_map.find(address_type_str);
        if (it != address_type_map.end()) {
            return it->second;
        }
        // if address type not match, return all by default
        return all;
    }

    inline bool validate_port(const std::string& port_str) {
        unsigned short port;
        try {
            port = boost::lexical_cast<unsigned short>(port_str);
        } catch (const boost::bad_lexical_cast&) {
            return false;
        }

        if (port == 0 || port > 65535) {
            return false;
        }

        return true;
    }

    struct peer_address {
        peer_address( address_type_enum t = all) : host(), port(), address_type(t), receive(), last_active(), manual(false) {}
        std::string host;
        std::string port;
        address_type_enum address_type;
        fc::time_point receive;
        fc::time_point last_active;
        bool manual;

        // if host and port are same then ignore other configuration
        bool operator==(const peer_address& other) const {
            return host == other.host && port == other.port;
        }

        bool operator!=(const peer_address& other) const {
            return host != other.host || port != other.port;
        }

        std::string to_address() const {
            return host + ":" + port;
        }

        std::string to_key() const {
            return host + ":" + port;
        }

        std::string to_str() const {
            return host + ":" + port + address_type_str(address_type);
        }

        static peer_address from_str(const std::string& input_address_str, bool is_manual = false) {
            try {
                //(?:([\w.-]+),)? for eosproducer1, （optional)
                //([\w.]+) for host and ip address
                //(?::(\d+)) for port
                //(?::(\w+))? for type (optional)
                //([-:\s]*(\w+))? for any suffix like ' - 012345' （optional)
                static const std::regex address_regex("^(?:([\\w.-]+),)?([\\w.]+)(?::(\\d+))(?::(\\w+))?([-:\\s]*(\\w+))?$");
                std::smatch match;
                if (!std::regex_match(input_address_str, match, address_regex)) {
                    throw std::invalid_argument(input_address_str);
                }

                std::string host_str = match[2].str();
                std::string port_str = match[3].str();
                std::string type_str = match[4].str();

                if (host_str.empty() || port_str.empty()) {
                    throw std::invalid_argument(input_address_str);
                }
                if (!validate_port(port_str)) {
                    throw std::invalid_argument("port number " + port_str);
                }

                peer_address address;
                address.host = host_str;
                address.port = port_str;
                address.address_type = str_to_address_type(type_str);
                address.receive = fc::time_point::now();
                address.last_active = fc::time_point::min();
                address.manual = is_manual;
                return address;
            }
            catch (const std::exception& e) {
                throw std::invalid_argument(std::string("Invalid peer address string: ") + e.what());
            }
        }


    };

    class address_manager {
    private:
        std::unordered_map<std::string, peer_address> addresses;
        mutable std::mutex addresses_mutex;

    public:
        // time_out for future address time out counting
        explicit address_manager(uint32_t time_out) {};

        void add_address(const peer_address& address);

        void add_or_update_address(const peer_address& address);

        void add_address_str(const std::string& address, bool is_manual = false);

        void add_active_address(const std::string& address);

        void add_addresses(const std::unordered_set<std::string>& new_addresses_str, bool is_manual = false);

        //for lock test
        void add_addresses2(const std::unordered_set<std::string>& new_addresses_str, bool is_manual = false);

        void remove_address(const peer_address& address);

        void remove_addresses_str(const std::unordered_set<std::string>& addresses_to_remove);

        void remove_addresses_str2(const std::unordered_set<std::string>& addresses_to_remove);

        void touch_address(const std::string& address);

        void update_address(const peer_address& updated_address);

        std::unordered_set<std::string> get_addresses() const;

        std::unordered_map<std::string, peer_address> get_addresses_map() const;

        std::unordered_set<std::string> get_manual_addresses() const;

        std::unordered_set<std::string> get_diff_addresses(const std::unordered_set<std::string>& addresses_exist, bool manual = false) const;

        std::unordered_set<std::string> get_latest_active_addresses(const std::chrono::seconds& secs, bool manual = false) const;

        bool has_address(const std::string& address_str) const;

    };

}
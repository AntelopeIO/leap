#pragma once

#include <boost/asio.hpp>
#include <boost/filesystem.hpp>

#include <appbase/application.hpp>
#include <eosio/chain/exceptions.hpp>

namespace bfs = boost::filesystem;

namespace eosio::producer_api {
class snapshot_schedule_db {
private:
    std::vector<producer_plugin::snapshot_request_information>  snapshot_requests;
    std::map<uint32_t, snapshot_information>                    pending_snapshots;
public:
   snapshot_schedule_db() {}
   ~snapshot_schedule_db() {}
};
}// namespace eosio::producer_api

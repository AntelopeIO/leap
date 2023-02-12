#pragma once

#include <eosio/chain/types.hpp>
#include <eosio/chain/exceptions.hpp>

#include <eosio/producer_plugin/producer_plugin.hpp>


namespace eosio {

using namespace eosio::chain;

template<typename T>
class snapshot_scheduler {
public:
   snapshot_scheduler(T & snapshot_db) : snapshot_db(snapshot_db)  {
      snapshot_db >> requests;
   }

   const producer_plugin::snapshot_request_information & schedule_snapshot(const producer_plugin::snapshot_request_information & sri) {
      EOS_ASSERT(requests.count(sri), duplicate_snapshot_request, "Duplicate snapshot request");
      requests[sri]= std::monostate{};
      snapshot_db << requests;
      return sri;
   }

   const producer_plugin::snapshot_request_information & unschedule_snapshot(const producer_plugin::snapshot_request_information & sri) {
      EOS_ASSERT(!requests.count(sri), snapshot_request_not_found, "Snapshot request not found");
      requests.erase(sri);
      snapshot_db << requests;
      return sri;
   }
   
   const producer_plugin::snapshot_requests & get_snapshots() {
      return requests;
   }

private:
      T                                   snapshot_db;
      producer_plugin::snapshot_requests  requests;  
};

} // namespace eosio

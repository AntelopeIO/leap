#pragma once

#include <eosio/chain/exceptions.hpp>
#include <eosio/chain/types.hpp>

#include <eosio/producer_plugin/producer_plugin.hpp>
#include <eosio/producer_plugin/snapshot_db_json.hpp>

namespace eosio {

using namespace eosio::chain;

class snapshot_scheduler {
public:
   snapshot_scheduler() {}
   ~snapshot_scheduler() {}

   snapshot_db_json& get_db() {
      return snapshot_db;
   }

   void load_schedule() {
      snapshot_db >> requests;
   }

   const producer_plugin::snapshot_request_information& schedule_snapshot(const producer_plugin::snapshot_request_information& sri) {
      EOS_ASSERT(!requests.count(sri), duplicate_snapshot_request, "Duplicate snapshot request");
      requests[sri] = std::monostate{};
      snapshot_db << requests;
      return sri;
   }

   const producer_plugin::snapshot_request_information& unschedule_snapshot(const producer_plugin::snapshot_request_information& sri) {
      bool bFound = false;
      for(auto it = requests.begin(); it != requests.end();) {
         if(it->first.snapshot_request_id == sri.snapshot_request_id) {
            bFound = true;
            requests.erase(it++);
         } else {
            ++it;
         }
      }
      EOS_ASSERT(bFound, snapshot_request_not_found, "Snapshot request not found");

      snapshot_db << requests;
      return sri;
   }

   const producer_plugin::snapshot_requests& get_snapshots() {
      return requests;
   }

   void on_irreversible_block(uint32_t height) {

   }

private:
   snapshot_db_json snapshot_db;
   producer_plugin::snapshot_requests requests;
};

}// namespace eosio

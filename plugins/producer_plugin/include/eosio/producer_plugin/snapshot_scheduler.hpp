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
      EOS_ASSERT(sri.end_block_num==0 || ((sri.end_block_num>0) && (sri.start_block_num <= sri.end_block_num)), invalid_snapshot_request, "End block number should be greater or equal to start block number");
      EOS_ASSERT(sri.block_spacing==0 || ((sri.block_spacing>0) && (sri.start_block_num + sri.block_spacing <= sri.end_block_num)), invalid_snapshot_request, "Block spacing exceeds defined by start and end range");
            
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

   void on_block(uint32_t height) {
      for (const auto& [req, snap]: requests) {
         // execute "asap" or if matches spacing
         if ((req.start_block_num == 0) || 
            (!((height - req.start_block_num) % req.block_spacing))) {
            x_execute_snapshot();
         }
         // assume "asap" for snapshot with missed/zero start, it can have spacing
         if (req.start_block_num == 0) {
            auto node = requests.extract(req);
            node.key().start_block_num = height;
            requests.insert(std::move(node));
         }
         // remove expired request
         if (req.end_block_num > 0 && height >= req.end_block_num) {
            requests.extract(req);         
         }      
      }
   }

private:
   void x_execute_snapshot() {
     // producer_plugin::next_function<producer_plugin::snapshot_information> next;
     ilog("Executing snapshot according to schedule...");
   }

   snapshot_db_json snapshot_db;
   producer_plugin::snapshot_requests requests;
};

}// namespace eosio

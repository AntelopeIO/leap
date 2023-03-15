#pragma once

#include <eosio/producer_plugin/snapshot_db_json.hpp>

#include <eosio/chain/block_state.hpp>
#include <eosio/chain/config.hpp>
#include <eosio/chain/exceptions.hpp>
#include <eosio/chain/resource_limits.hpp>
#include <eosio/chain/resource_limits_private.hpp>
#include <eosio/chain/transaction.hpp>
#include <eosio/chain/types.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index_container.hpp>

namespace eosio {

namespace bmi = boost::multi_index;

class snapshot_scheduler {
private:
   struct by_snapshot_id;
   struct by_snapshot_value;
   struct as_vector;

   using snapshot_requests = bmi::multi_index_container<
         producer_plugin::snapshot_schedule_information,
         indexed_by<
               bmi::hashed_unique<tag<by_snapshot_id>, BOOST_MULTI_INDEX_MEMBER(producer_plugin::snapshot_request_id_information, uint32_t, snapshot_request_id)>,
               bmi::random_access<tag<as_vector>>,
               bmi::ordered_unique<tag<by_snapshot_value>,
                                   composite_key<producer_plugin::snapshot_schedule_information,
                                                 BOOST_MULTI_INDEX_MEMBER(producer_plugin::snapshot_request_information, uint32_t, block_spacing),
                                                 BOOST_MULTI_INDEX_MEMBER(producer_plugin::snapshot_request_information, uint32_t, start_block_num),
                                                 BOOST_MULTI_INDEX_MEMBER(producer_plugin::snapshot_request_information, uint32_t, end_block_num)>>>>;
   snapshot_requests _snapshot_requests;
   snapshot_db_json _snapshot_db;
   uint32_t _snapshot_id   = 0;
   uint32_t _inflight_sid  = 0;
   std::function<void(producer_plugin::next_function<producer_plugin::snapshot_information>)> _create_snapshot;

   void x_serialize() {
      auto& vec = _snapshot_requests.get<as_vector>();
      std::vector<producer_plugin::snapshot_schedule_information> sr(vec.begin(), vec.end());
      _snapshot_db << sr;   
   }

public:
   snapshot_scheduler() = default;

   // snapshot_scheduler_listener
   void on_start_block(uint32_t height) {
      bool serialize_needed  = false;
      bool snapshot_executed = false; 

      auto execute_snapshot_with_log = [this, &height, &snapshot_executed](const auto & req) {
         // one snapshot per height
         if (!snapshot_executed) {
            dlog("snapshot scheduler creating a snapshot from the request [start_block_num:${start_block_num}, end_block_num=${end_block_num}, block_spacing=${block_spacing}], height=${height}",
               ("start_block_num", req.start_block_num)
               ("end_block_num",   req.end_block_num)
               ("block_spacing",   req.block_spacing)
               ("height",          height));
               
            execute_snapshot(req.snapshot_request_id);
            snapshot_executed = true;
         }
      };    

      for(const auto& req: _snapshot_requests.get<0>()) {
         // -1 since its called from start block
         bool recurring_snapshot =  req.block_spacing &&  (height >= req.start_block_num + 1) && (!((height - req.start_block_num - 1) % req.block_spacing));
         bool onetime_snapshot   = (!req.block_spacing) && (height == req.start_block_num + 1);
        
         // assume "asap" for snapshot with missed/zero start, it can have spacing
         if(!req.start_block_num) {
            // update start_block_num with current height only if this is recurring
            // if non recurring, will be executed and unscheduled
            if (req.block_spacing && height) {
               auto& snapshot_by_id = _snapshot_requests.get<by_snapshot_id>();
               auto it = snapshot_by_id.find(req.snapshot_request_id);
               _snapshot_requests.modify(it, [&height](auto& p) { p.start_block_num = height - 1; });
               serialize_needed = true;              
            }
            execute_snapshot_with_log(req);
         }        
         else if(recurring_snapshot || onetime_snapshot) {
            execute_snapshot_with_log(req);
         }       

         // cleanup - remove expired (or invalid) request
         if((!req.start_block_num && !req.block_spacing) ||         
            (!req.block_spacing && height >= (req.start_block_num + 1)) || 
            (req.end_block_num > 0 && height >= (req.end_block_num + 1))) {
            unschedule_snapshot(req.snapshot_request_id);
            if (snapshot_executed) break;
         }
      }

      // store db to filesystem
      if (serialize_needed) x_serialize();
   }

   // snapshot_scheduler_handler
   void schedule_snapshot(const producer_plugin::snapshot_request_information& sri) {
      auto& snapshot_by_value = _snapshot_requests.get<by_snapshot_value>();
      auto existing = snapshot_by_value.find(std::make_tuple(sri.block_spacing, sri.start_block_num, sri.end_block_num));
      EOS_ASSERT(existing == snapshot_by_value.end(), chain::duplicate_snapshot_request, "Duplicate snapshot request");

      if(sri.end_block_num > 0) {
         // if "end" is specified, it should be greater then start
         EOS_ASSERT(sri.start_block_num <= sri.end_block_num, chain::invalid_snapshot_request, "End block number should be greater or equal to start block number");
         // if also block_spacing specified, check it
         if(sri.block_spacing > 0) {
            EOS_ASSERT(sri.start_block_num + sri.block_spacing <= sri.end_block_num, chain::invalid_snapshot_request, "Block spacing exceeds defined by start and end range");
         }
      }

      _snapshot_requests.emplace(producer_plugin::snapshot_schedule_information {{_snapshot_id++},{sri.block_spacing, sri.start_block_num, sri.end_block_num, sri.snapshot_description},{}});
      x_serialize();
   }

   virtual void unschedule_snapshot(uint32_t sri) {
      auto& snapshot_by_id = _snapshot_requests.get<by_snapshot_id>();
      auto existing = snapshot_by_id.find(sri);
      EOS_ASSERT(existing != snapshot_by_id.end(), chain::snapshot_request_not_found, "Snapshot request not found");
      _snapshot_requests.erase(existing);
      x_serialize();
   }

   virtual producer_plugin::get_snapshot_requests_result get_snapshot_requests() {
      producer_plugin::get_snapshot_requests_result result;
      auto& asvector = _snapshot_requests.get<as_vector>();
      result.snapshot_requests.reserve(asvector.size());
      result.snapshot_requests.insert(result.snapshot_requests.begin(), asvector.begin(), asvector.end());
      return result;
   }

   // initialize with storage
   void set_db_path(bfs::path db_path) {
      _snapshot_db.set_path(std::move(db_path));
      // init from db
      if(fc::exists(_snapshot_db.get_json_path())) {
         std::vector<producer_plugin::snapshot_schedule_information> sr;
         _snapshot_db >> sr;
         // if db read succeeded, clear/load
         _snapshot_requests.get<by_snapshot_id>().clear();
         _snapshot_requests.insert(sr.begin(), sr.end());
      }
   }

   // add pending snapshot info to inflight snapshot request
   void add_pending_snapshot_info(const producer_plugin::snapshot_information & si) {
      auto& snapshot_by_id = _snapshot_requests.get<by_snapshot_id>();
      auto  snapshot_req   = snapshot_by_id.find(_inflight_sid);
      if (snapshot_req != snapshot_by_id.end()) {
         _snapshot_requests.modify(snapshot_req, [&si](auto& p) { 
            if (!p.pending_snapshots) {
               p.pending_snapshots = std::vector<producer_plugin::snapshot_information>();
            }
            p.pending_snapshots->emplace_back(si);       
         });
      }
   }

   // snapshot executor
   void set_create_snapshot_fn(std::function<void(producer_plugin::next_function<producer_plugin::snapshot_information>)> fn) {
      _create_snapshot = std::move(fn);
   }

   void execute_snapshot(uint32_t srid) {
      _inflight_sid = srid;
      auto next = [srid, this](const std::variant<fc::exception_ptr, producer_plugin::snapshot_information>& result) {
         if(std::holds_alternative<fc::exception_ptr>(result)) {
            try {
               std::get<fc::exception_ptr>(result)->dynamic_rethrow_exception();
            } catch(const fc::exception& e) {
               elog( "snapshot creation error: ${details}", ("details",e.to_detail_string()) );
               appbase::app().quit();
            } catch(const std::exception& e) {
               elog( "snapshot creation error: ${details}", ("details",e.what()) );
               appbase::app().quit();
            }
         } else {
            // success, snapshot finalized
            auto snapshot_info   = std::get<producer_plugin::snapshot_information>(result);
            auto& snapshot_by_id = _snapshot_requests.get<by_snapshot_id>();
            auto  snapshot_req   = snapshot_by_id.find(srid);

            if (snapshot_req != snapshot_by_id.end()) {               
               if (auto pending = snapshot_req->pending_snapshots; pending) {                 
                  auto it = std::remove_if(pending->begin(), pending->end(), [&snapshot_info](const producer_plugin::snapshot_information & s){ return s.head_block_num <=  snapshot_info.head_block_num; });
                  pending->erase(it, pending->end());
                  _snapshot_requests.modify(snapshot_req, [&pending](auto& p) { 
                     p.pending_snapshots = std::move(pending);
                  });
               }
            }
         }
      };
      _create_snapshot(next);
   }
};
}// namespace eosio

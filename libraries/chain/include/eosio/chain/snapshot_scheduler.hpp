#pragma once

#include <eosio/chain/pending_snapshot.hpp>
#include <eosio/chain/snapshot_db_json.hpp>

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
namespace chain {

namespace bmi = boost::multi_index;
namespace bfs = boost::filesystem;

class snapshot_scheduler {
public:
   struct snapshot_information {
      chain::block_id_type head_block_id;
      uint32_t head_block_num;
      fc::time_point head_block_time;
      uint32_t version;
      std::string snapshot_name;
   };

   struct snapshot_request_information {
      uint32_t block_spacing = 0;
      uint32_t start_block_num = 0;
      uint32_t end_block_num = 0;
      std::string snapshot_description = "";
   };

   struct snapshot_request_id_information {
      uint32_t snapshot_request_id = 0;
   };

   struct snapshot_schedule_result : public snapshot_request_id_information, public snapshot_request_information {
   };

   struct snapshot_schedule_information : public snapshot_request_id_information, public snapshot_request_information {
      std::vector<snapshot_information> pending_snapshots;
   };

   struct get_snapshot_requests_result {
      std::vector<snapshot_schedule_information> snapshot_requests;
   };

   template<typename T>
   using next_function = eosio::chain::next_function<T>;

   struct by_height;

   using pending_snapshot_index = bmi::multi_index_container<
         pending_snapshot<snapshot_information>,
         indexed_by<
               bmi::hashed_unique<tag<by_id>, BOOST_MULTI_INDEX_MEMBER(pending_snapshot<snapshot_information>, block_id_type, block_id)>,
               bmi::ordered_non_unique<tag<by_height>, BOOST_MULTI_INDEX_CONST_MEM_FUN(pending_snapshot<snapshot_information>, uint32_t, get_height)>>>;


private:
   struct by_snapshot_id;
   struct by_snapshot_value;
   struct as_vector;

   using snapshot_requests = bmi::multi_index_container<
         snapshot_scheduler::snapshot_schedule_information,
         indexed_by<
               bmi::hashed_unique<tag<by_snapshot_id>, BOOST_MULTI_INDEX_MEMBER(snapshot_scheduler::snapshot_request_id_information, uint32_t, snapshot_request_id)>,
               bmi::random_access<tag<as_vector>>,
               bmi::ordered_unique<tag<by_snapshot_value>,
                                   composite_key<snapshot_scheduler::snapshot_schedule_information,
                                                 BOOST_MULTI_INDEX_MEMBER(snapshot_scheduler::snapshot_request_information, uint32_t, block_spacing),
                                                 BOOST_MULTI_INDEX_MEMBER(snapshot_scheduler::snapshot_request_information, uint32_t, start_block_num),
                                                 BOOST_MULTI_INDEX_MEMBER(snapshot_scheduler::snapshot_request_information, uint32_t, end_block_num)>>>>;
   snapshot_requests _snapshot_requests;
   snapshot_db_json _snapshot_db;
   pending_snapshot_index _pending_snapshot_index;

   uint32_t _snapshot_id = 0;
   uint32_t _inflight_sid = 0;

   // path to write the snapshots to
   bfs::path _snapshots_dir;

   void x_serialize() {
      auto& vec = _snapshot_requests.get<as_vector>();
      std::vector<snapshot_scheduler::snapshot_schedule_information> sr(vec.begin(), vec.end());
      _snapshot_db << sr;
   };

public:
   snapshot_scheduler() = default;

   // snapshot scheduler listener
   void on_start_block(uint32_t height, chain::controller& chain);

   // to promote pending snapshots
   void on_irreversible_block(const signed_block_ptr& lib, const chain::controller& chain);

   // snapshot scheduler handlers
   snapshot_scheduler::snapshot_schedule_result schedule_snapshot(const snapshot_scheduler::snapshot_request_information& sri);
   snapshot_scheduler::snapshot_schedule_result unschedule_snapshot(uint32_t sri);
   snapshot_scheduler::get_snapshot_requests_result get_snapshot_requests();

   // initialize with storage
   void set_db_path(bfs::path db_path);

   // set snapshot path
   void set_snapshots_path(bfs::path sn_path);

   // add pending snapshot info to inflight snapshot request
   void add_pending_snapshot_info(const snapshot_scheduler::snapshot_information& si);

   // execute snapshot
   void execute_snapshot(uint32_t srid, chain::controller& chain);

   // former producer_plugin snapshot fn
   void create_snapshot(snapshot_scheduler::next_function<snapshot_scheduler::snapshot_information> next, chain::controller& chain, std::function<void(void)> predicate);
};
}// namespace chain
}// namespace eosio

FC_REFLECT(eosio::chain::snapshot_scheduler::snapshot_information, (head_block_id)(head_block_num)(head_block_time)(version)(snapshot_name))
FC_REFLECT(eosio::chain::snapshot_scheduler::snapshot_request_information, (block_spacing)(start_block_num)(end_block_num)(snapshot_description))
FC_REFLECT(eosio::chain::snapshot_scheduler::snapshot_request_id_information, (snapshot_request_id))
FC_REFLECT(eosio::chain::snapshot_scheduler::get_snapshot_requests_result, (snapshot_requests))
FC_REFLECT_DERIVED(eosio::chain::snapshot_scheduler::snapshot_schedule_information, (eosio::chain::snapshot_scheduler::snapshot_request_id_information)(eosio::chain::snapshot_scheduler::snapshot_request_information), (pending_snapshots))
FC_REFLECT_DERIVED(eosio::chain::snapshot_scheduler::snapshot_schedule_result, (eosio::chain::snapshot_scheduler::snapshot_request_id_information)(eosio::chain::snapshot_scheduler::snapshot_request_information), )

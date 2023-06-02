#pragma once

#include <eosio/chain/pending_snapshot.hpp>

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

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <limits>

namespace eosio::chain {

namespace bmi = boost::multi_index;
namespace fs = std::filesystem;

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
      uint32_t end_block_num = std::numeric_limits<uint32_t>::max();
      std::string snapshot_description = "";
   };

   // this struct used to hold request params in api call
   // it is differentiate between 0 and empty values
   struct snapshot_request_params {
      std::optional<uint32_t> block_spacing;
      std::optional<uint32_t> start_block_num;
      std::optional<uint32_t> end_block_num;
      std::optional<std::string> snapshot_description;
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

   class snapshot_db_json {
   public:
      snapshot_db_json() = default;
      ~snapshot_db_json() = default;

      void set_path(std::filesystem::path path) {
         db_path = std::move(path);
      }

      std::filesystem::path get_json_path() const {
         return db_path / "snapshot-schedule.json";
      }

      const snapshot_db_json& operator>>(std::vector<snapshot_schedule_information>& sr) {
         boost::property_tree::ptree root;

         try {
            std::ifstream file(get_json_path().string());
            file.exceptions(std::istream::failbit | std::istream::eofbit);
            boost::property_tree::read_json(file, root);

            // parse ptree
            for(boost::property_tree::ptree::value_type& req: root.get_child("snapshot_requests")) {
               snapshot_schedule_information ssi;
               ssi.snapshot_request_id = req.second.get<uint32_t>("snapshot_request_id");
               ssi.snapshot_description = req.second.get<std::string>("snapshot_description");
               ssi.block_spacing = req.second.get<uint32_t>("block_spacing");
               ssi.start_block_num = req.second.get<uint32_t>("start_block_num");
               ssi.end_block_num = req.second.get<uint32_t>("end_block_num");
               sr.push_back(ssi);
            }
         } catch(std::ifstream::failure& e) {
            elog("unable to restore snapshots schedule from filesystem ${jsonpath}, details: ${details}",
                 ("jsonpath", get_json_path().string())("details", e.what()));
         }

         return *this;
      }

      const snapshot_db_json& operator<<(const std::vector<snapshot_schedule_information>& sr) const {
         boost::property_tree::ptree root;
         boost::property_tree::ptree node_srs;

         for(const auto& key: sr) {
            boost::property_tree::ptree node;
            node.put("snapshot_request_id", key.snapshot_request_id);
            node.put("snapshot_description", key.snapshot_description);
            node.put("block_spacing", key.block_spacing);
            node.put("start_block_num", key.start_block_num);
            node.put("end_block_num", key.end_block_num);
            node_srs.push_back(std::make_pair("", node));
         }

         root.push_back(std::make_pair("snapshot_requests", node_srs));

         try {
            std::ofstream file(get_json_path().string());
            file.exceptions(std::istream::failbit | std::istream::eofbit);
            boost::property_tree::write_json(file, root);
         } catch(std::ofstream::failure& e) {
            elog("unable to store snapshots schedule to filesystem to ${jsonpath}, details: ${details}",
                 ("jsonpath", get_json_path().string())("details", e.what()));
         }

         return *this;
      }

   private:
      fs::path db_path;
   };

private:
   struct by_snapshot_id;
   struct by_snapshot_value;
   struct as_vector;

   using snapshot_requests = bmi::multi_index_container<
         snapshot_schedule_information,
         indexed_by<
               bmi::hashed_unique<tag<by_snapshot_id>, BOOST_MULTI_INDEX_MEMBER(snapshot_request_id_information, uint32_t, snapshot_request_id)>,
               bmi::random_access<tag<as_vector>>,
               bmi::ordered_unique<tag<by_snapshot_value>,
                                   composite_key<snapshot_schedule_information,
                                                 BOOST_MULTI_INDEX_MEMBER(snapshot_request_information, uint32_t, block_spacing),
                                                 BOOST_MULTI_INDEX_MEMBER(snapshot_request_information, uint32_t, start_block_num),
                                                 BOOST_MULTI_INDEX_MEMBER(snapshot_request_information, uint32_t, end_block_num)>>>>;
   snapshot_requests _snapshot_requests;
   snapshot_db_json _snapshot_db;
   pending_snapshot_index _pending_snapshot_index;

   uint32_t _snapshot_id = 0;
   uint32_t _inflight_sid = 0;

   // path to write the snapshots to
   fs::path _snapshots_dir;

   void x_serialize() {
      auto& vec = _snapshot_requests.get<as_vector>();
      std::vector<snapshot_schedule_information> sr(vec.begin(), vec.end());
      _snapshot_db << sr;
   };

public:
   snapshot_scheduler() = default;

   // snapshot scheduler listener
   void on_start_block(uint32_t height, chain::controller& chain);

   // to promote pending snapshots
   void on_irreversible_block(const signed_block_ptr& lib, const chain::controller& chain);

   // snapshot scheduler handlers
   snapshot_schedule_result schedule_snapshot(const snapshot_request_information& sri);
   snapshot_schedule_result unschedule_snapshot(uint32_t sri);
   get_snapshot_requests_result get_snapshot_requests();

   // initialize with storage
   void set_db_path(fs::path db_path);

   // set snapshot path
   void set_snapshots_path(fs::path sn_path);

   // add pending snapshot info to inflight snapshot request
   void add_pending_snapshot_info(const snapshot_information& si);

   // execute snapshot
   void execute_snapshot(uint32_t srid, chain::controller& chain);

   // former producer_plugin snapshot fn
   void create_snapshot(next_function<snapshot_information> next, chain::controller& chain, std::function<void(void)> predicate);
};


}// namespace eosio::chain

FC_REFLECT(eosio::chain::snapshot_scheduler::snapshot_information, (head_block_id) (head_block_num) (head_block_time) (version) (snapshot_name))
FC_REFLECT(eosio::chain::snapshot_scheduler::snapshot_request_information, (block_spacing) (start_block_num) (end_block_num) (snapshot_description))
FC_REFLECT(eosio::chain::snapshot_scheduler::snapshot_request_params, (block_spacing) (start_block_num) (end_block_num) (snapshot_description))
FC_REFLECT(eosio::chain::snapshot_scheduler::snapshot_request_id_information, (snapshot_request_id))
FC_REFLECT(eosio::chain::snapshot_scheduler::get_snapshot_requests_result, (snapshot_requests))
FC_REFLECT_DERIVED(eosio::chain::snapshot_scheduler::snapshot_schedule_information, (eosio::chain::snapshot_scheduler::snapshot_request_id_information)(eosio::chain::snapshot_scheduler::snapshot_request_information), (pending_snapshots))
FC_REFLECT_DERIVED(eosio::chain::snapshot_scheduler::snapshot_schedule_result, (eosio::chain::snapshot_scheduler::snapshot_request_id_information)(eosio::chain::snapshot_scheduler::snapshot_request_information), )

#pragma once

#include <eosio/chain/exceptions.hpp>
#include <eosio/chain/types.hpp>

#include <eosio/producer_plugin/producer_plugin.hpp>

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>


namespace eosio {

using namespace eosio::chain;

class snapshot_db_json {
public:
   snapshot_db_json() {}
   snapshot_db_json(bfs::path db_path) : db_path(db_path) {}
   ~snapshot_db_json() {}

   void set_path(bfs::path path) {
      db_path = path;
   }

   bfs::path get_json_path() {
      return db_path / "snapshot-schedule.json";
   }

   const snapshot_db_json& operator>>(producer_plugin::snapshot_requests& sr) {
      boost::property_tree::ptree root;
      std::ifstream file(get_json_path().string());
      boost::property_tree::read_json(file, root);
      file.close();

      // parse ptree
      for(boost::property_tree::ptree::value_type& req: root.get_child("snapshot_requests")) {
         producer_plugin::snapshot_request_information sri;
         sri.snapshot_request_id = req.second.get<uint32_t>("snapshot_request_id");
         sri.snapshot_description = req.second.get<std::string>("snapshot_description");
         sri.block_spacing = req.second.get<uint32_t>("block_spacing");
         sri.start_block_num = req.second.get<uint32_t>("start_block_num");
         sri.end_block_num = req.second.get<uint32_t>("end_block_num");
         sr[sri] = std::monostate{};
      }

      return *this;
   }

   const snapshot_db_json& operator<<(const producer_plugin::snapshot_requests& sr) {
      boost::property_tree::ptree root;
      boost::property_tree::ptree node_srs;

      for(const auto& [key, value]: sr) {
         boost::property_tree::ptree node;
         node.put("snapshot_request_id", key.snapshot_request_id);
         node.put("snapshot_description", key.snapshot_description);
         node.put("block_spacing", key.block_spacing);
         node.put("start_block_num", key.start_block_num);
         node.put("end_block_num", key.end_block_num);
         node_srs.push_back(std::make_pair("", node));
      }
      root.push_back(std::make_pair("snapshot_requests", node_srs));

      std::ofstream file(get_json_path().string());
      boost::property_tree::write_json(file, root);
      file.close();

      return *this;
   }

private:
   bfs::path db_path;
};

}// namespace eosio

#pragma once

#include <eosio/chain/exceptions.hpp>
#include <eosio/chain/types.hpp>

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

namespace eosio::chain {

namespace fs = std::filesystem;

/// this class designed to serialize/deserialize snapshot schedule to a filesystem so it can be restored after restart
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

   template<typename F>
   const snapshot_db_json& operator>>(std::vector<F>& sr) {
      boost::property_tree::ptree root;

      try {
         std::ifstream file(get_json_path().string());
         file.exceptions(std::istream::failbit | std::istream::eofbit);
         boost::property_tree::read_json(file, root);

         // parse ptree
         for(boost::property_tree::ptree::value_type& req: root.get_child("snapshot_requests")) {
            F ssi;
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

   template<typename F>
   const snapshot_db_json& operator<<(std::vector<F>& sr) const {
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

}// namespace eosio::chain

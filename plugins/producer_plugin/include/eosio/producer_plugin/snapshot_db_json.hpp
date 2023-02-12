#pragma once

#include <eosio/chain/types.hpp>
#include <eosio/chain/exceptions.hpp>

#include <eosio/producer_plugin/producer_plugin.hpp>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>


namespace eosio {

using namespace eosio::chain;

class snapshot_db_json {
public:
   snapshot_db_json(std::string db_path) : db_path(db_path) {}
 
   const snapshot_db_json & operator >> (producer_plugin::snapshot_requests & sri)
   {
        boost::property_tree::ptree root;
        std::ifstream file(db_path);
        boost::property_tree::read_json(file, root);
        file.close();

        // parse ptree

        return *this;
   }

   const snapshot_db_json & operator << (const producer_plugin::snapshot_requests & sri)
   {
        boost::property_tree::ptree root;

       // boost::property_tree::ptree node;
       // node.put("aaa", bbb);
       // root.push_back(std::make_pair("", node));
    
        std::ofstream file(db_path);
        boost::property_tree::write_json(file, root);
        file.close();

        return *this;
   }

private:
      std::string db_path;
};

} // namespace eosio

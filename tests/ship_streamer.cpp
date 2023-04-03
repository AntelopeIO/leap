#include <eosio/abi.hpp>
#include <eosio/from_json.hpp>
#include <eosio/convert.hpp>

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/prettywriter.h>

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/program_options.hpp>

#include <iostream>
#include <string>

namespace bpo = boost::program_options;

int main(int argc, char* argv[]) {
   boost::asio::io_context ctx;
   boost::asio::ip::tcp::resolver resolver(ctx);
   boost::beast::websocket::stream<boost::asio::ip::tcp::socket> stream(ctx);
   eosio::abi abi;

   bpo::options_description cli("ship_streamer command line options");
   bool help = false;
   std::string socket_address = "127.0.0.1:8080";
   uint32_t start_block_num = 1;
   uint32_t end_block_num = std::numeric_limits<u_int32_t>::max()-1;
   bool irreversible_only = false;
   bool fetch_block = false;
   bool fetch_traces = false;
   bool fetch_deltas = false;

   cli.add_options()
      ("help,h", bpo::bool_switch(&help)->default_value(false), "Print this help message and exit.")
      ("socket-address,a", bpo::value<std::string>(&socket_address)->default_value(socket_address), "Websocket address and port.")
      ("start-block-num", bpo::value<uint32_t>(&start_block_num)->default_value(start_block_num), "Block to start streaming from")
      ("end-block-num", bpo::value<uint32_t>(&end_block_num)->default_value(end_block_num), "Block to stop streaming")
      ("irreversible-only", bpo::bool_switch(&irreversible_only)->default_value(irreversible_only), "Irreversible blocks only")
      ("fetch-block", bpo::bool_switch(&fetch_block)->default_value(fetch_block), "Fetch blocks")
      ("fetch-traces", bpo::bool_switch(&fetch_traces)->default_value(fetch_traces), "Fetch traces")
      ("fetch-deltas", bpo::bool_switch(&fetch_deltas)->default_value(fetch_deltas), "Fetch deltas")
      ;
   bpo::variables_map varmap;
   bpo::store(bpo::parse_command_line(argc, argv, cli), varmap);
   bpo::notify(varmap);

   if(help) {
      cli.print(std::cout);
      return 0;
   }

   std::string::size_type colon = socket_address.find(':');
   eosio::check(colon != std::string::npos, "Missing ':' seperator in Websocket address and port");
   std::string statehistory_server = socket_address.substr(0, colon);
   std::string statehistory_port = socket_address.substr(colon+1);

   try {
      boost::asio::connect(stream.next_layer(), resolver.resolve(statehistory_server, statehistory_port));
      stream.handshake(statehistory_server, "/");

      {
         boost::beast::flat_buffer abi_buffer;
         stream.read(abi_buffer);
         std::string abi_string((const char*)abi_buffer.data().data(), abi_buffer.data().size());
         eosio::json_token_stream token_stream(abi_string.data());
         eosio::abi_def abidef = eosio::from_json<eosio::abi_def>(token_stream);
         eosio::convert(abidef, abi);
      }

      const eosio::abi_type& request_type = abi.abi_types.at("request");
      const eosio::abi_type& result_type = abi.abi_types.at("result");

      rapidjson::StringBuffer request_sb;
      rapidjson::PrettyWriter<rapidjson::StringBuffer> request_writer(request_sb);

      //struct get_blocks_request_v0 {
      //   uint32_t                    start_block_num        = 0;
      //   uint32_t                    end_block_num          = 0;
      //   uint32_t                    max_messages_in_flight = 0;
      //   std::vector<block_position> have_positions         = {};
      //   bool                        irreversible_only      = false;
      //   bool                        fetch_block            = false;
      //   bool                        fetch_traces           = false;
      //   bool                        fetch_deltas           = false;
      //};
      request_writer.StartArray();
         request_writer.String("get_blocks_request_v0");
         request_writer.StartObject();
         request_writer.Key("start_block_num");
         request_writer.Uint(start_block_num);
         request_writer.Key("end_block_num");
         request_writer.String(std::to_string(end_block_num + 1).c_str()); // SHiP is (start-end] exclusive
         request_writer.Key("max_messages_in_flight");
         request_writer.String(std::to_string(std::numeric_limits<u_int32_t>::max()).c_str());
         request_writer.Key("have_positions");
         request_writer.StartArray();
         request_writer.EndArray();
         request_writer.Key("irreversible_only");
         request_writer.Bool(irreversible_only);
         request_writer.Key("fetch_block");
         request_writer.Bool(fetch_block);
         request_writer.Key("fetch_traces");
         request_writer.Bool(fetch_traces);
         request_writer.Key("fetch_deltas");
         request_writer.Bool(fetch_deltas);
         request_writer.EndObject();
      request_writer.EndArray();

      stream.binary(true);
      stream.write(boost::asio::buffer(request_type.json_to_bin(request_sb.GetString(), [](){})));

      //      block_num, block_id
      std::map<uint32_t, std::string> block_ids;
      bool is_first = true;
      for(;;) {
         boost::beast::flat_buffer buffer;
         stream.read(buffer);

         eosio::input_stream is((const char*)buffer.data().data(), buffer.data().size());
         rapidjson::Document result_document;
         result_document.Parse(result_type.bin_to_json(is).c_str());

         eosio::check(!result_document.HasParseError(),                                      "Failed to parse result JSON from abieos");
         eosio::check(result_document.IsArray(),                                             "result should have been an array (variant) but it's not");
         eosio::check(result_document.Size() == 2,                                           "result was an array but did not contain 2 items like a variant should");
         eosio::check(std::string(result_document[0].GetString()) == "get_blocks_result_v0", "result type doesn't look like get_blocks_result_v0");
         eosio::check(result_document[1].IsObject(),                                         "second item in result array is not an object");
         eosio::check(result_document[1].HasMember("head"),                                  "cannot find 'head' in result");
         eosio::check(result_document[1]["head"].IsObject(),                                 "'head' is not an object");
         eosio::check(result_document[1]["head"].HasMember("block_num"),                     "'head' does not contain 'block_num'");
         eosio::check(result_document[1]["head"]["block_num"].IsUint(),                      "'head.block_num' isn't a number");
         eosio::check(result_document[1]["head"].HasMember("block_id"),                      "'head' does not contain 'block_id'");
         eosio::check(result_document[1]["head"]["block_id"].IsString(),                     "'head.block_id' isn't a string");

         uint32_t this_block_num = 0;
         if( result_document[1].HasMember("this_block") && result_document[1]["this_block"].IsObject() ) {
            if( result_document[1]["this_block"].HasMember("block_num") && result_document[1]["this_block"]["block_num"].IsUint() ) {
               this_block_num = result_document[1]["this_block"]["block_num"].GetUint();
            }
            std::string this_block_id;
            if( result_document[1]["this_block"].HasMember("block_id") && result_document[1]["this_block"]["block_id"].IsString() ) {
               this_block_id = result_document[1]["this_block"]["block_id"].GetString();
            }
            std::string prev_block_id;
            if( result_document[1]["prev_block"].HasMember("block_id") && result_document[1]["prev_block"]["block_id"].IsString() ) {
               prev_block_id = result_document[1]["prev_block"]["block_id"].GetString();
            }
            if( !irreversible_only && !this_block_id.empty() && !prev_block_id.empty() ) {
               // verify forks were sent
               if (block_ids.count(this_block_num-1)) {
                  if (block_ids[this_block_num-1] != prev_block_id) {
                     std::cerr << "Received block: << " << this_block_num << " that does not link to previous: " << block_ids[this_block_num-1] << std::endl;
                     return 1;
                  }
               }
               block_ids[this_block_num] = this_block_id;

               if( result_document[1]["last_irreversible"].HasMember("block_num") && result_document[1]["last_irreversible"]["block_num"].IsUint() ) {
                  uint32_t lib_num = result_document[1]["last_irreversible"]["block_num"].GetUint();
                  auto i = block_ids.lower_bound(lib_num);
                  if (i != block_ids.end()) {
                     block_ids.erase(block_ids.begin(), i);
                  }
               }
            }

         }

         if(is_first) {
            std::cout << "[" << std::endl;
            is_first = false;
         } else {
            std::cout << "," << std::endl;
         }
         std::cout << "{ \"get_blocks_result_v0\":" << std::endl;

         rapidjson::StringBuffer result_sb;
         rapidjson::PrettyWriter<rapidjson::StringBuffer> result_writer(result_sb);
         result_document[1].Accept(result_writer);
         std::cout << result_sb.GetString() << std::endl << "}" << std::endl;

         if( this_block_num == end_block_num ) break;
      }

      std::cout << "]" << std::endl;
   }
   catch(std::exception& e) {
      std::cerr << "Caught exception: " << e.what() << std::endl;
      return 1;
   }

   return 0;
}

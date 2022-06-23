#include <eosio/abi.hpp>
#include <eosio/from_json.hpp>
#include <eosio/convert.hpp>

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/prettywriter.h>

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/program_options.hpp>
#include <boost/algorithm/string.hpp>


#include <iostream>
#include <string>

namespace bpo = boost::program_options;

int main(int argc, char* argv[]) {
   boost::asio::io_context ctx;
   boost::asio::ip::tcp::resolver resolver(ctx);
   boost::beast::websocket::stream<boost::asio::ip::tcp::socket> stream(ctx);
   eosio::abi abi;

   bpo::options_description cli("ship_client command line options");
   bool help = false;
   std::string socket_address = "127.0.0.1:8080";
   uint32_t num_requests = 1;

   cli.add_options()
      ("help,h", bpo::bool_switch(&help)->default_value(false), "Print this help message and exit.")
      ("socket-address,a", bpo::value<std::string>(&socket_address)->default_value(socket_address), "Websocket address and port.")
      ("num-requests,n", bpo::value<uint32_t>(&num_requests)->default_value(num_requests), "number of requests to make");
   bpo::variables_map varmap;
   bpo::store(bpo::parse_command_line(argc, argv, cli), varmap);
   bpo::notify(varmap);

   if(help) {
      cli.print(std::cout);
      return 0;
   }

   std::string statehistory_server, statehistory_port;
   
   // unix socket
   if(boost::algorithm::starts_with(socket_address, "ws+unix://") || boost::algorithm::starts_with(socket_address, "unix://")) {
         // statehistory_port   = "";
         // statehistory_server = socket_address.substr(socket_address.find("unix://") + strlen("unix://") + 1);     
         // TODO: hack - switching to ip connection, unix socket client needs to be implemented
         statehistory_server =  "127.0.0.1";
         statehistory_port   =  "8080";
   }
   else {
      std::string::size_type colon = socket_address.find(':');
      eosio::check(colon != std::string::npos, "Missing ':' seperator in Websocket address and port");
      statehistory_server = socket_address.substr(0, colon);
      statehistory_port   = socket_address.substr(colon+1);
   }

   std::cerr << "[\n{\n   \"status\": \"construct\",\n   \"time\": " << time(NULL) << "\n},\n";

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
   
      std::cerr << "{\n   \"status\": \"set_abi\",\n   \"time\": " << time(NULL) << "\n},\n";

      const eosio::abi_type& request_type = abi.abi_types.at("request");
      const eosio::abi_type& result_type = abi.abi_types.at("result");

      bool is_first = true;
      uint32_t first_block_num = 0;
      uint32_t last_block_num = 0;

      while(num_requests--) {
         rapidjson::StringBuffer request_sb;
         rapidjson::PrettyWriter<rapidjson::StringBuffer> request_writer(request_sb);

         request_writer.StartArray();
            request_writer.String("get_status_request_v0");
            request_writer.StartObject();
            request_writer.EndObject();
         request_writer.EndArray();

         stream.write(boost::asio::buffer(request_type.json_to_bin(request_sb.GetString(), [](){})));

         boost::beast::flat_buffer buffer;
         stream.read(buffer);

         eosio::input_stream is((const char*)buffer.data().data(), buffer.data().size());
         rapidjson::Document result_doucment;
         result_doucment.Parse(result_type.bin_to_json(is).c_str());

         eosio::check(!result_doucment.HasParseError(),                                      "Failed to parse result JSON from abieos");
         eosio::check(result_doucment.IsArray(),                                             "result should have been an array (variant) but it's not");
         eosio::check(result_doucment.Size() == 2,                                           "result was an array but did not contain 2 items like a variant should");
         eosio::check(std::string(result_doucment[0].GetString()) == "get_status_result_v0", "result type doesn't look like get_status_result_v0");
         eosio::check(result_doucment[1].IsObject(),                                         "second item in result array is not an object");
         eosio::check(result_doucment[1].HasMember("head"),                                  "cannot find 'head' in result");
         eosio::check(result_doucment[1]["head"].IsObject(),                                 "'head' is not an object");
         eosio::check(result_doucment[1]["head"].HasMember("block_num"),                     "'head' does not contain 'block_num'");
         eosio::check(result_doucment[1]["head"]["block_num"].IsUint(),                      "'head.block_num' isn't a number");

         uint32_t this_block_num = result_doucment[1]["head"]["block_num"].GetUint();

         if(is_first) {
            std::cout << "[" << std::endl;
            first_block_num = this_block_num;
            is_first = false;
         }
         else {
            std::cout << "," << std::endl;
         }
         std::cout << "{ \"get_status_result_v0\":" << std::endl;

         rapidjson::StringBuffer result_sb;
         rapidjson::PrettyWriter<rapidjson::StringBuffer> result_writer(result_sb);
         result_doucment[1].Accept(result_writer);
         std::cout << result_sb.GetString() << std::endl << "}" << std::endl;

         last_block_num = this_block_num;
      }

      std::cout << "]" << std::endl;

      rapidjson::StringBuffer done_sb;
      rapidjson::PrettyWriter<rapidjson::StringBuffer> done_writer(done_sb);

      done_writer.StartObject();
         done_writer.Key("status");
         done_writer.String("done");
         done_writer.Key("time");
         done_writer.Uint(time(NULL));
         done_writer.Key("first_block_num");
         done_writer.Uint(first_block_num);
         done_writer.Key("last_block_num");
         done_writer.Uint(last_block_num);
      done_writer.EndObject();

      std::cerr << done_sb.GetString() << std::endl << "]" << std::endl;
   }
   catch(std::exception& e) {
      std::cerr << "Caught exception: " << e.what() << std::endl;
      return 1;
   }

   return 0;
}

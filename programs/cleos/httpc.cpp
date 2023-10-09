//
// sync_client.cpp
// ~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2012 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <eosio/chain/exceptions.hpp>
#include <eosio/chain_plugin/chain_plugin.hpp>
#include <eosio/http_plugin/http_plugin.hpp>
#include <fc/io/json.hpp>
#include <fc/variant.hpp>
#include <iostream>
#include <istream>
#include <ostream>
#include "httpc.hpp"
#include "do_http_post.hpp"

using namespace eosio::chain;

namespace eosio { namespace client { namespace http {

fc::variant do_http_call(const config_t& config, const std::string& base_uri, const std::string& path,
                            const fc::variant& postdata) {
   std::string postjson;
   if (!postdata.is_null()) {
      if (config.print_request) {
         postjson = fc::json::to_pretty_string(postdata);
         std::cerr << "REQUEST:\n"
                     << "---------------------\n"
                     << postjson << "\n"
                     << "---------------------" << std::endl;
      } else {
         postjson = fc::json::to_string(postdata, fc::time_point::maximum());
      }
   }

   auto [status_code, re] = do_http_post(base_uri, path, config.headers, postjson, !config.no_verify_cert,
                                          config.verbose, config.trace);

   fc::variant response_result;
   bool        print_response = config.print_response;
   try {
      response_result = fc::json::from_string(re);
   } catch(...) {
      // re reported below if print_response requested
      print_response = true;
   }

   if( print_response ) {
      std::cerr << "RESPONSE:" << std::endl
                << "---------------------" << std::endl
                << ( response_result.is_null() ? re : fc::json::to_pretty_string( response_result ) ) << std::endl
                << "---------------------" << std::endl;
   }

   if( !response_result.is_null() ) {
      if( status_code == 200 || status_code == 201 || status_code == 202 ) {
         return response_result;
      } else if( status_code == 404 ) {
         // Unknown endpoint
         if (path.compare(0, chain_func_base.size(), chain_func_base) == 0) {
            throw chain::missing_chain_api_plugin_exception(FC_LOG_MESSAGE(error, "Chain API plugin is not enabled on specified endpoint"));
         } else if (path.compare(0, wallet_func_base.size(), wallet_func_base) == 0) {
            throw chain::missing_wallet_api_plugin_exception(FC_LOG_MESSAGE(error, "Wallet is not available on specified endpoint"));
         } else if (path.compare(0, history_func_base.size(), history_func_base) == 0) {
            throw chain::missing_history_api_plugin_exception(FC_LOG_MESSAGE(error, "History API support is not enabled on specified endpoint"));
         } else if (path.compare(0, net_func_base.size(), net_func_base) == 0) {
            throw chain::missing_net_api_plugin_exception(FC_LOG_MESSAGE(error, "Net API plugin is not enabled on specified endpoint"));
         }
      } else {
         auto &&error_info = response_result.as<eosio::error_results>().error;
         // Construct fc exception from error
         const auto &error_details = error_info.details;

         fc::log_messages logs;
         for (auto itr = error_details.begin(); itr != error_details.end(); itr++) {
            const auto& context = fc::log_context(fc::log_level::error, itr->file.data(), itr->line_number, itr->method.data());
            logs.emplace_back(fc::log_message(context, itr->message));
         }

         throw fc::exception(logs, error_info.code, error_info.name, error_info.what);
      }
   }

   EOS_ASSERT( status_code == 200 && !response_result.is_null(), http_request_fail,
               "Error code ${c}\n: ${msg}\n", ("c", status_code)("msg", re) );
   return response_result;
   }
}}}

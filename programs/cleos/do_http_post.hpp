
#pragma once

#include <cstring>
#include <string>
#include <tuple>
#include <vector>
#include <fc/exception/exception.hpp>

namespace eosio { namespace client { namespace http {
   FC_DECLARE_EXCEPTION( connection_exception, 1100000, "Connection Exception" );

   std::tuple<unsigned int, std::string> do_http_post(const std::string& base_uri, const std::string& path,
                                                      const std::vector<std::string>& headers,
                                                      const std::string& postjson, bool verify_cert, bool verbose,
                                                      bool trace);
}}} // namespace eosio::client::http
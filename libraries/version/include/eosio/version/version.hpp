#pragma once

#include <string> // std::string

namespace eosio { namespace version {

   ///< Grab the basic version information of the client; example: `v1.8.0-rc1`
   const std::string& version_client();

   ///< Grab the full version information of the client; example:  `v1.8.0-rc1-7de458254[-dirty]`
   const std::string& version_full();

   ///< Grab the full git hash hex string; example:  `f3b24a2118df7d98659511897cfde6de0e4a375b`
   const std::string& version_hash();
} }

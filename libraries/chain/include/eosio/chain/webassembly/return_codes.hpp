#pragma once


namespace eosio::chain::webassembly { 

   enum return_code : int32_t {
      failure = -1,
      success = 0,
   };

} // ns eosio::chain::webassembly
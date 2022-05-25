#pragma once


namespace eosio { namespace chain { namespace webassembly { namespace error_codes {

   enum crypto : int32_t {
      none      = 0,
      fail      = 1
   };

}}}} // ns eosio::chain::webassembly::error_codes
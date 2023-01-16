#pragma once
#include "ship_protocol.hpp"

namespace chain_types
{
   using namespace eosio::ship_protocol;

   struct block_info
   {
      uint32_t block_num = {};
      eosio::checksum256 block_id = {};
      eosio::block_timestamp timestamp;
   };

   EOSIO_REFLECT(block_info, block_num, block_id, timestamp);
};  // namespace chain_types
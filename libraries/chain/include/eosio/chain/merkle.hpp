#pragma once
#include <eosio/chain/types.hpp>
#include <fc/io/raw.hpp>

namespace eosio::chain {

/**
 * Calculates the merkle root of a set of digests. Does not manipulate the digests.
 */
inline digest_type calculate_merkle( deque<digest_type> ids ) {
   if( 0 == ids.size() ) { return digest_type(); }

   while( ids.size() > 1 ) {
      if( ids.size() % 2 )
         ids.push_back(ids.back());

      for (size_t i = 0; i < ids.size() / 2; ++i) {
         ids[i] = digest_type::hash(std::make_pair(std::cref(ids[2 * i]), std::cref(ids[(2 * i) + 1])));
      }

      ids.resize(ids.size() / 2);
   }

   return ids.front();
}


} /// eosio::chain

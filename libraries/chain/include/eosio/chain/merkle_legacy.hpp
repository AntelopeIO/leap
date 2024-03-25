#pragma once
#include <eosio/chain/types.hpp>
#include <fc/io/raw.hpp>

namespace eosio::chain {

   inline digest_type make_canonical_left(const digest_type& val) {
      digest_type canonical_l = val;
      canonical_l._hash[0] &= 0xFFFFFFFFFFFFFF7FULL;
      return canonical_l;
   }

   inline digest_type make_canonical_right(const digest_type& val) {
      digest_type canonical_r = val;
      canonical_r._hash[0] |= 0x0000000000000080ULL;
      return canonical_r;
   }

   inline bool is_canonical_left(const digest_type& val) {
      return (val._hash[0] & 0x0000000000000080ULL) == 0;
   }

   inline bool is_canonical_right(const digest_type& val)  {
      return (val._hash[0] & 0x0000000000000080ULL) != 0;
   }


   inline auto make_canonical_pair(const digest_type& l, const digest_type& r) {
      return make_pair(make_canonical_left(l), make_canonical_right(r));
   };

   /**
    *  Calculates the merkle root of a set of digests, if ids is odd it will duplicate the last id.
    *  Uses make_canonical_pair which before hashing sets the first bit of the previous hashes
    *  to 0 or 1 to indicate the side it is on.
    */
   inline digest_type legacy_merkle( deque<digest_type> ids ) {
      if( 0 == ids.size() ) { return digest_type(); }

      while( ids.size() > 1 ) {
         if( ids.size() % 2 )
            ids.push_back(ids.back());

         for (size_t i = 0; i < ids.size() / 2; i++) {
            ids[i] = digest_type::hash(make_canonical_pair(ids[2 * i], ids[(2 * i) + 1]));
         }

         ids.resize(ids.size() / 2);
      }

      return ids.front();
   }

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

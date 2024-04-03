#pragma once
#include <eosio/chain/types.hpp>
#include <fc/io/raw.hpp>

namespace eosio::chain {

namespace detail {

   inline digest_type make_legacy_left_digest(const digest_type& val) {
      digest_type left = val;
      left._hash[0] &= 0xFFFFFFFFFFFFFF7FULL;
      return left;
   }

   inline digest_type make_legacy_right_digest(const digest_type& val) {
      digest_type right = val;
      right._hash[0] |= 0x0000000000000080ULL;
      return right;
   }

   inline bool is_legacy_left_digest(const digest_type& val) {
      return (val._hash[0] & 0x0000000000000080ULL) == 0;
   }

   inline bool is_legacy_right_digest(const digest_type& val)  {
      return (val._hash[0] & 0x0000000000000080ULL) != 0;
   }

   inline auto make_legacy_digest_pair(const digest_type& l, const digest_type& r) {
      return make_pair(make_legacy_left_digest(l), make_legacy_right_digest(r));
   };

} // namespace detail

/**
 *  Calculates the merkle root of a set of digests, if ids is odd it will duplicate the last id.
 *  Uses make_legacy_digest_pair which before hashing sets the first bit of the previous hashes
 *  to 0 or 1 to indicate the side it is on.
 */
inline digest_type calculate_merkle_legacy( deque<digest_type> ids ) {
   if( 0 == ids.size() ) { return digest_type(); }

   while( ids.size() > 1 ) {
      if( ids.size() % 2 )
         ids.push_back(ids.back());

      for (size_t i = 0; i < ids.size() / 2; i++) {
         ids[i] = digest_type::hash(detail::make_legacy_digest_pair(ids[2 * i], ids[(2 * i) + 1]));
      }

      ids.resize(ids.size() / 2);
   }

   return ids.front();
}

} /// eosio::chain

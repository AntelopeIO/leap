#pragma once
#include <eosio/chain/types.hpp>
#include <fc/io/raw.hpp>
#include <bit>

namespace eosio::chain {

#if 0
/**
 * Calculates the merkle root of a set of digests. Does not manipulate the digests.
 */
inline digest_type calculate_merkle( deque<digest_type> ids ) {
   if (ids.empty())
      return {};

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
#endif

namespace detail {

inline digest_type hash_combine(const digest_type& a, const digest_type& b) {
   return digest_type::hash(std::make_pair(std::cref(a), std::cref(b)));
}

// does not overwrite passed sequence
// ----------------------------------
template <class It>
requires std::is_same_v<typename std::iterator_traits<It>::value_type, std::decay_t<digest_type>>
inline digest_type calculate_merkle_pow2(const It& start, const It& end) {
   auto size = end - start;
   assert(size >= 2);
   assert(std::bit_floor(static_cast<size_t>(size)) == size);

   if (size == 2)
      return hash_combine(start[0], start[1]);
   else {
      auto mid = start + size / 2;
      return hash_combine(calculate_merkle_pow2(start, mid), calculate_merkle_pow2(mid, end));
   }
}


template <class It>
requires std::is_same_v<typename std::iterator_traits<It>::value_type, digest_type>
inline digest_type calculate_merkle(const It& start, const It& end) {
   assert(end >= start);
   auto size = end - start;
   if (size <= 1)
      return (size == 0) ? digest_type{} : *start;

   auto midpoint = std::bit_floor(static_cast<size_t>(size));
   if (size == midpoint)
      return calculate_merkle_pow2(start, end);

   auto mid = start + midpoint;
   return hash_combine(calculate_merkle_pow2(start, mid), calculate_merkle(mid, end));
}

// overwrites passed sequence
// --------------------------
template <class It>
requires std::is_same_v<typename std::iterator_traits<It>::value_type, digest_type>
inline digest_type calculate_merkle_2(const It& start, const It& end) {
   assert(end >= start);
   auto size = end - start;
   if (size <= 1)
      return (size == 0) ? digest_type{} : *start;

   auto midpoint = std::bit_floor(static_cast<size_t>(size));

   // accumulate the first 2**N digests into start[0]
   // -----------------------------------------------
   {
      auto remaining = midpoint;
      while (remaining > 1) {
         for (size_t i = 0; i < remaining / 2; ++i)
            start[i] = hash_combine(start[2 * i], start[(2 * i) + 1]);
         remaining /= 2;
      }
   }

   if (midpoint == size)
      return start[0];

   // accumulate the rest of the digests into start[1], recursion limited to
   // log2(size) so stack overflow is not a concern.
   // ----------------------------------------------------------------------
   start[1] = calculate_merkle_2(start + midpoint, end);

   // combine and return
   // ------------------
   return hash_combine(start[0], std::cref(start[1]));
}

}

inline digest_type calculate_merkle(const deque<digest_type>& ids) {
   return detail::calculate_merkle(ids.cbegin(), ids.cend());
}


} /// eosio::chain

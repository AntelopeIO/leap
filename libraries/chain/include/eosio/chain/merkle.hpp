#pragma once
#include <eosio/chain/types.hpp>
#include <fc/io/raw.hpp>
#include <bit>

namespace eosio::chain {

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

}

inline digest_type calculate_merkle(const deque<digest_type>& ids) {
   return detail::calculate_merkle(ids.cbegin(), ids.cend());
}


} /// eosio::chain

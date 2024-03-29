#pragma once
#include <eosio/chain/types.hpp>
#include <fc/io/raw.hpp>
#include <bit>

namespace eosio::chain {

namespace detail {

#if __cplusplus >= 202002L
   inline int      popcount(uint64_t x)  noexcept { return std::popcount(x); }
   inline uint64_t bit_floor(uint64_t x) noexcept { return std::bit_floor(x); }
#else
   inline int      popcount(uint64_t x)  noexcept { return __builtin_popcountll(x); }
   inline uint64_t bit_floor(uint64_t x) noexcept { return x == 0 ? 0ull : 1ull << (64 - 1 - __builtin_clzll(x)); }
#endif

inline digest_type hash_combine(const digest_type& a, const digest_type& b) {
   return digest_type::hash(std::make_pair(std::cref(a), std::cref(b)));
}

// does not overwrite passed sequence
// ----------------------------------
template <class It>
requires std::is_same_v<std::decay_t<typename std::iterator_traits<It>::value_type>, digest_type>
inline digest_type calculate_merkle_pow2(const It& start, const It& end) {
   auto size = end - start;
   assert(size >= 2);
   assert(detail::bit_floor(static_cast<size_t>(size)) == size);

   if (size == 2)
      return hash_combine(start[0], start[1]);
   else {
      auto mid = start + size / 2;
      return hash_combine(calculate_merkle_pow2(start, mid), calculate_merkle_pow2(mid, end));
   }
}


template <class It>
requires std::is_same_v<std::decay_t<typename std::iterator_traits<It>::value_type>, digest_type>
inline digest_type calculate_merkle(const It& start, const It& end) {
   assert(end >= start);
   auto size = end - start;
   if (size <= 1)
      return (size == 0) ? digest_type{} : *start;

   auto midpoint = detail::bit_floor(static_cast<size_t>(size));
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

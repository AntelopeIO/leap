#pragma once
#include <eosio/chain/types.hpp>
#include <eosio/chain/merkle.hpp>
#include <fc/io/raw.hpp>
#include <bit>

namespace eosio::chain {

class incremental_merkle_tree {
public:
   void append(const digest_type& digest) {
      assert(trees.size() == detail::popcount(mask));
      _append(digest, trees.end(), 0);
      assert(trees.size() == detail::popcount(mask));
   }

   digest_type get_root() const {
      if (!mask)
         return {};
      assert(!trees.empty());
      return _get_root(0);
   };

private:
   friend struct fc::reflector<incremental_merkle_tree>;
   using vec_it = std::vector<digest_type>::iterator;

   bool is_bit_set(size_t idx) const { return !!(mask & (1ull << idx)); }
   void set_bit(size_t idx)          { mask |= (1ull << idx); }
   void clear_bit(size_t idx)        { mask &= ~(1ull << idx); }

   digest_type _get_root(size_t idx) const {
      if (idx + 1 == trees.size())
         return trees[idx];
      return detail::hash_combine(trees[idx], _get_root(idx + 1)); // log2 recursion OK
   }

   // slot points to the current insertion point. *(slot-1) is the digest for the first bit set >= idx
   void _append(const digest_type& digest, vec_it slot, size_t idx) {
      if (is_bit_set(idx)) {
         assert(!trees.empty());
         if (!is_bit_set(idx+1)) {
            // next location is empty, replace its tree with new combination, same number of slots and one bits
            *(slot-1) = detail::hash_combine(*(slot-1), digest);
            clear_bit(idx);
            set_bit(idx+1);
         } else {
            assert(trees.size() >= 2);
            clear_bit(idx);
            clear_bit(idx+1);
            digest_type d = detail::hash_combine(*(slot-2), detail::hash_combine(*(slot-1), digest));
            trees.erase(slot-2, slot);
            _append(d, slot-2, idx+2); // log2 recursion OK, uses less than 5KB stack space for 4 billion digests
                                       // appended (or 0.25% of default 2MB thread stack size on Ubuntu)
         }
      } else {
         trees.insert(slot, digest);
         set_bit(idx);
      }
   }

   uint64_t                 mask = 0; // bits set signify tree present in trees vector.
                                      // least signif. bit set maps to smallest tree present.
   std::vector<digest_type> trees;    // digests representing power of 2 trees, smallest tree last
                                      // to minimize digest copying when appending.
                                      // invariant: `trees.size() == detail::popcount(mask)`
};

} /// eosio::chain

FC_REFLECT( eosio::chain::incremental_merkle_tree, (mask)(trees) );

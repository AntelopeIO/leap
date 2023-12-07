#pragma once
#include <eosio/chain/types.hpp>

namespace eosio { namespace chain {

   digest_type make_canonical_left(const digest_type& val);
   digest_type make_canonical_right(const digest_type& val);

   bool is_canonical_left(const digest_type& val);
   bool is_canonical_right(const digest_type& val);


   inline auto make_canonical_pair(const digest_type& l, const digest_type& r) {
      return make_pair(make_canonical_left(l), make_canonical_right(r));
   };

   /**
    *  Calculates the merkle root of a set of digests, if ids is odd it will duplicate the last id.
    *  Uses make_canonical_pair which before hashing sets the first bit of the previous hashes
    *  to 0 or 1 to indicate the side it is on.
    */
   digest_type canonical_merkle( deque<digest_type> ids );

   /**
    * Calculates the merkle root of a set of digests. Does not manipulate the digests.
    */
   digest_type calculate_merkle( deque<digest_type> ids );

} } /// eosio::chain

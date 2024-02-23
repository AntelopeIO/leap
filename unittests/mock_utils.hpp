#pragma once

#include "bhs_core.hpp"
#include <fc/bitutil.hpp>
#include <eosio/chain/hotstuff/finalizer.hpp>

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/global_fun.hpp>
#include <boost/multi_index/composite_key.hpp>

namespace mock_utils {

using namespace eosio;
using namespace eosio::chain;

inline block_id_type calc_id(block_id_type id, uint32_t block_number) {
   id._hash[0] &= 0xffffffff00000000;
   id._hash[0] += fc::endian_reverse_u32(block_number); // store the block num in the ID, 160 bits is plenty for the hash
   return id;
}


// ---------------------------------------------------------------------------------------
// emulations of block_header_state and fork_database sufficient for instantiating a
// finalizer.
// ---------------------------------------------------------------------------------------
struct bhs : bhs_core::core {
   block_id_type        block_id;
   block_id_type        previous_block;
   block_timestamp_type block_timestamp;

   uint32_t             block_num() const { return block_header::num_from_id(block_id); }
   const block_id_type& id()        const { return block_id; }
   const block_id_type& previous()  const { return previous_block; }
   block_timestamp_type timestamp() const { return block_timestamp; }
   bool                 is_valid()  const { return true; }
   uint32_t             irreversible_blocknum() const { return last_final_block_num(); }

   static bhs           genesis_bhs() {
      return bhs{ bhs_core::core{{bhs_core::qc_link{0, 0, false}}, {}, 0},
                  calc_id(fc::sha256::hash("genesis"), 0),
                  block_id_type{},
                  block_timestamp_type{0}
      };
   }
};

using bhsp = std::shared_ptr<bhs>;

// ---------------------------------------------------------------------------------------
struct bs : public bhs {
   bs() : bhs(genesis_bhs()) {}
   bs(const bhs& h) : bhs(h) {}

   uint32_t             block_num() const { return bhs::block_num(); }
   const block_id_type& id()        const { return bhs::id(); }
   const block_id_type& previous()  const { return bhs::previous(); }
   bool                 is_valid()  const { return true; }
   uint32_t             irreversible_blocknum() const { return bhs::irreversible_blocknum(); }

   explicit operator bhs_core::block_ref() const {
      return bhs_core::block_ref{id(), timestamp()};
   }
};

using bsp = std::shared_ptr<bs>;

// ---------------------------------------------------------------------------------------
struct proposal_t {
   uint32_t             block_number;
   std::string          proposer_name;
   block_timestamp_type block_timestamp;

   const std::string&   proposer()  const { return proposer_name; }
   block_timestamp_type timestamp() const { return block_timestamp; }
   uint32_t             block_num() const { return block_number; }

   block_id_type calculate_id() const
   {
      std::string   id_str = proposer_name + std::to_string(block_number);
      return calc_id(fc::sha256::hash(id_str.c_str()), block_number);
   }

   explicit operator bhs_core::block_ref() const {
      return bhs_core::block_ref{calculate_id(), timestamp()};
   }
};

// ---------------------------------------------------------------------------------------
bsp  make_bsp(const mock_utils::proposal_t& p, const bsp& previous, std::optional<bhs_core::qc_claim> claim = {}) {
   if (p.block_num() == 0) {
      // genesis block
      return std::make_shared<bs>();
   }
   assert(claim);
   bhs_core::block_ref ref(*previous);
   return std::make_shared<bs>(bhs{previous->next(ref, *claim), ref.block_id, previous->id(), p.timestamp() });
}

// ---------------------------------------------------------------------------------------
struct forkdb_t {
   using bsp              = bsp;
   using bs               = bsp::element_type;
   using bhsp             = bhsp;
   using bhs              = bhsp::element_type;
   using full_branch_type = std::vector<bhsp>;

   struct by_block_id;
   struct by_lib_block_num;
   struct by_prev;

   using fork_multi_index_type = boost::multi_index::multi_index_container<
       bsp,
      indexed_by<boost::multi_index::hashed_unique<tag<by_block_id>,
                                BOOST_MULTI_INDEX_CONST_MEM_FUN(bs, const block_id_type&, id),
                                std::hash<block_id_type>>,
                  ordered_non_unique<tag<by_prev>, const_mem_fun<bs, const block_id_type&, &bs::previous>>,
                  ordered_unique<tag<by_lib_block_num>,
                                 composite_key<bs,
                                               BOOST_MULTI_INDEX_CONST_MEM_FUN(bs, bool, is_valid),
                                               BOOST_MULTI_INDEX_CONST_MEM_FUN(bs, uint32_t, irreversible_blocknum),
                                               BOOST_MULTI_INDEX_CONST_MEM_FUN(bs, uint32_t, block_num),
                                               BOOST_MULTI_INDEX_CONST_MEM_FUN(bs, const block_id_type&, id)>,
                                 composite_key_compare<std::greater<bool>,
                                                       std::greater<uint32_t>,
                                                       std::greater<uint32_t>,
                                                       sha256_less>>>>;

   fork_multi_index_type  index;
   bsp                    head_;
   bsp                    root_;

   bsp root() const { return root_; }
   bsp head() const { return head_; }

   void add(const bsp& n) {
      auto inserted = index.insert(n);
      if( !inserted.second )
         return;
      if (index.size() == 1)
         root_= n;
      auto candidate = index.template get<by_lib_block_num>().begin();
      if( (*candidate)->is_valid() ) {
         head_ = *candidate;
      }
   }

   bsp get_block_impl(const block_id_type& id) const {
      auto itr = index.find( id );
      if( itr != index.end() )
         return *itr;
      return bsp();
   }

   full_branch_type fetch_full_branch(const block_id_type& id) const {
      full_branch_type result;
      result.reserve(10);
      for (auto s = get_block_impl(id); s; s = get_block_impl(s->previous())) {
         result.push_back(s);
      }
      return result;
   };

};






} // namespace mock_utils

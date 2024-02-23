#warning Remove undef NDEBUG for assert before RC
//Undefine NDEBUG to enable assertions in CICD.
#undef NDEBUG
#include <cassert>

#include <eosio/chain/finality_core.hpp>
#include <eosio/chain/block_header.hpp>

namespace eosio::chain {

/**
 *  @pre block_id is not null
 *  @returns the extracted block_num from block_id
 */
block_num_type block_ref::block_num() const {
   return block_header::num_from_id(block_id);
}

/**
 *  @pre none
 *
 *  @post returned core has current_block_num() == block_num
 *  @post returned core has latest_qc_claim() == {.block_num=block_num, .is_strong_qc=false}
 *  @post returned core has final_on_strong_qc_block_num == block_num
 *  @post returned core has last_final_block_num() == block_num
 */
finality_core finality_core::create_core_for_genesis_block(block_num_type block_num)
{
   return finality_core {
      .links                        = {
         qc_link{
            .source_block_num = block_num,
            .target_block_num = block_num,
            .is_link_strong    = false,
         },
      },
      .refs                         = {},
      .final_on_strong_qc_block_num = block_num,
   };

   // Invariants 1 to 7 can be easily verified to be satisfied for the returned core.
   // (And so, remaining invariants are also automatically satisfied.)
}

/**
 *  @pre this->links.empty() == false
 *  @post none
 *  @returns block number of the core
 */
block_num_type finality_core::current_block_num() const
{
   assert(!links.empty()); // Satisfied by invariant 1.

   return links.back().source_block_num;
}

/**
 *  @pre this->links.empty() == false
 *  @post none
 *  @returns last final block_num in respect to the core
 */
block_num_type finality_core::last_final_block_num() const
{
   assert(!links.empty()); // Satisfied by invariant 1.

   return links.front().target_block_num;
}

/**
 *  @pre this->links.empty() == false
 *  @post none
 *  @returns latest qc_claim made by the core
 */
qc_claim_t finality_core::latest_qc_claim() const
{
   assert(!links.empty()); // Satisfied by invariant 1.

   return qc_claim_t{.block_num = links.back().target_block_num, .is_strong_qc = links.back().is_link_strong};
}

/**
 *  @pre  all finality_core invariants
 *  @post same
 *  @returns boolean indicating whether `id` is an ancestor of this block
 */
bool finality_core::extends(const block_id_type& id) const {
   uint32_t block_num = block_header::num_from_id(id);
   if (block_num >= last_final_block_num() && block_num < current_block_num()) {
      const block_ref& ref = get_block_reference(block_num);
      return ref.block_id == id;
   }
   return false;
}

/**
 *  @pre last_final_block_num() <= block_num < current_block_num()
 *
 *  @post returned block_ref has block_num() == block_num
 */
const block_ref& finality_core::get_block_reference(block_num_type block_num) const
{
   assert(last_final_block_num() <= block_num); // Satisfied by precondition.
   assert(block_num < current_block_num()); // Satisfied by precondition.

   // If refs.empty() == true, then by invariant 3, current_block_num() == last_final_block_num(), 
   // and therefore it is impossible to satisfy the precondition. So going forward, it is safe to assume refs.empty() == false.

   const size_t ref_index = block_num - last_final_block_num();

   // By the precondition, 0 <= ref_index < (current_block_num() - last_final_block_num()).
   // Then, by invariant 8, 0 <= ref_index < refs.size().

   assert(ref_index < refs.size()); // Satisfied by justification above.

   return refs[ref_index];
   // By invariants 4 and 6, tail[ref_index].block_num() == block_num, which satisfies the post-condition.
}

/**
 *  @pre links.front().source_block_num <= block_num <= current_block_num()
 *
 *  @post returned qc_link has source_block_num == block_num
 */
const qc_link& finality_core::get_qc_link_from(block_num_type block_num) const
{
   assert(!links.empty()); // Satisfied by invariant 1.

   assert(links.front().source_block_num <= block_num); // Satisfied by precondition.
   assert(block_num <= current_block_num()); // Satisfied by precondition.

   const size_t link_index = block_num - links.front().source_block_num;

   // By the precondition, 0 <= link_index <= (current_block_num() - links.front().source_block_num).
   // Then, by invariant 9, 0 <= link_index <= links.size() - 1

   assert(link_index < links.size()); // Satisfied by justification above.

   return links[link_index];
   // By invariants 7, links[link_index].source_block_num == block_num, which satisfies the post-condition.
}

/**
 *  @pre c.latest_qc_claim().block_num <= most_recent_ancestor_with_qc.block_num <= c.current_block_num()
 * 
 *  @post std::get<0>(returned_value) <= std::get<1>(returned_value) <= std::get<2>(returned_value) <= most_recent_ancestor_with_qc.block_num
 *  @post c.last_final_block_num() <= std::get<0>(returned_value)
 *  @post c.links.front().source_block_num <= std::get<1>(returned_value)
 *  @post c.final_on_strong_qc_block_num <= std::get<2>(returned_value)
 */
std::tuple<block_num_type, block_num_type, block_num_type> get_new_block_numbers(const finality_core& c, const qc_claim_t& most_recent_ancestor_with_qc)
{
   assert(most_recent_ancestor_with_qc.block_num <= c.current_block_num()); // Satisfied by the precondition.

   // Invariant 2 of core guarantees that:
   // c.last_final_block_num() <= c.links.front().source_block_num <= c.final_on_strong_qc_block_num  <= c.latest_qc_claim().block_num

   assert(c.links.front().source_block_num <= most_recent_ancestor_with_qc.block_num); // Satisfied by invariant 2 of core and the precondition.

   // No changes on new claim of weak QC.
   if (!most_recent_ancestor_with_qc.is_strong_qc) {
      return {c.last_final_block_num(), c.links.front().source_block_num, c.final_on_strong_qc_block_num};
   }

   const auto& link1 = c.get_qc_link_from(most_recent_ancestor_with_qc.block_num);

   // By the post-condition of get_qc_link_from, link1.source_block_num == most_recent_ancestor_with_qc.block_num.
   // By the invariant on qc_link, link1.target_block_num <= link1.source_block_num.
   // Therefore, link1.target_block_num <= most_recent_ancestor_with_qc.block_num.
   // And also by the precondition, link1.target_block_num <= c.current_block_num().

   // If c.refs.empty() == true, then by invariant 3 of core, link1 == c.links.front() == c.links.back() and so
   // link1.target_block_num == c.current_block_num().

   // Otherwise, if c.refs.empty() == false, consider two cases.
   // Case 1: link1 != c.links.back()
   //   In this case, link1.target_block_num <= link1.source_block_num < c.links.back().source_block_num.
   //   The strict inequality is justified by invariant 7 of core.
   //   Therefore, link1.target_block_num < c.current_block_num().
   // Case 2: link1 == c.links.back()
   //   In this case, link1.target_block_num < link1.source_block_num == c.links.back().source_block_num.
   //   The strict inequality is justified because target_block_num and source_block_num of a qc_link can only be equal for a
   //   genesis block. And a link mapping genesis block number to genesis block number can only possibly exist for c.links.front().
   //   Therefore, link1.target_block_num < c.current_block_num().

   // There must exist some link, call it link0, within c.links where 
   // link0.target_block_num == c.final_on_strong_qc_block_num and link0.source_block_num <= c.latest_qc_claim().block_num.
   // By the precondition, link0.source_block_num <= most_recent_ancestor_with_qc.block_num.
   // If c.links.size() > 1, then by invariant 7 of core, link0.target_block_num <= link1.target_block_num.
   // Otherwise if c.links.size() == 1, then link0 == link1 and so link0.target_block_num == link1.target_block_num.
   // Therefore, c.final_on_strong_qc_block_num <= link1.target_block_num.

   assert(c.final_on_strong_qc_block_num <= link1.target_block_num); // Satisfied by justification above.

   // Finality does not advance if a better 3-chain is not found.
   if (!link1.is_link_strong || (link1.target_block_num < c.links.front().source_block_num)) {
      return {c.last_final_block_num(), c.links.front().source_block_num, link1.target_block_num};
   }

   const auto& link2 = c.get_qc_link_from(link1.target_block_num);

   // By the post-condition of get_qc_link_from, link2.source_block_num == link1.target_block_num.
   // By the invariant on qc_link, link2.target_block_num <= link2.source_block_num.
   // Therefore, link2.target_block_num <= link1.target_block_num.

   // Wherever link2 is found within c.links, it must be the case that c.links.front().target_block_num <= link2.target_block_num.
   // This is obvious if c.links.size() == 1 (even though the code would even not get to this point if c.links.size() == 1), and 
   // for the case where c.links.size() > 1, it is justified by invariant 7 of core.
   // Therefore, c.last_final_block_num() <= link2.target_block_num.

   return {link2.target_block_num, link2.source_block_num, link1.target_block_num}; 
}

core_metadata finality_core::next_metadata(const qc_claim_t& most_recent_ancestor_with_qc) const
{
   assert(most_recent_ancestor_with_qc.block_num <= current_block_num()); // Satisfied by precondition 1.
   assert(latest_qc_claim() <= most_recent_ancestor_with_qc); // Satisfied by precondition 2.

   const auto [new_last_final_block_num, new_links_front_source_block_num, new_final_on_strong_qc_block_num] = 
      get_new_block_numbers(*this, most_recent_ancestor_with_qc);

   (void)new_links_front_source_block_num;

   return core_metadata {
      .last_final_block_num = new_last_final_block_num,
      .final_on_strong_qc_block_num = new_final_on_strong_qc_block_num,
      .latest_qc_claim_block_num = most_recent_ancestor_with_qc.block_num,
   };
   // Post-conditions satisfied by post-conditions of get_new_block_numbers.
}

/**
 *  @pre current_block.block_num() == this->current_block_num()
 *  @pre If this->refs.empty() == false, then current_block is the block after the one referenced by this->refs.back()
 *  @pre this->latest_qc_claim().block_num <= most_recent_ancestor_with_qc.block_num <= this->current_block_num()
 *  @pre this->latest_qc_claim() <= most_recent_ancestor_with_qc
 *
 *  @post returned core has current_block_num() == this->current_block_num() + 1
 *  @post returned core has latest_qc_claim() == most_recent_ancestor_with_qc
 *  @post returned core has final_on_strong_qc_block_num >= this->final_on_strong_qc_block_num
 *  @post returned core has last_final_block_num() >= this->last_final_block_num()
 */
finality_core finality_core::next(const block_ref& current_block, const qc_claim_t& most_recent_ancestor_with_qc) const
{
   assert(current_block.block_num() == current_block_num()); // Satisfied by precondition 1.
   
   assert(refs.empty() || (refs.back().block_num() + 1 == current_block.block_num())); // Satisfied by precondition 2.
   assert(refs.empty() || (refs.back().timestamp < current_block.timestamp)); // Satisfied by precondition 2.

   assert(most_recent_ancestor_with_qc.block_num <= current_block_num()); // Satisfied by precondition 3.

   assert(latest_qc_claim() <= most_recent_ancestor_with_qc); // Satisfied by precondition 4.

   finality_core next_core;

   const auto [new_last_final_block_num, new_links_front_source_block_num, new_final_on_strong_qc_block_num] = 
      get_new_block_numbers(*this, most_recent_ancestor_with_qc);

   assert(new_last_final_block_num <= new_links_front_source_block_num); // Satisfied by post-condition 1 of get_new_block_numbers.
   assert(new_links_front_source_block_num <= new_final_on_strong_qc_block_num); // Satisfied by post-condition 1 of get_new_block_numbers.
   assert(new_final_on_strong_qc_block_num <= most_recent_ancestor_with_qc.block_num); // Satisfied by post-condition 1 of get_new_block_numbers.

   assert(last_final_block_num() <= new_last_final_block_num); // Satisfied by post-condition 2 of get_new_block_numbers.
   assert(links.front().source_block_num <= new_links_front_source_block_num); // Satisfied by post-condition 3 of get_new_block_numbers.
   assert(final_on_strong_qc_block_num <= new_final_on_strong_qc_block_num); // Satisfied by post-condition 4 of get_new_block_numbers.

   next_core.final_on_strong_qc_block_num = new_final_on_strong_qc_block_num;
   // Post-condition 3 is satisfied, assuming next_core will be returned without further modifications to next_core.final_on_strong_qc_block_num.

   // Post-condition 4 and invariant 2 will be satisfied when next_core.last_final_block_num() is updated to become new_last_final_block_num.

   // Setup next_core.links by garbage collecting unnecessary links and then adding the new QC link.
   {
      const size_t links_index = new_links_front_source_block_num - links.front().source_block_num;

      assert(links_index < links.size()); // Satisfied by justification in this->get_qc_link_from(new_links_front_source_block_num).

      // Garbage collect unnecessary links
      next_core.links = { links.cbegin() + links_index, links.cend() };

      assert(next_core.last_final_block_num() == new_last_final_block_num); // Satisfied by choice of links_index.

      // Also, by choice of links_index, at this point, next_core.links.back() == this->links.back().
      assert(next_core.links.back().source_block_num == current_block_num()); // Satisfied because last item in links has not yet changed.
      assert(next_core.links.back().target_block_num <= most_recent_ancestor_with_qc.block_num); // Satisfied because of above and precondition 3.

      // Add new link
      next_core.links.emplace_back(
         qc_link{
            .source_block_num = current_block_num() + 1, 
            .target_block_num = most_recent_ancestor_with_qc.block_num, // Guaranteed to be less than current_block_num() + 1.
            .is_link_strong = most_recent_ancestor_with_qc.is_strong_qc,
         });

      // Post-conditions 1, 2, and 4 are satisfied, assuming next_core will be returned without further modifications to next_core.links.

      // Invariants 1, 2, and 7 are satisfied for next_core.
   }

   // Setup next_core.refs by garbage collecting unnecessary block references in the refs and then adding the new block reference.
   {
      const size_t refs_index = new_last_final_block_num - last_final_block_num();

      // Using the justifications in new_block_nums, 0 <= ref_index <= (current_block_num() - last_final_block_num).
      // If refs.empty() == true, then by invariant 3, current_block_num() == last_final_block_num, and therefore ref_index == 0.
      // Otherwise if refs.empty() == false, the justification in new_block_nums provides the stronger inequality
      // 0 <= ref_index < (current_block_num() - last_final_block_num), which, using invariant 8, can be simplified to
      // 0 <= ref_index < refs.size().

      assert(!refs.empty() || (refs_index == 0)); // Satisfied by justification above.
      assert(refs.empty() || (refs_index < refs.size())); // Satisfied by justification above.

      // Garbage collect unnecessary block references
      next_core.refs = {refs.cbegin() + refs_index, refs.cend()};
      assert(refs.empty() || (next_core.refs.front().block_num() == new_last_final_block_num)); // Satisfied by choice of refs_index.

      // Add new block reference
      next_core.refs.emplace_back(current_block);

      // Invariant 3 is trivially satisfied for next_core because next_core.refs.empty() == false.

      // Invariant 5 is clearly satisfied for next_core because next_core.refs.back().block_num() == this->current_block_num()
      // and next_core.links.back().source_block_num == this->current_block_num() + 1.

      // Invariant 6 is also clearly satisfied for next_core because invariant 6 is satisfied for *this and the only
      // additional requirements needed are the ones provided by precondition 2.

      // If this->refs.empty() == true, then new_last_final_block_num == this->last_final_block_num() == this->current_block_num(),
      // and next_core.refs.size() == 1 and next_core.refs.front() == current_block.
      // And so, next_core.refs.front().block_num() == new_last_final_block_num.
      // If this->refs.empty() == false, then adding the current_block to the end does not change the fact that
      // next_core.refs.front().block_num() is still equal to new_last_final_block_num.

      assert(next_core.refs.front().block_num() == new_last_final_block_num); // Satisfied by justification above.

      // Because it was also already shown earlier that links.front().target_block_num == new_last_final_block_num,
      // then the justification above satisfies the remaining equalities needed to satisfy invariant 4 for next_core.

      // So, invariants 3 to 6 are now satisfied for next_core in addition to the invariants 1, 2, and 7 that were shown to be satisfied
      // earlier (and still remain satisfied since next_core.links and next_core.final_on_strong_qc_block_num have not changed).
   }

   return next_core;
   // Invariants 1 to 7 were verified to be satisfied for the current value of next_core at various points above. 
   // (And so, the remaining invariants for next_core are also automatically satisfied.)
}

} /// eosio::chain

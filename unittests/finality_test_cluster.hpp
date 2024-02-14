#pragma once

#include <eosio/chain/hotstuff/finalizer_authority.hpp>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#include <boost/test/unit_test.hpp>
#pragma GCC diagnostic pop
#include <eosio/testing/tester.hpp>

// Set up a test network which consists of 3 nodes:
//   * node1 produces blocks and pushes them to node2 and node3;
//     node1 votes the blocks it produces internally.
//   * node2 votes on the proposal sent by node1
//   * node3 votes on the proposal sent by node1
// Each node has one finalizer: node1 -- "node1"_n, node2 -- "node2"_n, node3 -- "node3"_n.
// Quorum is set to 2.
// After starup up, IF are activated on both nodes.
//
// APIs are provided to modify/delay/reoder/remove votes from node2 and node3 to node1.


class finality_test_cluster {
public:

   enum class vote_mode {
      strong,
      weak,
   };

   // Construct a test network and activate IF.
   finality_test_cluster();

   // node1 produces a block and pushes it to node2 and node3
   void produce_and_push_block();

   // send node2's vote identified by "index" in the collected votes
   eosio::chain::vote_status process_node2_vote(uint32_t vote_index, vote_mode mode = vote_mode::strong);

   // send node2's latest vote
   eosio::chain::vote_status process_node2_vote(vote_mode mode = vote_mode::strong);

   // send node3's vote identified by "index" in the collected votes
   eosio::chain::vote_status process_node3_vote(uint32_t vote_index, vote_mode mode = vote_mode::strong);

   // send node3's latest vote
   eosio::chain::vote_status process_node3_vote(vote_mode mode = vote_mode::strong);

   // returns true if node1's LIB has advanced
   bool node1_lib_advancing();

   // returns true if node2's LIB has advanced
   bool node2_lib_advancing();

   // returns true if node3's LIB has advanced
   bool node3_lib_advancing();

   // Produces a number of blocks and returns true if LIB is advancing.
   // This function can be only used at the end of a test as it clears
   // node2_votes and node3_votes when starting.
   bool produce_blocks_and_verify_lib_advancing();

   // Intentionally corrupt node2's vote's proposal_id and save the original vote
   void node2_corrupt_vote_proposal_id();

   // Intentionally corrupt node2's vote's finalizer_key and save the original vote
   void node2_corrupt_vote_finalizer_key();

   // Intentionally corrupt node2's vote's signature and save the original vote
   void node2_corrupt_vote_signature();

   // Restore node2's original vote
   void node2_restore_to_original_vote();

private:

   static constexpr size_t node1 = 0;  // node1 index in nodes array
   static constexpr size_t node2 = 1;  // node2 index in nodes array
   static constexpr size_t node3 = 2;  // node3 index in nodes array

   struct node_info {
      eosio::testing::tester                  node;
      uint32_t                                prev_lib_num{0};
      std::vector<eosio::chain::vote_message> votes;
   };
   std::array<node_info, 3> node;

   eosio::chain::vote_message node2_orig_vote;

   // sets up "node_index" node
   void setup_node(size_t index, eosio::chain::account_name local_finalizer);

   // returns true if LIB advances on "node_index" node
   bool lib_advancing(size_t node_index);

   // send "vote_index" vote on "node_index" node to node1
   eosio::chain::vote_status process_vote(size_t node_index, size_t vote_index, vote_mode mode);

   // send the latest vote on "node_index" node to node1
   eosio::chain::vote_status process_vote(size_t node_index, vote_mode mode);
};

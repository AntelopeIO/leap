#pragma once

#include <eosio/chain/hotstuff/finalizer_authority.hpp>
#include <fc/crypto/bls_private_key.hpp>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#include <boost/test/unit_test.hpp>
#pragma GCC diagnostic pop
#include <eosio/testing/tester.hpp>

// Set up a test network which consists of 3 nodes:
//   * node0 produces blocks and pushes them to node1 and node2;
//     node0 votes the blocks it produces internally.
//   * node1 votes on the proposal sent by node0
//   * node2 votes on the proposal sent by node0
// Each node has one finalizer: node0 -- "node0"_n, node1 -- "node1"_n, node2 -- "node2"_n.
// Quorum is set to 2.
// After starup up, IF are activated on both nodes.
//
// APIs are provided to modify/delay/reoder/remove votes from node1 and node2 to node0.


class finality_test_cluster {
public:

   enum class vote_mode {
      strong,
      weak,
   };

   // Construct a test network and activate IF.
   finality_test_cluster();

   // node0 produces a block and pushes it to node1 and node2
   void produce_and_push_block();

   // send node1's vote identified by "index" in the collected votes
   eosio::chain::vote_status process_node1_vote(uint32_t vote_index, vote_mode mode = vote_mode::strong);

   // send node1's latest vote
   eosio::chain::vote_status process_node1_vote(vote_mode mode = vote_mode::strong);

   // send node2's vote identified by "index" in the collected votes
   eosio::chain::vote_status process_node2_vote(uint32_t vote_index, vote_mode mode = vote_mode::strong);

   // send node2's latest vote
   eosio::chain::vote_status process_node2_vote(vote_mode mode = vote_mode::strong);

   // returns true if node0's LIB has advanced
   bool node0_lib_advancing();

   // returns true if node1's LIB has advanced
   bool node1_lib_advancing();

   // returns true if node2's LIB has advanced
   bool node2_lib_advancing();

   // Produces a number of blocks and returns true if LIB is advancing.
   // This function can be only used at the end of a test as it clears
   // node1_votes and node2_votes when starting.
   bool produce_blocks_and_verify_lib_advancing();

   // Intentionally corrupt node1's vote's proposal_id and save the original vote
   void node1_corrupt_vote_proposal_id();

   // Intentionally corrupt node1's vote's finalizer_key and save the original vote
   void node1_corrupt_vote_finalizer_key();

   // Intentionally corrupt node1's vote's signature and save the original vote
   void node1_corrupt_vote_signature();

   // Restore node1's original vote
   void node1_restore_to_original_vote();

private:

   struct node_info {
      eosio::testing::tester                  node;
      uint32_t                                prev_lib_num{0};
      std::vector<eosio::chain::vote_message> votes;
      fc::crypto::blslib::bls_private_key     priv_key;
   };

   std::array<node_info, 3> nodes;
   node_info& node0 = nodes[0];
   node_info& node1 = nodes[1];
   node_info& node2 = nodes[2];

   eosio::chain::vote_message node1_orig_vote;

   // sets up "node_index" node
   void setup_node(node_info& node, eosio::chain::account_name local_finalizer);

   // returns true if LIB advances on "node_index" node
   bool lib_advancing(node_info& node);

   // send "vote_index" vote on node to node0
   eosio::chain::vote_status process_vote(node_info& node, size_t vote_index, vote_mode mode);

   // send the latest vote on "node_index" node to node0
   eosio::chain::vote_status process_vote(node_info& node, vote_mode mode);
};

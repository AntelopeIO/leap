#include "finality_test_cluster.hpp"

/*
 * register test suite `finality_tests`
 */
BOOST_AUTO_TEST_SUITE(finality_tests)

// verify LIB advances with 2 finalizers voting.
BOOST_AUTO_TEST_CASE(two_votes) { try {
   finality_test_cluster cluster;

   for (auto i = 0; i < 3; ++i) {
      // node0 produces a block and pushes to node1 and node2
      cluster.produce_and_push_block();
      // process node1's votes only
      cluster.process_node1_vote();
      cluster.produce_and_push_block();

      // all nodes advance LIB
      BOOST_REQUIRE(cluster.node0_lib_advancing());
      BOOST_REQUIRE(cluster.node1_lib_advancing());
      BOOST_REQUIRE(cluster.node2_lib_advancing());
   }
} FC_LOG_AND_RETHROW() }

// verify LIB does not advances with finalizers not voting.
BOOST_AUTO_TEST_CASE(no_votes) { try {
   finality_test_cluster cluster;

   cluster.produce_and_push_block();
   cluster.node0_lib_advancing(); // reset
   cluster.node1_lib_advancing(); // reset
   cluster.node2_lib_advancing(); // reset
   for (auto i = 0; i < 3; ++i) {
      // node0 produces a block and pushes to node1 and node2
      cluster.produce_and_push_block();
      // process no votes
      cluster.produce_and_push_block();

      // all nodes don't advance LIB
      BOOST_REQUIRE(!cluster.node0_lib_advancing());
      BOOST_REQUIRE(!cluster.node1_lib_advancing());
      BOOST_REQUIRE(!cluster.node2_lib_advancing());
   }
} FC_LOG_AND_RETHROW() }

// verify LIB advances with all of the three finalizers voting
BOOST_AUTO_TEST_CASE(all_votes) { try {
   finality_test_cluster cluster;

   cluster.produce_and_push_block();
   for (auto i = 0; i < 3; ++i) {
      // process node1 and node2's votes
      cluster.process_node1_vote();
      cluster.process_node2_vote();
      // node0 produces a block and pushes to node1 and node2
      cluster.produce_and_push_block();

      // all nodes advance LIB
      BOOST_REQUIRE(cluster.node0_lib_advancing());
      BOOST_REQUIRE(cluster.node1_lib_advancing());
      BOOST_REQUIRE(cluster.node2_lib_advancing());
   }
} FC_LOG_AND_RETHROW() }

// verify LIB advances when votes conflict (strong first and followed by weak)
BOOST_AUTO_TEST_CASE(conflicting_votes_strong_first) { try {
   finality_test_cluster cluster;

   cluster.produce_and_push_block();
   for (auto i = 0; i < 3; ++i) {
      cluster.process_node1_vote();  // strong
      cluster.process_node2_vote(finality_test_cluster::vote_mode::weak); // weak
      cluster.produce_and_push_block();

      BOOST_REQUIRE(cluster.node0_lib_advancing());
      BOOST_REQUIRE(cluster.node1_lib_advancing());
      BOOST_REQUIRE(cluster.node2_lib_advancing());
   }
} FC_LOG_AND_RETHROW() }

// verify LIB advances when votes conflict (weak first and followed by strong)
BOOST_AUTO_TEST_CASE(conflicting_votes_weak_first) { try {
   finality_test_cluster cluster;

   cluster.produce_and_push_block();
   for (auto i = 0; i < 3; ++i) {
      cluster.process_node1_vote(finality_test_cluster::vote_mode::weak);  // weak
      cluster.process_node2_vote();  // strong
      cluster.produce_and_push_block();

      BOOST_REQUIRE(cluster.node0_lib_advancing());
      BOOST_REQUIRE(cluster.node1_lib_advancing());
      BOOST_REQUIRE(cluster.node2_lib_advancing());
   }
} FC_LOG_AND_RETHROW() }

// Verify a delayed vote works
BOOST_AUTO_TEST_CASE(one_delayed_votes) { try {
   finality_test_cluster cluster;

   // hold the vote for the first block to simulate delay
   cluster.produce_and_push_block();
   // LIB advanced on nodes because a new block was received
   BOOST_REQUIRE(cluster.node2_lib_advancing());
   BOOST_REQUIRE(cluster.node1_lib_advancing());

   cluster.produce_and_push_block();
   // vote block 0 (index 0) to make it have a strong QC,
   // prompting LIB advacing on node2
   cluster.process_node1_vote(0);
   cluster.produce_and_push_block();
   BOOST_REQUIRE(cluster.node2_lib_advancing());
   BOOST_REQUIRE(cluster.node1_lib_advancing());

   // block 1 (index 1) has the same QC claim as block 0. It cannot move LIB
   cluster.process_node1_vote(1);
   cluster.produce_and_push_block();
   BOOST_REQUIRE(!cluster.node2_lib_advancing());
   BOOST_REQUIRE(!cluster.node1_lib_advancing());

   // producing, pushing, and voting a new block makes LIB moving
   cluster.process_node1_vote();
   cluster.produce_and_push_block();
   BOOST_REQUIRE(cluster.node2_lib_advancing());
   BOOST_REQUIRE(cluster.node1_lib_advancing());

   BOOST_REQUIRE(cluster.produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }

// Verify 3 consecutive delayed votes work
BOOST_AUTO_TEST_CASE(three_delayed_votes) { try {
   finality_test_cluster cluster;

   // produce 4 blocks and hold the votes for the first 3 to simulate delayed votes
   // The 4 blocks have the same QC claim as no QCs are created because missing one vote
   for (auto i = 0; i < 4; ++i) {
      cluster.produce_and_push_block();
   }
   // LIB advanced on nodes because a new block was received
   BOOST_REQUIRE(cluster.node2_lib_advancing());
   BOOST_REQUIRE(cluster.node1_lib_advancing());

   cluster.produce_and_push_block();
   BOOST_REQUIRE(!cluster.node2_lib_advancing());
   BOOST_REQUIRE(!cluster.node1_lib_advancing());

   // vote block 0 (index 0) to make it have a strong QC,
   // prompting LIB advacing on nodes
   cluster.process_node1_vote(0);
   cluster.produce_and_push_block();
   BOOST_REQUIRE(cluster.node2_lib_advancing());
   BOOST_REQUIRE(cluster.node1_lib_advancing());

   // blocks 1 to 3 have the same QC claim as block 0. It cannot move LIB
   for (auto i=1; i < 4; ++i) {
      cluster.process_node1_vote(i);
      cluster.produce_and_push_block();
      BOOST_REQUIRE(!cluster.node2_lib_advancing());
      BOOST_REQUIRE(!cluster.node1_lib_advancing());
   }

   // producing, pushing, and voting a new block makes LIB moving
   cluster.process_node1_vote();
   cluster.produce_and_push_block();
   BOOST_REQUIRE(cluster.node2_lib_advancing());
   BOOST_REQUIRE(cluster.node1_lib_advancing());

   BOOST_REQUIRE(cluster.produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(out_of_order_votes) { try {
   finality_test_cluster cluster;

   // produce 3 blocks and hold the votes to simulate delayed votes
   // The 3 blocks have the same QC claim as no QCs are created because missing votes
   for (auto i = 0; i < 3; ++i) {
      cluster.produce_and_push_block();
   }

   // vote out of the order: the newest to oldest

   // vote block 2 (index 2) to make it have a strong QC,
   // prompting LIB advacing
   cluster.process_node1_vote(2);
   cluster.produce_and_push_block();
   BOOST_REQUIRE(cluster.node0_lib_advancing());
   BOOST_REQUIRE(cluster.node1_lib_advancing());

   // block 1 (index 1) has the same QC claim as block 2. It will not move LIB
   cluster.process_node1_vote(1);
   cluster.produce_and_push_block();
   BOOST_REQUIRE(!cluster.node0_lib_advancing());
   BOOST_REQUIRE(!cluster.node1_lib_advancing());

   // block 0 (index 0) has the same QC claim as block 2. It will not move LIB
   cluster.process_node1_vote(0);
   cluster.produce_and_push_block();
   BOOST_REQUIRE(!cluster.node0_lib_advancing());
   BOOST_REQUIRE(!cluster.node1_lib_advancing());

   // producing, pushing, and voting a new block makes LIB moving
   cluster.process_node1_vote();
   cluster.produce_and_push_block();
   BOOST_REQUIRE(cluster.node0_lib_advancing());
   BOOST_REQUIRE(cluster.node1_lib_advancing());

   BOOST_REQUIRE(cluster.produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }

// Verify a vote which was delayed by a large number of blocks does not cause any issues
BOOST_AUTO_TEST_CASE(long_delayed_votes) { try {
   finality_test_cluster cluster;

   // Produce and push a block, vote on it after a long delay.
   constexpr uint32_t delayed_vote_index = 0;
   cluster.produce_and_push_block();
   // The strong QC extension for prior block makes LIB advance on nodes
   BOOST_REQUIRE(cluster.node2_lib_advancing());
   BOOST_REQUIRE(cluster.node1_lib_advancing());

   cluster.produce_and_push_block();
   BOOST_REQUIRE(!cluster.node2_lib_advancing());
   BOOST_REQUIRE(!cluster.node1_lib_advancing());

   cluster.process_node1_vote();
   cluster.produce_and_push_block();
   // the vote makes a strong QC for the current block, prompting LIB advance on nodes
   BOOST_REQUIRE(cluster.node2_lib_advancing());
   BOOST_REQUIRE(cluster.node1_lib_advancing());

   for (auto i = 2; i < 100; ++i) {
      cluster.process_node1_vote();
      cluster.produce_and_push_block();
      BOOST_REQUIRE(cluster.node0_lib_advancing());
      BOOST_REQUIRE(cluster.node1_lib_advancing());
   }

   // Late vote does not cause any issues
   BOOST_REQUIRE_NO_THROW(cluster.process_node1_vote(delayed_vote_index));

   BOOST_REQUIRE(cluster.produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(lost_votes) { try {
   finality_test_cluster cluster;

   // Produce and push a block, never vote on it to simulate lost.
   // The block contains a strong QC extension for prior block
   cluster.produce_and_push_block();

   // The strong QC extension for prior block makes LIB advance on nodes
   BOOST_REQUIRE(cluster.node1_lib_advancing());
   BOOST_REQUIRE(cluster.node2_lib_advancing());

   cluster.produce_and_push_block();
   // The block is not voted, so no strong QC is created and LIB does not advance on nodes
   BOOST_REQUIRE(!cluster.node1_lib_advancing());
   BOOST_REQUIRE(!cluster.node2_lib_advancing());

   cluster.process_node1_vote();
   cluster.produce_and_push_block();

   // vote causes lib to advance
   BOOST_REQUIRE(cluster.node1_lib_advancing());
   BOOST_REQUIRE(cluster.node2_lib_advancing());

   BOOST_REQUIRE(cluster.produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(one_weak_vote) { try {
   finality_test_cluster cluster;

   // Produce and push a block
   cluster.produce_and_push_block();
   // Change the vote to a weak vote and process it
   cluster.process_node1_vote(0, finality_test_cluster::vote_mode::weak);
   // The strong QC extension for prior block makes LIB advance on node1
   BOOST_REQUIRE(cluster.node1_lib_advancing());
   BOOST_REQUIRE(cluster.node2_lib_advancing());

   cluster.produce_and_push_block();
   // A weak QC is created and LIB does not advance on node2
   BOOST_REQUIRE(!cluster.node2_lib_advancing());
   // no 2-chain was formed as prior block was not a strong block
   BOOST_REQUIRE(!cluster.node1_lib_advancing());

   cluster.process_node1_vote();
   cluster.produce_and_push_block();
   BOOST_REQUIRE(cluster.node1_lib_advancing());
   BOOST_REQUIRE(cluster.node2_lib_advancing());

   cluster.process_node1_vote();
   cluster.produce_and_push_block();
   // the vote makes a strong QC and a higher final_on_strong_qc,
   // prompting LIB advance on nodes
   BOOST_REQUIRE(cluster.node1_lib_advancing());
   BOOST_REQUIRE(cluster.node2_lib_advancing());

   // now a 3 chain has formed.
   BOOST_REQUIRE(cluster.produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(two_weak_votes) { try {
   finality_test_cluster cluster;

   // Produce and push a block
   cluster.produce_and_push_block();
   // The strong QC extension for prior block makes LIB advance on nodes
   BOOST_REQUIRE(cluster.node1_lib_advancing());
   BOOST_REQUIRE(cluster.node2_lib_advancing());

   // Change the vote to a weak vote and process it
   cluster.process_node1_vote(finality_test_cluster::vote_mode::weak);
   cluster.produce_and_push_block();
   // A weak QC cannot advance LIB on nodes
   BOOST_REQUIRE(!cluster.node2_lib_advancing());
   BOOST_REQUIRE(!cluster.node1_lib_advancing());

   cluster.process_node1_vote(finality_test_cluster::vote_mode::weak);
   cluster.produce_and_push_block();
   // A weak QC cannot advance LIB on node2
   BOOST_REQUIRE(!cluster.node2_lib_advancing());
   // no 2-chain was formed as prior block was not a strong block
   BOOST_REQUIRE(!cluster.node1_lib_advancing());

   cluster.process_node1_vote();
   cluster.produce_and_push_block();
   BOOST_REQUIRE(cluster.node2_lib_advancing());
   BOOST_REQUIRE(cluster.node1_lib_advancing());

   cluster.process_node1_vote();
   cluster.produce_and_push_block();
   BOOST_REQUIRE(cluster.node2_lib_advancing());
   BOOST_REQUIRE(cluster.node1_lib_advancing());

   // now a 3 chain has formed.
   BOOST_REQUIRE(cluster.produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(intertwined_weak_votes) { try {
   finality_test_cluster cluster;

   cluster.produce_and_push_block();
   BOOST_REQUIRE(cluster.node2_lib_advancing());
   BOOST_REQUIRE(cluster.node1_lib_advancing());

   // Weak vote
   cluster.process_node1_vote(finality_test_cluster::vote_mode::weak);
   cluster.produce_and_push_block();

   // The strong QC extension for prior block makes LIB advance on nodes
   BOOST_REQUIRE(!cluster.node2_lib_advancing());
   BOOST_REQUIRE(!cluster.node1_lib_advancing());

   // Strong vote
   cluster.process_node1_vote();
   cluster.produce_and_push_block();
   BOOST_REQUIRE(cluster.node2_lib_advancing());
   BOOST_REQUIRE(cluster.node1_lib_advancing());

   // Weak vote
   cluster.process_node1_vote(finality_test_cluster::vote_mode::weak);
   cluster.produce_and_push_block();
   // A weak QC cannot advance LIB on nodes
   BOOST_REQUIRE(!cluster.node2_lib_advancing());
   BOOST_REQUIRE(!cluster.node1_lib_advancing());

   // Strong vote
   cluster.process_node1_vote();
   cluster.produce_and_push_block();
   // the vote makes a strong QC for the current block, prompting LIB advance on node0
   BOOST_REQUIRE(cluster.node2_lib_advancing());
   BOOST_REQUIRE(cluster.node1_lib_advancing());

   // Strong vote
   cluster.process_node1_vote();
   cluster.produce_and_push_block();
   BOOST_REQUIRE(cluster.node2_lib_advancing());
   BOOST_REQUIRE(cluster.node1_lib_advancing());

   BOOST_REQUIRE(cluster.produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }

// Verify a combination of weak, delayed, lost votes still work
BOOST_AUTO_TEST_CASE(weak_delayed_lost_vote) { try {
   finality_test_cluster cluster;

   cluster.produce_and_push_block();
   BOOST_REQUIRE(cluster.node2_lib_advancing());
   BOOST_REQUIRE(cluster.node1_lib_advancing());

   // A weak vote
   cluster.process_node1_vote(finality_test_cluster::vote_mode::weak);
   cluster.produce_and_push_block();
   BOOST_REQUIRE(!cluster.node2_lib_advancing());
   BOOST_REQUIRE(!cluster.node1_lib_advancing());

   // A delayed vote (index 1)
   constexpr uint32_t delayed_index = 1; 
   cluster.produce_and_push_block();
   BOOST_REQUIRE(!cluster.node2_lib_advancing());
   BOOST_REQUIRE(!cluster.node1_lib_advancing());

   // A strong vote
   cluster.process_node1_vote();
   cluster.produce_and_push_block();
   BOOST_REQUIRE(cluster.node2_lib_advancing());
   BOOST_REQUIRE(cluster.node1_lib_advancing());

   // A lost vote
   cluster.produce_and_push_block();
   BOOST_REQUIRE(!cluster.node2_lib_advancing());
   BOOST_REQUIRE(!cluster.node1_lib_advancing());

   // The delayed vote arrives, does not advance lib because it is weak
   cluster.process_node1_vote(delayed_index);
   cluster.produce_and_push_block();
   BOOST_REQUIRE(!cluster.node2_lib_advancing());
   BOOST_REQUIRE(!cluster.node1_lib_advancing());

   // strong vote advances lib
   cluster.process_node1_vote();
   cluster.produce_and_push_block();
   BOOST_REQUIRE(cluster.node2_lib_advancing());
   BOOST_REQUIRE(cluster.node1_lib_advancing());

   BOOST_REQUIRE(cluster.produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }

// Verify a combination of delayed, weak, lost votes still work
BOOST_AUTO_TEST_CASE(delayed_strong_weak_lost_vote) { try {
   finality_test_cluster cluster;

   // A delayed vote (index 0)
   constexpr uint32_t delayed_index = 0; 
   cluster.produce_and_push_block();
   BOOST_REQUIRE(cluster.node2_lib_advancing());
   BOOST_REQUIRE(cluster.node1_lib_advancing());

   // A strong vote
   cluster.process_node1_vote();
   cluster.produce_and_push_block();
   BOOST_REQUIRE(cluster.node2_lib_advancing());
   BOOST_REQUIRE(cluster.node1_lib_advancing());

   // A weak vote
   cluster.process_node1_vote(finality_test_cluster::vote_mode::weak);
   cluster.produce_and_push_block();
   BOOST_REQUIRE(!cluster.node2_lib_advancing());
   BOOST_REQUIRE(!cluster.node1_lib_advancing());

   // A strong vote
   cluster.process_node1_vote();
   cluster.produce_and_push_block();
   BOOST_REQUIRE(cluster.node2_lib_advancing());
   BOOST_REQUIRE(cluster.node1_lib_advancing());

   // A lost vote
   cluster.produce_and_push_block();
   BOOST_REQUIRE(!cluster.node2_lib_advancing());
   BOOST_REQUIRE(!cluster.node1_lib_advancing());

   // The delayed vote arrives
   cluster.process_node1_vote(delayed_index);
   cluster.produce_and_push_block();
   BOOST_REQUIRE(!cluster.node2_lib_advancing());
   BOOST_REQUIRE(!cluster.node1_lib_advancing());

   cluster.process_node1_vote();
   cluster.produce_and_push_block();
   BOOST_REQUIRE(cluster.node2_lib_advancing());
   BOOST_REQUIRE(cluster.node1_lib_advancing());

   BOOST_REQUIRE(cluster.produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }

// verify duplicate votes do not affect LIB advancing
BOOST_AUTO_TEST_CASE(duplicate_votes) { try {
   finality_test_cluster cluster;

   cluster.produce_and_push_block();
   for (auto i = 0; i < 5; ++i) {
      cluster.process_node1_vote(i);
      // vote again to make it duplicate
      BOOST_REQUIRE(cluster.process_node1_vote(i) == eosio::chain::vote_status::duplicate);
      cluster.produce_and_push_block();

      // verify duplicate votes do not affect LIB advancing
      BOOST_REQUIRE(cluster.node2_lib_advancing());
      BOOST_REQUIRE(cluster.node1_lib_advancing());
   }
} FC_LOG_AND_RETHROW() }

// verify unknown_proposal votes are handled properly
BOOST_AUTO_TEST_CASE(unknown_proposal_votes) { try {
   finality_test_cluster cluster;

   // node0 produces a block and pushes to node1
   cluster.produce_and_push_block();
   // intentionally corrupt proposal_id in node1's vote
   cluster.node1_corrupt_vote_proposal_id();

   // process the corrupted vote
   cluster.process_node1_vote(0);
   BOOST_REQUIRE(cluster.process_node1_vote(0) == eosio::chain::vote_status::unknown_block);
   cluster.produce_and_push_block();
   BOOST_REQUIRE(cluster.node2_lib_advancing());

   // restore to original vote
   cluster.node1_restore_to_original_vote();

   // process the original vote. LIB should advance
   cluster.produce_and_push_block();
   cluster.process_node1_vote(0);

   BOOST_REQUIRE(cluster.produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }

// verify unknown finalizer_key votes are handled properly
BOOST_AUTO_TEST_CASE(unknown_finalizer_key_votes) { try {
   finality_test_cluster cluster;

   // node0 produces a block and pushes to node1
   cluster.produce_and_push_block();

   // intentionally corrupt finalizer_key in node1's vote
   cluster.node1_corrupt_vote_finalizer_key();

   // process the corrupted vote. LIB should not advance
   cluster.process_node1_vote(0);
   BOOST_REQUIRE(cluster.process_node1_vote(0) == eosio::chain::vote_status::unknown_public_key);

   // restore to original vote
   cluster.node1_restore_to_original_vote();

   // process the original vote. LIB should advance
   cluster.process_node1_vote(0);

   BOOST_REQUIRE(cluster.produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }

// verify corrupted signature votes are handled properly
BOOST_AUTO_TEST_CASE(corrupted_signature_votes) { try {
   finality_test_cluster cluster;

   // node0 produces a block and pushes to node1
   cluster.produce_and_push_block();

   // intentionally corrupt signature in node1's vote
   cluster.node1_corrupt_vote_signature();

   // process the corrupted vote. LIB should not advance
   BOOST_REQUIRE(cluster.process_node1_vote(0) == eosio::chain::vote_status::invalid_signature);

   // restore to original vote
   cluster.node1_restore_to_original_vote();

   // process the original vote. LIB should advance
   cluster.process_node1_vote();

   BOOST_REQUIRE(cluster.produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()
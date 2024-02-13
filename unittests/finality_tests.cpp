#include "finality_tests.hpp"

/*
 * register test suite `finality_tests`
 */
BOOST_AUTO_TEST_SUITE(finality_tests)

// verify LIB advances with 2 finalizers voting.
BOOST_AUTO_TEST_CASE(two_votes) { try {
   finality_tester tester;

   for (auto i = 0; i < 3; ++i) {
      // node1 produces a block and pushes to node2 and node3
      tester.produce_and_push_block();
      // process node2's votes only
      tester.process_node2_vote();

      // all nodes advance LIB
      BOOST_REQUIRE(tester.node1_lib_advancing());
      BOOST_REQUIRE(tester.node2_lib_advancing());
      BOOST_REQUIRE(tester.node3_lib_advancing());
   }
} FC_LOG_AND_RETHROW() }

// verify LIB advances with all of the three finalizers voting
BOOST_AUTO_TEST_CASE(all_votes) { try {
   finality_tester tester;

   for (auto i = 0; i < 3; ++i) {
      // node1 produces a block and pushes to node2 and node3
      tester.produce_and_push_block();
      // process node2 and node3's votes
      tester.process_node2_vote();
      tester.process_node3_vote();

      // all nodes advance LIB
      BOOST_REQUIRE(tester.node1_lib_advancing());
      BOOST_REQUIRE(tester.node2_lib_advancing());
      BOOST_REQUIRE(tester.node3_lib_advancing());
   }
} FC_LOG_AND_RETHROW() }

// verify LIB advances when votes conflict (strong first and followed by weak)
BOOST_AUTO_TEST_CASE(conflicting_votes_strong_first) { try {
   finality_tester tester;

   for (auto i = 0; i < 3; ++i) {
      tester.produce_and_push_block();
      tester.process_node2_vote();  // strong
      tester.process_node3_vote(finality_tester::vote_mode::weak); // weak

      BOOST_REQUIRE(tester.node1_lib_advancing());
      BOOST_REQUIRE(tester.node2_lib_advancing());
      BOOST_REQUIRE(tester.node3_lib_advancing());
   }
} FC_LOG_AND_RETHROW() }

// verify LIB advances when votes conflict (weak first and followed by strong)
BOOST_AUTO_TEST_CASE(conflicting_votes_weak_first) { try {
   finality_tester tester;

   for (auto i = 0; i < 3; ++i) {
      tester.produce_and_push_block();
      tester.process_node2_vote(finality_tester::vote_mode::weak);  // weak
      tester.process_node3_vote();  // strong

      BOOST_REQUIRE(tester.node1_lib_advancing());
      BOOST_REQUIRE(tester.node2_lib_advancing());
      BOOST_REQUIRE(tester.node3_lib_advancing());
   }
} FC_LOG_AND_RETHROW() }

// Verify a delayed vote works
BOOST_AUTO_TEST_CASE(one_delayed_votes) { try {
   finality_tester tester;

   // hold the vote for the first block to simulate delay
   tester.produce_and_push_block();
   BOOST_REQUIRE(!tester.node1_lib_advancing());
   // LIB advanced on node2 because a new block was received
   BOOST_REQUIRE(tester.node2_lib_advancing());

   tester.produce_and_push_block();
   // vote block 0 (index 0) to make it have a strong QC,
   // prompting LIB advacing on node1
   tester.process_node2_vote(0);
   BOOST_REQUIRE(tester.node1_lib_advancing());
   BOOST_REQUIRE(!tester.node2_lib_advancing());

   // block 1 (index 1) has the same QC claim as block 0. It cannot move LIB
   tester.process_node2_vote(1);
   BOOST_REQUIRE(!tester.node1_lib_advancing());
   BOOST_REQUIRE(!tester.node2_lib_advancing());

   // producing, pushing, and voting a new block makes LIB moving
   tester.produce_and_push_block();
   tester.process_node2_vote();
   BOOST_REQUIRE(tester.node1_lib_advancing());
   BOOST_REQUIRE(tester.node2_lib_advancing());

   BOOST_REQUIRE(tester.produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }

// Verify 3 consecutive delayed votes work
BOOST_AUTO_TEST_CASE(three_delayed_votes) { try {
   finality_tester tester;

   // produce 4 blocks and hold the votes for the first 3 to simulate delayed votes
   // The 4 blocks have the same QC claim as no QCs are created because missing one vote
   for (auto i = 0; i < 4; ++i) {
      tester.produce_and_push_block();
   }
   BOOST_REQUIRE(!tester.node1_lib_advancing());
   // LIB advanced on node2 because a new block was received
   BOOST_REQUIRE(tester.node2_lib_advancing());

   // vote block 0 (index 0) to make it have a strong QC,
   // prompting LIB advacing on node1
   tester.process_node2_vote(0);
   BOOST_REQUIRE(tester.node1_lib_advancing());
   BOOST_REQUIRE(!tester.node2_lib_advancing());

   // blocks 1 to 3 have the same QC claim as block 0. It cannot move LIB
   for (auto i=1; i < 4; ++i) {
      tester.process_node2_vote(i);
      BOOST_REQUIRE(!tester.node1_lib_advancing());
      BOOST_REQUIRE(!tester.node2_lib_advancing());
   }

   // producing, pushing, and voting a new block makes LIB moving
   tester.produce_and_push_block();
   tester.process_node2_vote();
   BOOST_REQUIRE(tester.node1_lib_advancing());
   BOOST_REQUIRE(tester.node2_lib_advancing());

   BOOST_REQUIRE(tester.produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(out_of_order_votes) { try {
   finality_tester tester;

   // produce 3 blocks and hold the votes to simulate delayed votes
   // The 3 blocks have the same QC claim as no QCs are created because missing votes
   for (auto i = 0; i < 3; ++i) {
      tester.produce_and_push_block();
   }

   // vote out of the order: the newest to oldest

   // vote block 2 (index 2) to make it have a strong QC,
   // prompting LIB advacing
   tester.process_node2_vote(2);
   BOOST_REQUIRE(tester.node1_lib_advancing());
   BOOST_REQUIRE(tester.node2_lib_advancing());

   // block 1 (index 1) has the same QC claim as block 2. It will not move LIB
   tester.process_node2_vote(1);
   BOOST_REQUIRE(!tester.node1_lib_advancing());
   BOOST_REQUIRE(!tester.node2_lib_advancing());

   // block 0 (index 0) has the same QC claim as block 2. It will not move LIB
   tester.process_node2_vote(0);
   BOOST_REQUIRE(!tester.node1_lib_advancing());
   BOOST_REQUIRE(!tester.node2_lib_advancing());

   // producing, pushing, and voting a new block makes LIB moving
   tester.produce_and_push_block();
   tester.process_node2_vote();
   BOOST_REQUIRE(tester.node1_lib_advancing());
   BOOST_REQUIRE(tester.node2_lib_advancing());

   BOOST_REQUIRE(tester.produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }

// Verify a vote which was delayed by a large number of blocks does not cause any issues
BOOST_AUTO_TEST_CASE(long_delayed_votes) { try {
   finality_tester tester;

   // Produce and push a block, vote on it after a long delay.
   constexpr uint32_t delayed_vote_index = 0;
   tester.produce_and_push_block();
   // The block is not voted, so no strong QC is created and LIB does not advance on node1
   BOOST_REQUIRE(!tester.node1_lib_advancing());
   // The strong QC extension for prior block makes LIB advance on node2
   BOOST_REQUIRE(tester.node2_lib_advancing());

   tester.produce_and_push_block();
   tester.process_node2_vote();
   // the vote makes a strong QC for the current block, prompting LIB advance on node1
   BOOST_REQUIRE(tester.node1_lib_advancing());
   // the block does not has a QC extension as prior block was not a strong block
   BOOST_REQUIRE(!tester.node2_lib_advancing());

   for (auto i = 2; i < 100; ++i) {
      tester.produce_and_push_block();
      tester.process_node2_vote();
      BOOST_REQUIRE(tester.node1_lib_advancing());
      BOOST_REQUIRE(tester.node2_lib_advancing());
   }

   // Late vote does not cause any issues
   BOOST_REQUIRE_NO_THROW(tester.process_node2_vote(delayed_vote_index));

   BOOST_REQUIRE(tester.produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(lost_votes) { try {
   finality_tester tester;

   // Produce and push a block, never vote on it to simulate lost.
   // The block contains a strong QC extension for prior block
   tester.produce_and_push_block();

   // The block is not voted, so no strong QC is created and LIB does not advance on node1
   BOOST_REQUIRE(!tester.node1_lib_advancing());
   // The strong QC extension for prior block makes LIB advance on node2
   BOOST_REQUIRE(tester.node2_lib_advancing());

   tester.produce_and_push_block();
   tester.process_node2_vote();

   // the vote makes a strong QC for the current block, prompting LIB advance on node1
   BOOST_REQUIRE(tester.node1_lib_advancing());
   // the block does not has a QC extension as prior block was not a strong block
   BOOST_REQUIRE(!tester.node2_lib_advancing());

   BOOST_REQUIRE(tester.produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(one_weak_vote) { try {
   finality_tester tester;

   // Produce and push a block
   tester.produce_and_push_block();
   // Change the vote to a weak vote and process it
   tester.process_node2_vote(0, finality_tester::vote_mode::weak);

   // A weak QC is created and LIB does not advance on node1
   BOOST_REQUIRE(!tester.node1_lib_advancing());
   // The strong QC extension for prior block makes LIB advance on node2
   BOOST_REQUIRE(tester.node2_lib_advancing());

   tester.produce_and_push_block();
   tester.process_node2_vote();
   // Even though the vote makes a strong QC for the current block,
   // its final_on_strong_qc_block_num is nullopt due to previous QC was weak.
   // Cannot advance LIB.
   BOOST_REQUIRE(!tester.node1_lib_advancing());
   // the block does not has a QC extension as prior block was not a strong block
   BOOST_REQUIRE(!tester.node2_lib_advancing());

   tester.produce_and_push_block();
   tester.process_node2_vote();
   BOOST_REQUIRE(tester.node1_lib_advancing());
   // the block does not has a QC extension as prior block was not a strong block
   BOOST_REQUIRE(!tester.node2_lib_advancing());

   tester.produce_and_push_block();
   tester.process_node2_vote();
   // the vote makes a strong QC and a higher final_on_strong_qc,
   // prompting LIB advance on node1
   BOOST_REQUIRE(tester.node1_lib_advancing());
   // the block does not has a QC extension as prior block was not a strong block
   BOOST_REQUIRE(tester.node2_lib_advancing());

   // now a 3 chain has formed.
   BOOST_REQUIRE(tester.produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(two_weak_votes) { try {
   finality_tester tester;

   // Produce and push a block
   tester.produce_and_push_block();
   // Change the vote to a weak vote and process it
   tester.process_node2_vote(finality_tester::vote_mode::weak);
   // A weak QC cannot advance LIB on node1
   BOOST_REQUIRE(!tester.node1_lib_advancing());
   // The strong QC extension for prior block makes LIB advance on node2
   BOOST_REQUIRE(tester.node2_lib_advancing());

   tester.produce_and_push_block();
   tester.process_node2_vote(finality_tester::vote_mode::weak);
   // A weak QC cannot advance LIB on node1
   BOOST_REQUIRE(!tester.node1_lib_advancing());
   // the block does not has a QC extension as prior block was not a strong block
   BOOST_REQUIRE(!tester.node2_lib_advancing());

   tester.produce_and_push_block();
   tester.process_node2_vote();
   // the vote makes a strong QC for the current block, prompting LIB advance on node1
   BOOST_REQUIRE(!tester.node1_lib_advancing());
   // the block does not has a QC extension as prior block was not a strong block
   BOOST_REQUIRE(!tester.node2_lib_advancing());

   tester.produce_and_push_block();
   tester.process_node2_vote();
   // the vote makes a strong QC for the current block, prompting LIB advance on node1
   BOOST_REQUIRE(tester.node1_lib_advancing());
   // the block does not has a QC extension as prior block was not a strong block
   BOOST_REQUIRE(!tester.node2_lib_advancing());

   // now a 3 chain has formed.
   BOOST_REQUIRE(tester.produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(interwined_weak_votes) { try {
   finality_tester tester;

   // Weak vote
   tester.produce_and_push_block();
   tester.process_node2_vote(finality_tester::vote_mode::weak);
   // A weak QC cannot advance LIB on node1
   BOOST_REQUIRE(!tester.node1_lib_advancing());
   // The strong QC extension for prior block makes LIB advance on node2
   BOOST_REQUIRE(tester.node2_lib_advancing());

   // Strong vote
   tester.produce_and_push_block();
   tester.process_node2_vote();
   // Even though the vote makes a strong QC for the current block,
   // its final_on_strong_qc_block_num is nullopt due to previous QC was weak.
   // Cannot advance LIB.
   BOOST_REQUIRE(!tester.node1_lib_advancing());
   // the block does not has a QC extension as prior block was not a strong block
   BOOST_REQUIRE(!tester.node2_lib_advancing());

   // Weak vote
   tester.produce_and_push_block();
   tester.process_node2_vote(finality_tester::vote_mode::weak);
   // A weak QC cannot advance LIB on node1
   BOOST_REQUIRE(!tester.node1_lib_advancing());
   // the block does not has a QC extension as prior block was not a strong block
   BOOST_REQUIRE(!tester.node2_lib_advancing());

   // Strong vote
   tester.produce_and_push_block();
   tester.process_node2_vote();
   // the vote makes a strong QC for the current block, prompting LIB advance on node1
   BOOST_REQUIRE(!tester.node1_lib_advancing());
   // the block does not has a QC extension as prior block was not a strong block
   BOOST_REQUIRE(!tester.node2_lib_advancing());

   // Strong vote
   tester.produce_and_push_block();
   tester.process_node2_vote();
   BOOST_REQUIRE(tester.node1_lib_advancing());
   BOOST_REQUIRE(!tester.node2_lib_advancing());

   BOOST_REQUIRE(tester.produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }

// Verify a combination of weak, delayed, lost votes still work
BOOST_AUTO_TEST_CASE(weak_delayed_lost_vote) { try {
   finality_tester tester;

   // A weak vote
   tester.produce_and_push_block();
   tester.process_node2_vote(finality_tester::vote_mode::weak);
   BOOST_REQUIRE(!tester.node1_lib_advancing());
   BOOST_REQUIRE(tester.node2_lib_advancing());

   // A delayed vote (index 1)
   constexpr uint32_t delayed_index = 1; 
   tester.produce_and_push_block();
   BOOST_REQUIRE(!tester.node1_lib_advancing());
   BOOST_REQUIRE(!tester.node2_lib_advancing());

   // A strong vote
   tester.produce_and_push_block();
   tester.process_node2_vote();
   // The vote makes a strong QC, but final_on_strong_qc is null.
   // Do not advance LIB
   BOOST_REQUIRE(!tester.node1_lib_advancing());
   // the block does not has a QC extension as prior block was not a strong block
   BOOST_REQUIRE(!tester.node2_lib_advancing());

   // A lost vote
   tester.produce_and_push_block();
   BOOST_REQUIRE(!tester.node1_lib_advancing());
   // the block does not has a QC extension as prior block was not a strong block
   BOOST_REQUIRE(!tester.node2_lib_advancing());

   // The delayed vote arrives
   tester.process_node2_vote(delayed_index);
   BOOST_REQUIRE(!tester.node1_lib_advancing());
   BOOST_REQUIRE(!tester.node2_lib_advancing());

   tester.produce_and_push_block();
   tester.process_node2_vote();
   BOOST_REQUIRE(tester.node1_lib_advancing());
   BOOST_REQUIRE(!tester.node2_lib_advancing());

   tester.produce_and_push_block();
   tester.process_node2_vote();
   BOOST_REQUIRE(tester.node1_lib_advancing());
   BOOST_REQUIRE(tester.node2_lib_advancing());

   BOOST_REQUIRE(tester.produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }

// Verify a combination of delayed, weak, lost votes still work
BOOST_AUTO_TEST_CASE(delayed_strong_weak_lost_vote) { try {
   finality_tester tester;

   // A delayed vote (index 0)
   constexpr uint32_t delayed_index = 0; 
   tester.produce_and_push_block();
   BOOST_REQUIRE(!tester.node1_lib_advancing());
   BOOST_REQUIRE(tester.node2_lib_advancing());

   // A strong vote
   tester.produce_and_push_block();
   tester.process_node2_vote();
   BOOST_REQUIRE(tester.node1_lib_advancing());
   BOOST_REQUIRE(!tester.node2_lib_advancing());

   // A weak vote
   tester.produce_and_push_block();
   tester.process_node2_vote(finality_tester::vote_mode::weak);
   BOOST_REQUIRE(!tester.node1_lib_advancing());
   BOOST_REQUIRE(tester.node2_lib_advancing());

   // A strong vote
   tester.produce_and_push_block();
   tester.process_node2_vote();
   // The vote makes a strong QC, but final_on_strong_qc is null.
   // LIB did not advance.
   BOOST_REQUIRE(!tester.node1_lib_advancing());
   // the block does not has a QC extension as prior block was not a strong block
   BOOST_REQUIRE(!tester.node2_lib_advancing());

   // A lost vote
   tester.produce_and_push_block();
   BOOST_REQUIRE(!tester.node1_lib_advancing());
   // the block does not has a QC extension as prior block was not a strong block
   BOOST_REQUIRE(!tester.node2_lib_advancing());

   // The delayed vote arrives
   tester.process_node2_vote(delayed_index);
   BOOST_REQUIRE(!tester.node1_lib_advancing());
   BOOST_REQUIRE(!tester.node2_lib_advancing());

   tester.produce_and_push_block();
   tester.process_node2_vote();
   BOOST_REQUIRE(tester.node1_lib_advancing());
   BOOST_REQUIRE(!tester.node2_lib_advancing());

   tester.produce_and_push_block();
   tester.process_node2_vote();
   BOOST_REQUIRE(tester.node1_lib_advancing());
   BOOST_REQUIRE(tester.node2_lib_advancing());

   BOOST_REQUIRE(tester.produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }

// verify duplicate votes do not affect LIB advancing
BOOST_AUTO_TEST_CASE(duplicate_votes) { try {
   finality_tester tester;

   for (auto i = 0; i < 5; ++i) {
      tester.produce_and_push_block();
      tester.process_node2_vote(i);

      // vote again to make it duplicate
      BOOST_REQUIRE(tester.process_node2_vote(i) == eosio::chain::vote_status::duplicate);

      // verify duplicate votes do not affect LIB advancing
      BOOST_REQUIRE(tester.node1_lib_advancing());
      BOOST_REQUIRE(tester.node2_lib_advancing());
   }
} FC_LOG_AND_RETHROW() }

// verify unknown_proposal votes are handled properly
BOOST_AUTO_TEST_CASE(unknown_proposal_votes) { try {
   finality_tester tester;

   // node1 produces a block and pushes to node2
   tester.produce_and_push_block();

   auto orig_vote = tester.node2_votes[0];

   // corrupt the vote 
   if( tester.node2_votes[0].proposal_id.data()[0] == 'a' ) {
      tester.node2_votes[0].proposal_id.data()[0] = 'b';
   } else {
      tester.node2_votes[0].proposal_id.data()[0] = 'a';
   }

   // process the corrupted vote. LIB should not advance
   tester.process_node2_vote(0);
   BOOST_REQUIRE(tester.process_node2_vote(0) == eosio::chain::vote_status::unknown_block);
   BOOST_REQUIRE(!tester.node1_lib_advancing());

   // process the original vote. LIB should advance
   tester.node2_votes[0] = orig_vote;
   tester.process_node2_vote(0);

   BOOST_REQUIRE(tester.produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }

// verify unknown finalizer_key votes are handled properly
BOOST_AUTO_TEST_CASE(unknown_finalizer_key_votes) { try {
   finality_tester tester;

   // node1 produces a block and pushes to node2
   tester.produce_and_push_block();

   auto orig_vote = tester.node2_votes[0];

   // corrupt the finalizer_key 
   if( tester.node2_votes[0].finalizer_key._pkey.x.d[0] == 1 ) {
      tester.node2_votes[0].finalizer_key._pkey.x.d[0] = 2;
   } else {
      tester.node2_votes[0].finalizer_key._pkey.x.d[0] = 1;
   }

   // process the corrupted vote. LIB should not advance
   tester.process_node2_vote(0);
   BOOST_REQUIRE(tester.process_node2_vote(0) == eosio::chain::vote_status::unknown_public_key);
   BOOST_REQUIRE(!tester.node1_lib_advancing());

   // process the original vote. LIB should advance
   tester.node2_votes[0] = orig_vote;
   tester.process_node2_vote(0);

   BOOST_REQUIRE(tester.produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }

// verify corrupted signature votes are handled properly
BOOST_AUTO_TEST_CASE(corrupted_signature_votes) { try {
   finality_tester tester;

   // node1 produces a block and pushes to node2
   tester.produce_and_push_block();

   auto orig_vote = tester.node2_votes[0];

   // corrupt the signature 
   if( tester.node2_votes[0].sig._sig.x.c0.d[0] == 1 ) {
      tester.node2_votes[0].sig._sig.x.c0.d[0] = 2;
   } else {
      tester.node2_votes[0].sig._sig.x.c0.d[0] = 1;
   }

   // process the corrupted vote. LIB should not advance
   BOOST_REQUIRE(tester.process_node2_vote(0) == eosio::chain::vote_status::invalid_signature);
   BOOST_REQUIRE(!tester.node1_lib_advancing());

   // process the original vote. LIB should advance
   tester.node2_votes[0] = orig_vote;
   tester.process_node2_vote();

   BOOST_REQUIRE(tester.produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()

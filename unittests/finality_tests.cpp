#include <eosio/chain/hotstuff/finalizer_authority.hpp>
#include <eosio/chain/exceptions.hpp>

#include <fc/exception/exception.hpp>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#include <boost/test/unit_test.hpp>
#pragma GCC diagnostic pop
#include <eosio/testing/tester.hpp>

using namespace eosio::testing;

/*
 * register test suite `finality_tests`
 */
BOOST_AUTO_TEST_SUITE(finality_tests)

// Set up a test cluster which consists of 3 nodes:
//   * node1 produces blocks and pushes them to node2 and node3;
//     node1 votes the blocks it produces internally.
//   * node2 votes on the proposal sent by node1
//   * node3 votes on the proposal sent by node1
// Each node has one finalizer: node1 -- "node1"_n, node2 -- "node2"_n, node3 -- "node3"_n.
// Quorum is set to 2.
// After starup up, IF are activated on both nodes.
//
// APIs are provided to modify/delay/reoder/remove votes from node2 and node3 to node1.

enum class vote_mode {
   strong,
   weak,
};

class tester_cluster {
public:

   // Construct a test cluster and activate IF.
   tester_cluster() {
      setup_node(node1, "node1"_n);
      setup_node(node2, "node2"_n);
      setup_node(node3, "node3"_n);

      // collect node2's votes
      node2.control->voted_block().connect( [&]( const eosio::chain::vote_message& vote ) {
         node2_votes.emplace_back(vote);
      });
      // collect node3's votes
      node3.control->voted_block().connect( [&]( const eosio::chain::vote_message& vote ) {
         node3_votes.emplace_back(vote);
      });

      // form a 3-chain to make LIB advacing on node1
      // node1's vote (internal voting) and node2's vote make the quorum
      for (auto i = 0; i < 3; ++i) {
         produce_and_push_block();
         process_node2_vote();
      }
      BOOST_REQUIRE(node1_lib_advancing());

      // QC extension in the block sent to node2 and node3 makes them LIB advancing
      produce_and_push_block();
      process_node2_vote();
      BOOST_REQUIRE(node2_lib_advancing());
      BOOST_REQUIRE(node3_lib_advancing());

      // clean up processed votes
      node2_votes.clear();
      node3_votes.clear();
      node1_prev_lib_bum = node1.control->if_irreversible_block_num();
      node2_prev_lib_bum = node2.control->if_irreversible_block_num();
      node3_prev_lib_bum = node3.control->if_irreversible_block_num();
   }

   // node1 produces a block and pushes it to node2 and node3
   void produce_and_push_block() {
      auto b = node1.produce_block();
      node2.push_block(b);
      node3.push_block(b);
   }

   // send a vote to node1
   vote_status process_vote(eosio::chain::vote_message& vote, vote_mode mode) {
      if( mode == vote_mode::strong ) {
         vote.strong = true;
      } else {
         vote.strong = false;
      }
      return node1.control->process_vote_message( vote );
   }

   // send node2's vote identified by "index" in the collected votes
   vote_status process_node2_vote(uint32_t index, vote_mode mode = vote_mode::strong) {
      FC_ASSERT( index < node2_votes.size(), "out of bound index in process_node2_vote" );
      return process_vote( node2_votes[index], mode );
   }

   // send node2's latest vote
   vote_status process_node2_vote(vote_mode mode = vote_mode::strong) {
      auto index = node2_votes.size() - 1;
      return process_vote( node2_votes[index], mode );
   }

   // send node3's vote identified by "index" in the collected votes
   vote_status process_node3_vote(uint32_t index, vote_mode mode = vote_mode::strong) {
      FC_ASSERT( index < node3_votes.size(), "out of bound index in process_node3_vote" );
      return process_vote( node3_votes[index], mode );
   }

   // send node3's latest vote
   vote_status process_node3_vote(vote_mode mode = vote_mode::strong) {
      auto index = node3_votes.size() - 1;
      return process_vote( node3_votes[index], mode );
   }

   // returns true if node1's LIB has advanced
   bool node1_lib_advancing() {
      return lib_advancing(node1.control->if_irreversible_block_num(), node1_prev_lib_bum);
   }

   // returns true if node2's LIB has advanced
   bool node2_lib_advancing() {
      return lib_advancing(node2.control->if_irreversible_block_num(), node2_prev_lib_bum);
   }

   // returns true if node3's LIB has advanced
   bool node3_lib_advancing() {
      return lib_advancing(node3.control->if_irreversible_block_num(), node3_prev_lib_bum);
   }

   // Produces a number of blocks and returns true if LIB is advancing.
   // This function can be only used at the end of a test as it clears
   // node2_votes and node3_votes when starting.
   bool produce_blocks_and_verify_lib_advancing() {
      // start from fresh
      node2_votes.clear();
      node3_votes.clear();

      for (auto i = 0; i < 3; ++i) {
         produce_and_push_block();
         process_node2_vote();
         if (!node1_lib_advancing() || !node2_lib_advancing() || !node3_lib_advancing()) {
            return false;
         }
      }

      return true;
   }

   std::vector<eosio::chain::vote_message> node2_votes;

private:
   bool lib_advancing(uint32_t curr_lib_num, uint32_t& prev_lib_num) {
      auto advancing = curr_lib_num > prev_lib_num;
      // update pre_lib_num for next time check
      prev_lib_num = curr_lib_num;
      return advancing;
   }

   eosio::testing::tester   node1;
   eosio::testing::tester   node2;
   eosio::testing::tester   node3;
   uint32_t                 node1_prev_lib_bum {0};
   uint32_t                 node2_prev_lib_bum {0};
   uint32_t                 node3_prev_lib_bum {0};
   std::vector<eosio::chain::vote_message> node3_votes;

   void setup_node(eosio::testing::tester& node, eosio::chain::account_name local_finalizer) {
      node.produce_block();
      node.produce_block();

      // activate hotstuff
      eosio::testing::base_tester::finalizer_policy_input policy_input = {
         .finalizers       = { {.name = "node1"_n, .weight = 1},
                               {.name = "node2"_n, .weight = 1},
                               {.name = "node3"_n, .weight = 1}},
         .threshold        = 2,
         .local_finalizers = {local_finalizer}
      };
      node.set_finalizers(policy_input);
      auto block = node.produce_block();

      // this block contains the header extension for the instant finality
      std::optional<eosio::chain::block_header_extension> ext = block->extract_header_extension(eosio::chain::instant_finality_extension::extension_id());
      BOOST_TEST(!!ext);
      std::optional<eosio::chain::finalizer_policy> fin_policy = std::get<eosio::chain::instant_finality_extension>(*ext).new_finalizer_policy;
      BOOST_TEST(!!fin_policy);
      BOOST_TEST(fin_policy->finalizers.size() == 3);
      BOOST_TEST(fin_policy->generation == 1);
   }
};

// verify LIB advances with 2 finalizers voting.
BOOST_AUTO_TEST_CASE(two_votes) { try {
   tester_cluster cluster;

   for (auto i = 0; i < 3; ++i) {
      // node1 produces a block and pushes to node2 and node3
      cluster.produce_and_push_block();
      // process node2's votes only
      cluster.process_node2_vote();

      // all nodes advance LIB
      BOOST_REQUIRE(cluster.node1_lib_advancing());
      BOOST_REQUIRE(cluster.node2_lib_advancing());
      BOOST_REQUIRE(cluster.node3_lib_advancing());
   }
} FC_LOG_AND_RETHROW() }

// verify LIB advances with all of the three finalizers voting
BOOST_AUTO_TEST_CASE(all_votes) { try {
   tester_cluster cluster;

   for (auto i = 0; i < 3; ++i) {
      // node1 produces a block and pushes to node2 and node3
      cluster.produce_and_push_block();
      // process node2 and node3's votes
      cluster.process_node2_vote();
      cluster.process_node3_vote();

      // all nodes advance LIB
      BOOST_REQUIRE(cluster.node1_lib_advancing());
      BOOST_REQUIRE(cluster.node2_lib_advancing());
      BOOST_REQUIRE(cluster.node3_lib_advancing());
   }
} FC_LOG_AND_RETHROW() }

// verify LIB advances when votes conflict (strong first and followed by weak)
BOOST_AUTO_TEST_CASE(conflicting_votes_strong_first) { try {
   tester_cluster cluster;

   for (auto i = 0; i < 3; ++i) {
      cluster.produce_and_push_block();
      cluster.process_node2_vote();  // strong
      cluster.process_node3_vote(vote_mode::weak); // weak

      BOOST_REQUIRE(cluster.node1_lib_advancing());
      BOOST_REQUIRE(cluster.node2_lib_advancing());
      BOOST_REQUIRE(cluster.node3_lib_advancing());
   }
} FC_LOG_AND_RETHROW() }

// verify LIB advances when votes conflict (weak first and followed by strong)
BOOST_AUTO_TEST_CASE(conflicting_votes_weak_first) { try {
   tester_cluster cluster;

   for (auto i = 0; i < 3; ++i) {
      cluster.produce_and_push_block();
      cluster.process_node2_vote(vote_mode::weak);  // weak
      cluster.process_node3_vote();  // strong

      BOOST_REQUIRE(cluster.node1_lib_advancing());
      BOOST_REQUIRE(cluster.node2_lib_advancing());
      BOOST_REQUIRE(cluster.node3_lib_advancing());
   }
} FC_LOG_AND_RETHROW() }

// Verify a delayed vote works
BOOST_AUTO_TEST_CASE(one_delayed_votes) { try {
   tester_cluster cluster;

   // hold the vote for the first block to simulate delay
   cluster.produce_and_push_block();
   BOOST_REQUIRE(!cluster.node1_lib_advancing());
   // LIB advanced on node2 because a new block was received
   BOOST_REQUIRE(cluster.node2_lib_advancing());

   cluster.produce_and_push_block();
   // vote block 0 (index 0) to make it have a strong QC,
   // prompting LIB advacing on node1
   cluster.process_node2_vote(0);
   BOOST_REQUIRE(cluster.node1_lib_advancing());
   BOOST_REQUIRE(!cluster.node2_lib_advancing());

   // block 1 (index 1) has the same QC claim as block 0. It cannot move LIB
   cluster.process_node2_vote(1);
   BOOST_REQUIRE(!cluster.node1_lib_advancing());
   BOOST_REQUIRE(!cluster.node2_lib_advancing());

   // producing, pushing, and voting a new block makes LIB moving
   cluster.produce_and_push_block();
   cluster.process_node2_vote();
   BOOST_REQUIRE(cluster.node1_lib_advancing());
   BOOST_REQUIRE(cluster.node2_lib_advancing());

   BOOST_REQUIRE(cluster.produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }

// Verify 3 consecutive delayed votes work
BOOST_AUTO_TEST_CASE(three_delayed_votes) { try {
   tester_cluster cluster;

   // produce 4 blocks and hold the votes for the first 3 to simulate delayed votes
   // The 4 blocks have the same QC claim as no QCs are created because missing one vote
   for (auto i = 0; i < 4; ++i) {
      cluster.produce_and_push_block();
   }
   BOOST_REQUIRE(!cluster.node1_lib_advancing());
   // LIB advanced on node2 because a new block was received
   BOOST_REQUIRE(cluster.node2_lib_advancing());

   // vote block 0 (index 0) to make it have a strong QC,
   // prompting LIB advacing on node1
   cluster.process_node2_vote(0);
   BOOST_REQUIRE(cluster.node1_lib_advancing());
   BOOST_REQUIRE(!cluster.node2_lib_advancing());

   // blocks 1 to 3 have the same QC claim as block 0. It cannot move LIB
   for (auto i=1; i < 4; ++i) {
      cluster.process_node2_vote(i);
      BOOST_REQUIRE(!cluster.node1_lib_advancing());
      BOOST_REQUIRE(!cluster.node2_lib_advancing());
   }

   // producing, pushing, and voting a new block makes LIB moving
   cluster.produce_and_push_block();
   cluster.process_node2_vote();
   BOOST_REQUIRE(cluster.node1_lib_advancing());
   BOOST_REQUIRE(cluster.node2_lib_advancing());

   BOOST_REQUIRE(cluster.produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(out_of_order_votes) { try {
   tester_cluster cluster;

   // produce 3 blocks and hold the votes to simulate delayed votes
   // The 3 blocks have the same QC claim as no QCs are created because missing votes
   for (auto i = 0; i < 3; ++i) {
      cluster.produce_and_push_block();
   }

   // vote out of the order: the newest to oldest

   // vote block 2 (index 2) to make it have a strong QC,
   // prompting LIB advacing
   cluster.process_node2_vote(2);
   BOOST_REQUIRE(cluster.node1_lib_advancing());
   BOOST_REQUIRE(cluster.node2_lib_advancing());

   // block 1 (index 1) has the same QC claim as block 2. It will not move LIB
   cluster.process_node2_vote(1);
   BOOST_REQUIRE(!cluster.node1_lib_advancing());
   BOOST_REQUIRE(!cluster.node2_lib_advancing());

   // block 0 (index 0) has the same QC claim as block 2. It will not move LIB
   cluster.process_node2_vote(0);
   BOOST_REQUIRE(!cluster.node1_lib_advancing());
   BOOST_REQUIRE(!cluster.node2_lib_advancing());

   // producing, pushing, and voting a new block makes LIB moving
   cluster.produce_and_push_block();
   cluster.process_node2_vote();
   BOOST_REQUIRE(cluster.node1_lib_advancing());
   BOOST_REQUIRE(cluster.node2_lib_advancing());

   BOOST_REQUIRE(cluster.produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }

// Verify a vote which was delayed by a large number of blocks does not cause any issues
BOOST_AUTO_TEST_CASE(long_delayed_votes) { try {
   tester_cluster cluster;

   // Produce and push a block, vote on it after a long delay.
   constexpr uint32_t delayed_vote_index = 0;
   cluster.produce_and_push_block();
   // The block is not voted, so no strong QC is created and LIB does not advance on node1
   BOOST_REQUIRE(!cluster.node1_lib_advancing());
   // The strong QC extension for prior block makes LIB advance on node2
   BOOST_REQUIRE(cluster.node2_lib_advancing());

   cluster.produce_and_push_block();
   cluster.process_node2_vote();
   // the vote makes a strong QC for the current block, prompting LIB advance on node1
   BOOST_REQUIRE(cluster.node1_lib_advancing());
   // the block does not has a QC extension as prior block was not a strong block
   BOOST_REQUIRE(!cluster.node2_lib_advancing());

   for (auto i = 2; i < 100; ++i) {
      cluster.produce_and_push_block();
      cluster.process_node2_vote();
      BOOST_REQUIRE(cluster.node1_lib_advancing());
      BOOST_REQUIRE(cluster.node2_lib_advancing());
   }

   // Late vote does not cause any issues
   BOOST_REQUIRE_NO_THROW(cluster.process_node2_vote(delayed_vote_index));

   BOOST_REQUIRE(cluster.produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(lost_votes) { try {
   tester_cluster cluster;

   // Produce and push a block, never vote on it to simulate lost.
   // The block contains a strong QC extension for prior block
   cluster.produce_and_push_block();

   // The block is not voted, so no strong QC is created and LIB does not advance on node1
   BOOST_REQUIRE(!cluster.node1_lib_advancing());
   // The strong QC extension for prior block makes LIB advance on node2
   BOOST_REQUIRE(cluster.node2_lib_advancing());

   cluster.produce_and_push_block();
   cluster.process_node2_vote();

   // the vote makes a strong QC for the current block, prompting LIB advance on node1
   BOOST_REQUIRE(cluster.node1_lib_advancing());
   // the block does not has a QC extension as prior block was not a strong block
   BOOST_REQUIRE(!cluster.node2_lib_advancing());

   BOOST_REQUIRE(cluster.produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(one_weak_vote) { try {
   tester_cluster cluster;

   // Produce and push a block
   cluster.produce_and_push_block();
   // Change the vote to a weak vote and process it
   cluster.process_node2_vote(0, vote_mode::weak);

   // A weak QC is created and LIB does not advance on node1
   BOOST_REQUIRE(!cluster.node1_lib_advancing());
   // The strong QC extension for prior block makes LIB advance on node2
   BOOST_REQUIRE(cluster.node2_lib_advancing());

   cluster.produce_and_push_block();
   cluster.process_node2_vote();
   // Even though the vote makes a strong QC for the current block,
   // its final_on_strong_qc_block_num is nullopt due to previous QC was weak.
   // Cannot advance LIB.
   BOOST_REQUIRE(!cluster.node1_lib_advancing());
   // the block does not has a QC extension as prior block was not a strong block
   BOOST_REQUIRE(!cluster.node2_lib_advancing());

   cluster.produce_and_push_block();
   cluster.process_node2_vote();
   BOOST_REQUIRE(cluster.node1_lib_advancing());
   // the block does not has a QC extension as prior block was not a strong block
   BOOST_REQUIRE(!cluster.node2_lib_advancing());

   cluster.produce_and_push_block();
   cluster.process_node2_vote();
   // the vote makes a strong QC and a higher final_on_strong_qc,
   // prompting LIB advance on node1
   BOOST_REQUIRE(cluster.node1_lib_advancing());
   // the block does not has a QC extension as prior block was not a strong block
   BOOST_REQUIRE(cluster.node2_lib_advancing());

   // now a 3 chain has formed.
   BOOST_REQUIRE(cluster.produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(two_weak_votes) { try {
   tester_cluster cluster;

   // Produce and push a block
   cluster.produce_and_push_block();
   // Change the vote to a weak vote and process it
   cluster.process_node2_vote(vote_mode::weak);
   // A weak QC cannot advance LIB on node1
   BOOST_REQUIRE(!cluster.node1_lib_advancing());
   // The strong QC extension for prior block makes LIB advance on node2
   BOOST_REQUIRE(cluster.node2_lib_advancing());

   cluster.produce_and_push_block();
   cluster.process_node2_vote(vote_mode::weak);
   // A weak QC cannot advance LIB on node1
   BOOST_REQUIRE(!cluster.node1_lib_advancing());
   // the block does not has a QC extension as prior block was not a strong block
   BOOST_REQUIRE(!cluster.node2_lib_advancing());

   cluster.produce_and_push_block();
   cluster.process_node2_vote();
   // the vote makes a strong QC for the current block, prompting LIB advance on node1
   BOOST_REQUIRE(!cluster.node1_lib_advancing());
   // the block does not has a QC extension as prior block was not a strong block
   BOOST_REQUIRE(!cluster.node2_lib_advancing());

   cluster.produce_and_push_block();
   cluster.process_node2_vote();
   // the vote makes a strong QC for the current block, prompting LIB advance on node1
   BOOST_REQUIRE(cluster.node1_lib_advancing());
   // the block does not has a QC extension as prior block was not a strong block
   BOOST_REQUIRE(!cluster.node2_lib_advancing());

   // now a 3 chain has formed.
   BOOST_REQUIRE(cluster.produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(interwined_weak_votes) { try {
   tester_cluster cluster;

   // Weak vote
   cluster.produce_and_push_block();
   cluster.process_node2_vote(vote_mode::weak);
   // A weak QC cannot advance LIB on node1
   BOOST_REQUIRE(!cluster.node1_lib_advancing());
   // The strong QC extension for prior block makes LIB advance on node2
   BOOST_REQUIRE(cluster.node2_lib_advancing());

   // Strong vote
   cluster.produce_and_push_block();
   cluster.process_node2_vote();
   // Even though the vote makes a strong QC for the current block,
   // its final_on_strong_qc_block_num is nullopt due to previous QC was weak.
   // Cannot advance LIB.
   BOOST_REQUIRE(!cluster.node1_lib_advancing());
   // the block does not has a QC extension as prior block was not a strong block
   BOOST_REQUIRE(!cluster.node2_lib_advancing());

   // Weak vote
   cluster.produce_and_push_block();
   cluster.process_node2_vote(vote_mode::weak);
   // A weak QC cannot advance LIB on node1
   BOOST_REQUIRE(!cluster.node1_lib_advancing());
   // the block does not has a QC extension as prior block was not a strong block
   BOOST_REQUIRE(!cluster.node2_lib_advancing());

   // Strong vote
   cluster.produce_and_push_block();
   cluster.process_node2_vote();
   // the vote makes a strong QC for the current block, prompting LIB advance on node1
   BOOST_REQUIRE(!cluster.node1_lib_advancing());
   // the block does not has a QC extension as prior block was not a strong block
   BOOST_REQUIRE(!cluster.node2_lib_advancing());

   // Strong vote
   cluster.produce_and_push_block();
   cluster.process_node2_vote();
   BOOST_REQUIRE(cluster.node1_lib_advancing());
   BOOST_REQUIRE(!cluster.node2_lib_advancing());

   BOOST_REQUIRE(cluster.produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }

// Verify a combination of weak, delayed, lost votes still work
BOOST_AUTO_TEST_CASE(weak_delayed_lost_vote) { try {
   tester_cluster cluster;

   // A weak vote
   cluster.produce_and_push_block();
   cluster.process_node2_vote(vote_mode::weak);
   BOOST_REQUIRE(!cluster.node1_lib_advancing());
   BOOST_REQUIRE(cluster.node2_lib_advancing());

   // A delayed vote (index 1)
   constexpr uint32_t delayed_index = 1; 
   cluster.produce_and_push_block();
   BOOST_REQUIRE(!cluster.node1_lib_advancing());
   BOOST_REQUIRE(!cluster.node2_lib_advancing());

   // A strong vote
   cluster.produce_and_push_block();
   cluster.process_node2_vote();
   // The vote makes a strong QC, but final_on_strong_qc is null.
   // Do not advance LIB
   BOOST_REQUIRE(!cluster.node1_lib_advancing());
   // the block does not has a QC extension as prior block was not a strong block
   BOOST_REQUIRE(!cluster.node2_lib_advancing());

   // A lost vote
   cluster.produce_and_push_block();
   BOOST_REQUIRE(!cluster.node1_lib_advancing());
   // the block does not has a QC extension as prior block was not a strong block
   BOOST_REQUIRE(!cluster.node2_lib_advancing());

   // The delayed vote arrives
   cluster.process_node2_vote(delayed_index);
   BOOST_REQUIRE(!cluster.node1_lib_advancing());
   BOOST_REQUIRE(!cluster.node2_lib_advancing());

   cluster.produce_and_push_block();
   cluster.process_node2_vote();
   BOOST_REQUIRE(cluster.node1_lib_advancing());
   BOOST_REQUIRE(!cluster.node2_lib_advancing());

   cluster.produce_and_push_block();
   cluster.process_node2_vote();
   BOOST_REQUIRE(cluster.node1_lib_advancing());
   BOOST_REQUIRE(cluster.node2_lib_advancing());

   BOOST_REQUIRE(cluster.produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }

// Verify a combination of delayed, weak, lost votes still work
BOOST_AUTO_TEST_CASE(delayed_strong_weak_lost_vote) { try {
   tester_cluster cluster;

   // A delayed vote (index 0)
   constexpr uint32_t delayed_index = 0; 
   cluster.produce_and_push_block();
   BOOST_REQUIRE(!cluster.node1_lib_advancing());
   BOOST_REQUIRE(cluster.node2_lib_advancing());

   // A strong vote
   cluster.produce_and_push_block();
   cluster.process_node2_vote();
   BOOST_REQUIRE(cluster.node1_lib_advancing());
   BOOST_REQUIRE(!cluster.node2_lib_advancing());

   // A weak vote
   cluster.produce_and_push_block();
   cluster.process_node2_vote(vote_mode::weak);
   BOOST_REQUIRE(!cluster.node1_lib_advancing());
   BOOST_REQUIRE(cluster.node2_lib_advancing());

   // A strong vote
   cluster.produce_and_push_block();
   cluster.process_node2_vote();
   // The vote makes a strong QC, but final_on_strong_qc is null.
   // LIB did not advance.
   BOOST_REQUIRE(!cluster.node1_lib_advancing());
   // the block does not has a QC extension as prior block was not a strong block
   BOOST_REQUIRE(!cluster.node2_lib_advancing());

   // A lost vote
   cluster.produce_and_push_block();
   BOOST_REQUIRE(!cluster.node1_lib_advancing());
   // the block does not has a QC extension as prior block was not a strong block
   BOOST_REQUIRE(!cluster.node2_lib_advancing());

   // The delayed vote arrives
   cluster.process_node2_vote(delayed_index);
   BOOST_REQUIRE(!cluster.node1_lib_advancing());
   BOOST_REQUIRE(!cluster.node2_lib_advancing());

   cluster.produce_and_push_block();
   cluster.process_node2_vote();
   BOOST_REQUIRE(cluster.node1_lib_advancing());
   BOOST_REQUIRE(!cluster.node2_lib_advancing());

   cluster.produce_and_push_block();
   cluster.process_node2_vote();
   BOOST_REQUIRE(cluster.node1_lib_advancing());
   BOOST_REQUIRE(cluster.node2_lib_advancing());

   BOOST_REQUIRE(cluster.produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }

// verify duplicate votes do not affect LIB advancing
BOOST_AUTO_TEST_CASE(duplicate_votes) { try {
   tester_cluster cluster;

   for (auto i = 0; i < 5; ++i) {
      cluster.produce_and_push_block();
      cluster.process_node2_vote(i);

      // vote again to make it duplicate
      BOOST_REQUIRE(cluster.process_node2_vote(i) == vote_status::duplicate);

      // verify duplicate votes do not affect LIB advancing
      BOOST_REQUIRE(cluster.node1_lib_advancing());
      BOOST_REQUIRE(cluster.node2_lib_advancing());
   }
} FC_LOG_AND_RETHROW() }

// verify unknown_proposal votes are handled properly
BOOST_AUTO_TEST_CASE(unknown_proposal_votes) { try {
   tester_cluster cluster;

   // node1 produces a block and pushes to node2
   cluster.produce_and_push_block();

   auto orig_vote = cluster.node2_votes[0];

   // corrupt the vote 
   if( cluster.node2_votes[0].proposal_id.data()[0] == 'a' ) {
      cluster.node2_votes[0].proposal_id.data()[0] = 'b';
   } else {
      cluster.node2_votes[0].proposal_id.data()[0] = 'a';
   }

   // process the corrupted vote. LIB should not advance
   cluster.process_node2_vote(0);
   BOOST_REQUIRE(cluster.process_node2_vote(0) == vote_status::unknown_block);
   BOOST_REQUIRE(!cluster.node1_lib_advancing());

   // process the original vote. LIB should advance
   cluster.node2_votes[0] = orig_vote;
   cluster.process_node2_vote(0);

   BOOST_REQUIRE(cluster.produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }

// verify unknown finalizer_key votes are handled properly
BOOST_AUTO_TEST_CASE(unknown_finalizer_key_votes) { try {
   tester_cluster cluster;

   // node1 produces a block and pushes to node2
   cluster.produce_and_push_block();

   auto orig_vote = cluster.node2_votes[0];

   // corrupt the finalizer_key 
   if( cluster.node2_votes[0].finalizer_key._pkey.x.d[0] == 1 ) {
      cluster.node2_votes[0].finalizer_key._pkey.x.d[0] = 2;
   } else {
      cluster.node2_votes[0].finalizer_key._pkey.x.d[0] = 1;
   }

   // process the corrupted vote. LIB should not advance
   cluster.process_node2_vote(0);
   BOOST_REQUIRE(cluster.process_node2_vote(0) == vote_status::unknown_public_key);
   BOOST_REQUIRE(!cluster.node1_lib_advancing());

   // process the original vote. LIB should advance
   cluster.node2_votes[0] = orig_vote;
   cluster.process_node2_vote(0);

   BOOST_REQUIRE(cluster.produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }

// verify corrupted signature votes are handled properly
BOOST_AUTO_TEST_CASE(corrupted_signature_votes) { try {
   tester_cluster cluster;

   // node1 produces a block and pushes to node2
   cluster.produce_and_push_block();

   auto orig_vote = cluster.node2_votes[0];

   // corrupt the signature 
   if( cluster.node2_votes[0].sig._sig.x.c0.d[0] == 1 ) {
      cluster.node2_votes[0].sig._sig.x.c0.d[0] = 2;
   } else {
      cluster.node2_votes[0].sig._sig.x.c0.d[0] = 1;
   }

   // process the corrupted vote. LIB should not advance
   BOOST_REQUIRE(cluster.process_node2_vote(0) == vote_status::invalid_signature);
   BOOST_REQUIRE(!cluster.node1_lib_advancing());

   // process the original vote. LIB should advance
   cluster.node2_votes[0] = orig_vote;
   cluster.process_node2_vote();

   BOOST_REQUIRE(cluster.produce_blocks_and_verify_lib_advancing());
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()

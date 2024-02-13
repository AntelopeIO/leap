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


class finality_tester {
public:

   enum class vote_mode {
      strong,
      weak,
   };

   // Construct a test network and activate IF.
   finality_tester() {
      using namespace eosio::testing;

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
      FC_ASSERT(node1_lib_advancing(), "LIB has not advanced on node1");

      // QC extension in the block sent to node2 and node3 makes them LIB advancing
      produce_and_push_block();
      process_node2_vote();
      FC_ASSERT(node2_lib_advancing(), "LIB has not advanced on node2");
      FC_ASSERT(node3_lib_advancing(), "LIB has not advanced on node3");

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
   eosio::chain::vote_status process_vote(eosio::chain::vote_message& vote, vote_mode mode) {
      if( mode == vote_mode::strong ) {
         vote.strong = true;
      } else {
         vote.strong = false;
      }
      return node1.control->process_vote_message( vote );
   }

   // send node2's vote identified by "index" in the collected votes
   eosio::chain::vote_status process_node2_vote(uint32_t index, vote_mode mode = vote_mode::strong) {
      FC_ASSERT( index < node2_votes.size(), "out of bound index in process_node2_vote" );
      return process_vote( node2_votes[index], mode );
   }

   // send node2's latest vote
   eosio::chain::vote_status process_node2_vote(vote_mode mode = vote_mode::strong) {
      auto index = node2_votes.size() - 1;
      return process_vote( node2_votes[index], mode );
   }

   // send node3's vote identified by "index" in the collected votes
   eosio::chain::vote_status process_node3_vote(uint32_t index, vote_mode mode = vote_mode::strong) {
      FC_ASSERT( index < node3_votes.size(), "out of bound index in process_node3_vote" );
      return process_vote( node3_votes[index], mode );
   }

   // send node3's latest vote
   eosio::chain::vote_status process_node3_vote(vote_mode mode = vote_mode::strong) {
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
      using namespace eosio::testing;

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

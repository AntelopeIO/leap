#include "finality_test_cluster.hpp"

// Construct a test network and activate IF.
finality_test_cluster::finality_test_cluster() {
   using namespace eosio::testing;

   setup_node(node0, "node0"_n);
   setup_node(node1, "node1"_n);
   setup_node(node2, "node2"_n);

   produce_and_push_block(); // make setfinalizer irreversible

   // collect node1's votes
   node1.node.control->voted_block().connect( [&]( const eosio::chain::vote_message& vote ) {
      node1.votes.emplace_back(vote);
   });
   // collect node2's votes
   node2.node.control->voted_block().connect( [&]( const eosio::chain::vote_message& vote ) {
      node2.votes.emplace_back(vote);
   });

   // form a 3-chain to make LIB advacing on node0
   // node0's vote (internal voting) and node1's vote make the quorum
   for (auto i = 0; i < 3; ++i) {
      produce_and_push_block();
      process_node1_vote();
   }
   FC_ASSERT(node0_lib_advancing(), "LIB has not advanced on node0");

   // QC extension in the block sent to node1 and node2 makes them LIB advancing
   produce_and_push_block();
   process_node1_vote();
   FC_ASSERT(node1_lib_advancing(), "LIB has not advanced on node1");
   FC_ASSERT(node2_lib_advancing(), "LIB has not advanced on node2");

   // clean up processed votes
   for (auto& n : nodes) {
      n.votes.clear();
      n.prev_lib_num = n.node.control->if_irreversible_block_num();
   }
}

// node0 produces a block and pushes it to node1 and node2
void finality_test_cluster::produce_and_push_block() {
   auto b = node0.node.produce_block();
   node1.node.push_block(b);
   node2.node.push_block(b);
}

// send node1's vote identified by "vote_index" in the collected votes
eosio::chain::vote_status finality_test_cluster::process_node1_vote(uint32_t vote_index, vote_mode mode) {
   return process_vote( node1, vote_index, mode );
}

// send node1's latest vote
eosio::chain::vote_status finality_test_cluster::process_node1_vote(vote_mode mode) {
   return process_vote( node1, mode );
}

// send node2's vote identified by "vote_index" in the collected votes
eosio::chain::vote_status finality_test_cluster::process_node2_vote(uint32_t vote_index, vote_mode mode) {
   return process_vote( node2, vote_index, mode );
}

// send node2's latest vote
eosio::chain::vote_status finality_test_cluster::process_node2_vote(vote_mode mode) {
   return process_vote( node2, mode );
}

// returns true if node0's LIB has advanced
bool finality_test_cluster::node0_lib_advancing() {
   return lib_advancing(node0);
}

// returns true if node1's LIB has advanced
bool finality_test_cluster::node1_lib_advancing() {
   return lib_advancing(node1);
}

// returns true if node2's LIB has advanced
bool finality_test_cluster::node2_lib_advancing() {
   return lib_advancing(node2);
}

// Produces a number of blocks and returns true if LIB is advancing.
// This function can be only used at the end of a test as it clears
// node1_votes and node2_votes when starting.
bool finality_test_cluster::produce_blocks_and_verify_lib_advancing() {
   // start from fresh
   node1.votes.clear();
   node2.votes.clear();

   for (auto i = 0; i < 3; ++i) {
      produce_and_push_block();
      process_node1_vote();
      produce_and_push_block();
      if (!node0_lib_advancing() || !node1_lib_advancing() || !node2_lib_advancing()) {
         return false;
      }
   }

   return true;
}

void finality_test_cluster::node1_corrupt_vote_proposal_id() {
   node1_orig_vote = node1.votes[0];

   if( node1.votes[0].block_id.data()[0] == 'a' ) {
      node1.votes[0].block_id.data()[0] = 'b';
   } else {
      node1.votes[0].block_id.data()[0] = 'a';
   }
}

void finality_test_cluster::node1_corrupt_vote_finalizer_key() {
   node1_orig_vote = node1.votes[0];

   // corrupt the finalizer_key (manipulate so it is different)
   auto g1 = node1.votes[0].finalizer_key.jacobian_montgomery_le();
   g1 = bls12_381::aggregate_public_keys(std::array{g1, g1});
   auto affine = g1.toAffineBytesLE(bls12_381::from_mont::yes);
   node1.votes[0].finalizer_key = fc::crypto::blslib::bls_public_key(affine);
}

void finality_test_cluster::node1_corrupt_vote_signature() {
   node1_orig_vote = node1.votes[0];

   // corrupt the signature
   auto g2 = node1.votes[0].sig.jacobian_montgomery_le();
   g2 = bls12_381::aggregate_signatures(std::array{g2, g2});
   auto affine = g2.toAffineBytesLE(bls12_381::from_mont::yes);
   node1.votes[0].sig = fc::crypto::blslib::bls_signature(affine);
}

void finality_test_cluster::node1_restore_to_original_vote() {
   node1.votes[0] = node1_orig_vote;
}

bool finality_test_cluster::lib_advancing(node_info& node) {
   auto curr_lib_num = node.node.control->if_irreversible_block_num();
   auto advancing = curr_lib_num > node.prev_lib_num;
   // update pre_lib_num for next time check
   node.prev_lib_num = curr_lib_num;
   return advancing;
}

// private methods follow
void finality_test_cluster::setup_node(node_info& node, eosio::chain::account_name local_finalizer) {
   using namespace eosio::testing;

   node.node.produce_block();
   node.node.produce_block();

   // activate hotstuff
   eosio::testing::base_tester::finalizer_policy_input policy_input = {
      .finalizers       = { {.name = "node0"_n, .weight = 1},
                            {.name = "node1"_n, .weight = 1},
                            {.name = "node2"_n, .weight = 1}},
      .threshold        = 2,
      .local_finalizers = {local_finalizer}
   };

   auto [trace_ptr, priv_keys] = node.node.set_finalizers(policy_input);
   FC_ASSERT( priv_keys.size() == 1, "number of private keys should be 1" );
   node.priv_key = priv_keys[0];  // we only have one private key

   auto block = node.node.produce_block();

   // this block contains the header extension for the instant finality
   std::optional<eosio::chain::block_header_extension> ext = block->extract_header_extension(eosio::chain::instant_finality_extension::extension_id());
   BOOST_TEST(!!ext);
   std::optional<eosio::chain::finalizer_policy> fin_policy = std::get<eosio::chain::instant_finality_extension>(*ext).new_finalizer_policy;
   BOOST_TEST(!!fin_policy);
   BOOST_TEST(fin_policy->finalizers.size() == 3);
   BOOST_TEST(fin_policy->generation == 1);
}

// send a vote to node0
eosio::chain::vote_status finality_test_cluster::process_vote(node_info& node, size_t vote_index, vote_mode mode) {
   FC_ASSERT( vote_index < node.votes.size(), "out of bound index in process_vote" );
   auto& vote = node.votes[vote_index];
   if( mode == vote_mode::strong ) {
      vote.strong = true;
   } else {
      vote.strong = false;

      // fetch the strong digest
      auto strong_digest = node.node.control->get_strong_digest_by_id(vote.block_id);
      // convert the strong digest to weak and sign it
      vote.sig = node.priv_key.sign(eosio::chain::create_weak_digest(strong_digest));
   }

   return node0.node.control->process_vote_message( vote );
}

eosio::chain::vote_status finality_test_cluster::process_vote(node_info& node, vote_mode mode) {
   auto vote_index = node.votes.size() - 1;
   return process_vote( node, vote_index, mode );
}

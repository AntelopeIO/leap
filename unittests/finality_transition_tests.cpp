#include <boost/test/unit_test.hpp>
#include <eosio/testing/tester.hpp>

using namespace eosio::chain;
using namespace eosio::testing;

BOOST_AUTO_TEST_SUITE(finality_transition_tests)

// test set_finalizer host function serialization and tester set_finalizers
BOOST_AUTO_TEST_CASE(set_finalizer_test) { try {
   validating_tester t;

   uint32_t curr_lib = 0;
   t.control->irreversible_block().connect([&](const block_signal_params& t) {
      const auto& [ block, id ] = t;
      curr_lib = block->block_num();
   });

   t.produce_block();

   // Create finalizer accounts
   vector<account_name> finalizers = {
      "inita"_n, "initb"_n, "initc"_n, "initd"_n, "inite"_n, "initf"_n, "initg"_n,
      "inith"_n, "initi"_n, "initj"_n, "initk"_n, "initl"_n, "initm"_n, "initn"_n,
      "inito"_n, "initp"_n, "initq"_n, "initr"_n, "inits"_n, "initt"_n, "initu"_n
   };

   t.create_accounts(finalizers);
   t.produce_block();

   // activate hotstuff
   t.set_finalizers(finalizers);
   auto block = t.produce_block(); // this block contains the header extension for the instant finality

   std::optional<block_header_extension> ext = block->extract_header_extension(instant_finality_extension::extension_id());
   BOOST_TEST(!!ext);
   std::optional<finalizer_policy> fin_policy = std::get<instant_finality_extension>(*ext).new_finalizer_policy;
   BOOST_TEST(!!fin_policy);
   BOOST_TEST(fin_policy->finalizers.size() == finalizers.size());
   BOOST_TEST(fin_policy->generation == 1u);
   BOOST_TEST(fin_policy->threshold == finalizers.size() / 3 * 2 + 1);
   // currently transition happens immediately after set_finalizer block
   // Need to update after https://github.com/AntelopeIO/leap/issues/2057

   block = t.produce_block(); // hotstuff now active
   BOOST_TEST(block->confirmed == 0);
   auto fb = t.control->fetch_block_by_id(block->calculate_id());
   BOOST_REQUIRE(!!fb);
   BOOST_TEST(fb == block);
   ext = fb->extract_header_extension(instant_finality_extension::extension_id());
   BOOST_REQUIRE(ext);

   // and another on top of a instant-finality block
   block = t.produce_block();
   auto lib_at_transition = curr_lib;
   BOOST_TEST(block->confirmed == 0);
   fb = t.control->fetch_block_by_id(block->calculate_id());
   BOOST_REQUIRE(!!fb);
   BOOST_TEST(fb == block);
   ext = fb->extract_header_extension(instant_finality_extension::extension_id());
   BOOST_REQUIRE(ext);

   // Local votes are signed asychronously. They can be delayed.
   // Leave room for the delay.
   for (auto i = 0; i < 500; ++i) {
      t.produce_block();
      if (curr_lib > lib_at_transition)
         break;
   }
   BOOST_CHECK_GT(curr_lib, lib_at_transition);
} FC_LOG_AND_RETHROW() }

void test_finality_transition(const vector<account_name>& accounts, const base_tester::finalizer_policy_input& input, bool lib_advancing_expected) {
   validating_tester t;

   uint32_t curr_lib = 0;
   t.control->irreversible_block().connect([&](const block_signal_params& t) {
      const auto& [ block, id ] = t;
      curr_lib = block->block_num();
   });

   t.produce_block();

   // Create finalizer accounts
   t.create_accounts(accounts);
   t.produce_block();

   // activate hotstuff
   t.set_finalizers(input);
   auto block = t.produce_block(); // this block contains the header extension for the instant finality

   std::optional<block_header_extension> ext = block->extract_header_extension(instant_finality_extension::extension_id());
   BOOST_TEST(!!ext);
   std::optional<finalizer_policy> fin_policy = std::get<instant_finality_extension>(*ext).new_finalizer_policy;
   BOOST_TEST(!!fin_policy);
   BOOST_TEST(fin_policy->finalizers.size() == accounts.size());
   BOOST_TEST(fin_policy->generation == 1u);

   block = t.produce_block(); // hotstuff now active
   BOOST_TEST(block->confirmed == 0);
   auto fb = t.control->fetch_block_by_id(block->calculate_id());
   BOOST_REQUIRE(!!fb);
   BOOST_TEST(fb == block);
   ext = fb->extract_header_extension(instant_finality_extension::extension_id());
   BOOST_REQUIRE(ext);

   auto lib_at_transition = curr_lib;

   if( lib_advancing_expected ) {
      // Local votes are signed asychronously. They can be delayed.
      // Leave room for the delay.
      for (auto i = 0; i < 500; ++i) {
         t.produce_block();
         if (curr_lib > lib_at_transition)
            break;
      }
      BOOST_CHECK_GT(curr_lib, lib_at_transition);
   } else {
      t.produce_blocks(4);
      BOOST_CHECK_EQUAL(curr_lib, lib_at_transition);
   }
}

BOOST_AUTO_TEST_CASE(threshold_equal_to_half_weight_sum_test) { try {
   vector<account_name> account_names = {
      "alice"_n, "bob"_n, "carol"_n
   };

   // threshold set to half of the weight sum of finalizers
   base_tester::finalizer_policy_input policy_input = {
      .finalizers       = { {.name = "alice"_n, .weight = 1},
                            {.name = "bob"_n,   .weight = 2},
                            {.name = "carol"_n, .weight = 3} },
      .threshold        = 3,
      .local_finalizers = {"alice"_n, "bob"_n}
   };

   // threshold must be greater than half of the sum of the weights
   BOOST_REQUIRE_THROW( test_finality_transition(account_names, policy_input, false), eosio_assert_message_exception );

} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(votes_equal_to_threshold_test) { try {
   vector<account_name> account_names = {
      "alice"_n, "bob"_n, "carol"_n
   };

   base_tester::finalizer_policy_input policy_input = {
      .finalizers       = { {.name = "alice"_n, .weight = 1},
                            {.name = "bob"_n,   .weight = 3},
                            {.name = "carol"_n, .weight = 5} },
      .threshold        = 5,
      .local_finalizers = {"carol"_n}
   };

   // Carol votes with weight 5 and threshold 5
   test_finality_transition(account_names, policy_input, true); // lib_advancing_expected
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(votes_greater_than_threshold_test) { try {
   vector<account_name> account_names = {
      "alice"_n, "bob"_n, "carol"_n
   };

   base_tester::finalizer_policy_input policy_input = {
      .finalizers       = { {.name = "alice"_n, .weight = 1},
                            {.name = "bob"_n,   .weight = 4},
                            {.name = "carol"_n, .weight = 2} },
      .threshold        = 4,
      .local_finalizers = {"alice"_n, "bob"_n}
   };

   // alice and bob vote with weight 5 and threshold 4
   test_finality_transition(account_names, policy_input, true); // lib_advancing_expected
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_CASE(votes_less_than_threshold_test) { try {
   vector<account_name> account_names = {
      "alice"_n, "bob"_n, "carol"_n
   };

   base_tester::finalizer_policy_input policy_input = {
      .finalizers       = { {.name = "alice"_n, .weight = 1},
                            {.name = "bob"_n,   .weight = 3},
                            {.name = "carol"_n, .weight = 10} },
      .threshold        = 8,
      .local_finalizers = {"alice"_n, "bob"_n}
   };

   // alice and bob vote with weight 4 but threshold 8. LIB cannot advance
   test_finality_transition(account_names, policy_input, false); // not expecting lib advancing
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()

#include <eosio/chain/global_property_object.hpp>
#include <eosio/testing/tester.hpp>

#include <boost/test/unit_test.hpp>

using namespace eosio::testing;
using namespace eosio::chain;
using mvo = fc::mutable_variant_object;

BOOST_AUTO_TEST_SUITE(producer_schedule_if_tests)

namespace {

// Calculate expected producer given the schedule and slot number
inline account_name get_expected_producer(const vector<producer_authority>& schedule, block_timestamp_type t) {
   const auto& index = (t.slot % (schedule.size() * config::producer_repetitions)) / config::producer_repetitions;
   return schedule.at(index).producer_name;
};

} // anonymous namespace

BOOST_FIXTURE_TEST_CASE( verify_producer_schedule_after_instant_finality_activation, validating_tester ) try {

   // Utility function to ensure that producer schedule work as expected
   const auto& confirm_schedule_correctness = [&](const vector<producer_authority>& new_prod_schd, uint32_t expected_schd_ver, uint32_t expected_block_num = 0)  {
      const uint32_t check_duration = 100; // number of blocks
      bool scheduled_changed_to_new = false;
      for (uint32_t i = 0; i < check_duration; ++i) {
         const auto current_schedule = control->active_producers();
         if (new_prod_schd == current_schedule.producers) {
            scheduled_changed_to_new = true;
            if (expected_block_num != 0)
               BOOST_TEST(control->head_block_num() == expected_block_num);
         }

         auto b = produce_block();
         BOOST_TEST( b->confirmed == 0); // must be 0 after instant finality is enabled

         // Check if the producer is the same as what we expect
         const auto block_time = control->head_block_time();
         const auto& expected_producer = get_expected_producer(current_schedule.producers, block_time);
         BOOST_TEST(control->head_block_producer() == expected_producer);

         if (scheduled_changed_to_new)
            break;
      }

      BOOST_TEST(scheduled_changed_to_new);
   };

   uint32_t lib = 0;
   control->irreversible_block().connect([&](const block_signal_params& t) {
      const auto& [ block, id ] = t;
      lib = block->block_num();
   });

   // Create producer accounts
   vector<account_name> producers = {
           "inita"_n, "initb"_n, "initc"_n, "initd"_n, "inite"_n, "initf"_n, "initg"_n,
           "inith"_n, "initi"_n, "initj"_n, "initk"_n, "initl"_n, "initm"_n, "initn"_n,
           "inito"_n, "initp"_n, "initq"_n, "initr"_n, "inits"_n, "initt"_n, "initu"_n
   };
   create_accounts(producers);

   // enable instant_finality
   set_finalizers(producers);
   auto setfin_block = produce_block(); // this block contains the header extension of the finalizer set

   for (block_num_type active_block_num = setfin_block->block_num(); active_block_num > lib; produce_block()) {
      set_producers({"initc"_n, "inite"_n}); // should be ignored since in transition
      (void)active_block_num; // avoid warning
   };

   // ---- Test first set of producers ----
   // Send set prods action and confirm schedule correctness
   auto trace = set_producers(producers);
   const auto first_prod_schd = get_producer_authorities(producers);
   // called in first round so complete it, skip one round of 12 and start on next round, so block 24
   confirm_schedule_correctness(first_prod_schd, 1, 24);

   // ---- Test second set of producers ----
   vector<account_name> second_set_of_producer = {
           producers[3], producers[6], producers[9], producers[12], producers[15], producers[18], producers[20]
   };
   // Send set prods action and confirm schedule correctness
   set_producers(second_set_of_producer);
   const auto second_prod_schd = get_producer_authorities(second_set_of_producer);
   // called after block 24, so next,next is 48
   confirm_schedule_correctness(second_prod_schd, 2, 48);

   // ---- Test deliberately miss some blocks ----
   const int64_t num_of_missed_blocks = 5000;
   produce_block(fc::microseconds(500 * 1000 * num_of_missed_blocks));
   // Ensure schedule is still correct
   confirm_schedule_correctness(second_prod_schd, 2);
   produce_block();

   // ---- Test third set of producers ----
   vector<account_name> third_set_of_producer = {
           producers[2], producers[5], producers[8], producers[11], producers[14], producers[17], producers[20],
           producers[0], producers[3], producers[6], producers[9], producers[12], producers[15], producers[18],
           producers[1], producers[4], producers[7], producers[10], producers[13], producers[16], producers[19]
   };
   // Send set prods action and confirm schedule correctness
   set_producers(third_set_of_producer);
   const auto third_prod_schd = get_producer_authorities(third_set_of_producer);
   confirm_schedule_correctness(third_prod_schd, 3);

} FC_LOG_AND_RETHROW()

bool compare_schedules( const vector<producer_authority>& a, const producer_authority_schedule& b ) {
      return std::equal( a.begin(), a.end(), b.producers.begin(), b.producers.end() );
};

BOOST_FIXTURE_TEST_CASE( proposer_policy_progression_test, validating_tester ) try {
   create_accounts( {"alice"_n,"bob"_n,"carol"_n} );

   while (control->head_block_num() < 3) {
      produce_block();
   }

   // activate instant_finality
   set_finalizers({"alice"_n,"bob"_n,"carol"_n});
   produce_block(); // this block contains the header extension of the finalizer set
   produce_block(); // one producer, lib here

   // current proposer schedule stays the same as the one prior to IF transition
   vector<producer_authority> prev_sch = {
                                 producer_authority{"eosio"_n, block_signing_authority_v0{1, {{get_public_key("eosio"_n, "active"), 1}}}}};
   BOOST_CHECK_EQUAL( true, compare_schedules( prev_sch, control->active_producers() ) );
   BOOST_CHECK_EQUAL( 0, control->active_producers().version );

   // set a new proposer policy sch1
   set_producers( {"alice"_n} );
   vector<producer_authority> sch1 = {
                                 producer_authority{"alice"_n, block_signing_authority_v0{1, {{get_public_key("alice"_n, "active"), 1}}}}
                               };

   // start a round of production
   produce_blocks(config::producer_repetitions);

   // sch1 cannot become active before one round of production
   BOOST_CHECK_EQUAL( 0, control->active_producers().version );
   BOOST_CHECK_EQUAL( true, compare_schedules( prev_sch, control->active_producers() ) );

   // set another ploicy to have multiple pending different active time policies
   set_producers( {"bob"_n,"carol"_n} );
   vector<producer_authority> sch2 = {
                                 producer_authority{"bob"_n,   block_signing_authority_v0{ 1, {{get_public_key("bob"_n,   "active"),1}}}},
                                 producer_authority{"carol"_n, block_signing_authority_v0{ 1, {{get_public_key("carol"_n, "active"),1}}}}
                               };
   produce_block();

   // set another ploicy should replace sch2
   set_producers( {"bob"_n,"alice"_n} );
   vector<producer_authority> sch3 = {
      producer_authority{"bob"_n,   block_signing_authority_v0{ 1, {{get_public_key("bob"_n,   "active"),1}}}},
      producer_authority{"alice"_n, block_signing_authority_v0{ 1, {{get_public_key("alice"_n, "active"),1}}}}
   };

   // another round
   produce_blocks(config::producer_repetitions-1); // -1, already produced one of the round above

   // sch1  must become active no later than 2 rounds but sch2 cannot become active yet
   BOOST_CHECK_EQUAL( control->active_producers().version, 1u );
   BOOST_CHECK_EQUAL( true, compare_schedules( sch1, control->active_producers() ) );

   produce_blocks(config::producer_repetitions);

   // sch3 becomes active
   BOOST_CHECK_EQUAL( 2u, control->active_producers().version ); // should be 2 as sch2 was replaced by sch3
   BOOST_CHECK_EQUAL( true, compare_schedules( sch3, control->active_producers() ) );
} FC_LOG_AND_RETHROW()


BOOST_FIXTURE_TEST_CASE( proposer_policy_misc_tests, validating_tester ) try {
   create_accounts( {"alice"_n,"bob"_n} );

   while (control->head_block_num() < 3) {
      produce_block();
   }

   // activate instant_finality
   set_finalizers({"alice"_n,"bob"_n});
   produce_block(); // this block contains the header extension of the finalizer set
   produce_block(); // one producer, lib here

   { // set multiple policies in the same block. The last one will be chosen
      set_producers( {"alice"_n} );
      set_producers( {"bob"_n} );

      produce_blocks(2 * config::producer_repetitions);

      vector<producer_authority> sch = {
         producer_authority{"bob"_n, block_signing_authority_v0{1, {{get_public_key("bob"_n, "active"), 1}}}}
                               };
      BOOST_CHECK_EQUAL( control->active_producers().version, 1u );
      BOOST_CHECK_EQUAL( true, compare_schedules( sch, control->active_producers() ) );
   }

   { // unknown account in proposer policy
      BOOST_CHECK_THROW( set_producers({"carol"_n}), wasm_execution_error );
   }

} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_SUITE_END()

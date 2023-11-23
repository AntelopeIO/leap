#include <eosio/chain/global_property_object.hpp>
#include <eosio/testing/tester.hpp>

#include <boost/test/unit_test.hpp>

#include "fork_test_utilities.hpp"

using namespace eosio::testing;
using namespace eosio::chain;
using mvo = fc::mutable_variant_object;

BOOST_AUTO_TEST_SUITE(producer_schedule_hs_tests)

namespace {

// Calculate expected producer given the schedule and slot number
inline account_name get_expected_producer(const vector<producer_authority>& schedule, block_timestamp_type t) {
   const auto& index = (t.slot % (schedule.size() * config::producer_repetitions)) / config::producer_repetitions;
   return schedule.at(index).producer_name;
};

} // anonymous namespace

BOOST_FIXTURE_TEST_CASE( verify_producer_schedule_after_hotstuff_activation, validating_tester ) try {

   // Utility function to ensure that producer schedule work as expected
   const auto& confirm_schedule_correctness = [&](const vector<producer_authority>& new_prod_schd, uint32_t expected_schd_ver, uint32_t expected_block_num = 0)  {
      const uint32_t check_duration = 100; // number of blocks
      bool scheduled_changed_to_new = false;
      for (uint32_t i = 0; i < check_duration; ++i) {
         const auto current_schedule = control->head_block_state()->active_schedule.producers;
         if (new_prod_schd == current_schedule) {
            scheduled_changed_to_new = true;
            if (expected_block_num != 0)
               BOOST_TEST(control->head_block_num() == expected_block_num);
         }

         produce_block();

         // Check if the producer is the same as what we expect
         const auto block_time = control->head_block_time();
         const auto& expected_producer = get_expected_producer(current_schedule, block_time);
         BOOST_TEST(control->head_block_producer() == expected_producer);

         if (scheduled_changed_to_new)
            break;
      }

      BOOST_TEST(scheduled_changed_to_new);

      const auto current_schd_ver = control->head_block_header().schedule_version;
      BOOST_TEST(current_schd_ver == expected_schd_ver);
   };

   uint32_t lib = 0;
   control->irreversible_block.connect([&](const block_state_ptr& bs) {
      lib = bs->block_num;
   });

   // Create producer accounts
   vector<account_name> producers = {
           "inita"_n, "initb"_n, "initc"_n, "initd"_n, "inite"_n, "initf"_n, "initg"_n,
           "inith"_n, "initi"_n, "initj"_n, "initk"_n, "initl"_n, "initm"_n, "initn"_n,
           "inito"_n, "initp"_n, "initq"_n, "initr"_n, "inits"_n, "initt"_n, "initu"_n
   };
   create_accounts(producers);

   // activate hotstuff
   set_finalizers(producers);
   auto block = produce_block(); // this block contains the header extension of the finalizer set
   BOOST_TEST(lib == 3);

   // ---- Test first set of producers ----
   // Send set prods action and confirm schedule correctness
   set_producers(producers);
   const auto first_prod_schd = get_producer_authorities(producers);
   // TODO: update expected when lib for hotstuff is working, will change from 22 at that time
   confirm_schedule_correctness(first_prod_schd, 1, 22);

   // ---- Test second set of producers ----
   vector<account_name> second_set_of_producer = {
           producers[3], producers[6], producers[9], producers[12], producers[15], producers[18], producers[20]
   };
   // Send set prods action and confirm schedule correctness
   set_producers(second_set_of_producer);
   const auto second_prod_schd = get_producer_authorities(second_set_of_producer);
   // TODO: update expected when lib for hotstuff is working, will change from 44 at that time
   confirm_schedule_correctness(second_prod_schd, 2, 44);

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

/** TODO: Enable tests after hotstuff LIB is working

BOOST_FIXTURE_TEST_CASE( producer_schedule_promotion_test, validating_tester ) try {
   create_accounts( {"alice"_n,"bob"_n,"carol"_n} );
   while (control->head_block_num() < 3) {
      produce_block();
   }

   // activate hotstuff
   set_finalizers({"alice"_n,"bob"_n,"carol"_n});
   auto block = produce_block(); // this block contains the header extension of the finalizer set

   auto compare_schedules = [&]( const vector<producer_authority>& a, const producer_authority_schedule& b ) {
      return std::equal( a.begin(), a.end(), b.producers.begin(), b.producers.end() );
   };

   auto res = set_producers( {"alice"_n,"bob"_n} );
   vector<producer_authority> sch1 = {
                                 producer_authority{"alice"_n, block_signing_authority_v0{1, {{get_public_key("alice"_n, "active"), 1}}}},
                                 producer_authority{"bob"_n,   block_signing_authority_v0{1, {{get_public_key("bob"_n,   "active"), 1}}}}
                               };
   //wdump((fc::json::to_pretty_string(res)));
   wlog("set producer schedule to [alice,bob]");
   BOOST_REQUIRE_EQUAL( true, control->proposed_producers().has_value() );
   BOOST_CHECK_EQUAL( true, compare_schedules( sch1, *control->proposed_producers() ) );
   BOOST_CHECK_EQUAL( control->pending_producers().version, 0u );
   produce_block(); // Starts new block which promotes the proposed schedule to pending
   BOOST_CHECK_EQUAL( control->pending_producers().version, 1u );
   BOOST_CHECK_EQUAL( true, compare_schedules( sch1, control->pending_producers() ) );
   BOOST_CHECK_EQUAL( control->active_producers().version, 0u );
   produce_block();
   produce_block(); // Starts new block which promotes the pending schedule to active
   BOOST_CHECK_EQUAL( control->active_producers().version, 1u );
   BOOST_CHECK_EQUAL( true, compare_schedules( sch1, control->active_producers() ) );
   produce_blocks(6);

   res = set_producers( {"alice"_n,"bob"_n,"carol"_n} );
   vector<producer_authority> sch2 = {
                                 producer_authority{"alice"_n, block_signing_authority_v0{1, {{get_public_key("alice"_n, "active"),1}}}},
                                 producer_authority{"bob"_n,   block_signing_authority_v0{1, {{get_public_key("bob"_n,   "active"),1}}}},
                                 producer_authority{"carol"_n, block_signing_authority_v0{1, {{get_public_key("carol"_n, "active"),1}}}}
                               };
   wlog("set producer schedule to [alice,bob,carol]");
   BOOST_REQUIRE_EQUAL( true, control->proposed_producers().has_value() );
   BOOST_CHECK_EQUAL( true, compare_schedules( sch2, *control->proposed_producers() ) );

   produce_block();
   produce_blocks(23); // Alice produces the last block of her first round.
                    // Bob's first block (which advances LIB to Alice's last block) is started but not finalized.
   BOOST_REQUIRE_EQUAL( control->head_block_producer(), "alice"_n );
   BOOST_REQUIRE_EQUAL( control->pending_block_producer(), "bob"_n );
   BOOST_CHECK_EQUAL( control->pending_producers().version, 2u );

   produce_blocks(12); // Bob produces his first 11 blocks
   BOOST_CHECK_EQUAL( control->active_producers().version, 1u );
   produce_blocks(12); // Bob produces his 12th block.
                    // Alice's first block of the second round is started but not finalized (which advances LIB to Bob's last block).
   BOOST_REQUIRE_EQUAL( control->head_block_producer(), "alice"_n );
   BOOST_REQUIRE_EQUAL( control->pending_block_producer(), "bob"_n );
   BOOST_CHECK_EQUAL( control->active_producers().version, 2u );
   BOOST_CHECK_EQUAL( true, compare_schedules( sch2, control->active_producers() ) );

   produce_block(); // Alice produces the first block of her second round which has changed the active schedule.

   // The next block will be produced according to the new schedule
   produce_block();
   BOOST_CHECK_EQUAL( control->head_block_producer(), "carol"_n ); // And that next block happens to be produced by Carol.

   BOOST_REQUIRE_EQUAL( validate(), true );
} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_CASE( producer_watermark_test ) try {
   tester c;

   c.create_accounts( {"alice"_n,"bob"_n,"carol"_n} );
   c.produce_block();

   // activate hotstuff
   c.set_finalizers({"alice"_n,"bob"_n,"carol"_n});
   auto block = c.produce_block(); // this block contains the header extension of the finalizer set

   auto compare_schedules = [&]( const vector<producer_authority>& a, const producer_authority_schedule& b ) {
      return std::equal( a.begin(), a.end(), b.producers.begin(), b.producers.end() );
   };

   auto res = c.set_producers( {"alice"_n,"bob"_n,"carol"_n} );
   vector<producer_authority> sch1 = {
                                 producer_authority{"alice"_n, block_signing_authority_v0{ 1, {{c.get_public_key("alice"_n, "active"),1}}}},
                                 producer_authority{"bob"_n,   block_signing_authority_v0{ 1, {{c.get_public_key("bob"_n,   "active"),1}}}},
                                 producer_authority{"carol"_n, block_signing_authority_v0{ 1, {{c.get_public_key("carol"_n, "active"),1}}}}
                               };
   wlog("set producer schedule to [alice,bob,carol]");
   BOOST_REQUIRE_EQUAL( true, c.control->proposed_producers().has_value() );
   BOOST_CHECK_EQUAL( true, compare_schedules( sch1, *c.control->proposed_producers() ) );
   BOOST_CHECK_EQUAL( c.control->pending_producers().version, 0u );
   c.produce_block(); // Starts new block which promotes the proposed schedule to pending
   BOOST_CHECK_EQUAL( c.control->pending_producers().version, 1u );
   BOOST_CHECK_EQUAL( true, compare_schedules( sch1, c.control->pending_producers() ) );
   BOOST_CHECK_EQUAL( c.control->active_producers().version, 0u );
   c.produce_block();
   c.produce_block(); // Starts new block which promotes the pending schedule to active
   BOOST_REQUIRE_EQUAL( c.control->active_producers().version, 1u );
   BOOST_CHECK_EQUAL( true, compare_schedules( sch1, c.control->active_producers() ) );

   produce_until_transition( c, "carol"_n, "alice"_n );
   c.produce_block();
   produce_until_transition( c, "carol"_n, "alice"_n );

   res = c.set_producers( {"alice"_n,"bob"_n} );
   vector<producer_authority> sch2 = {
                                 producer_authority{"alice"_n, block_signing_authority_v0{ 1, {{c.get_public_key("alice"_n, "active"),1}}}},
                                 producer_authority{"bob"_n,   block_signing_authority_v0{ 1, {{c.get_public_key("bob"_n,   "active"),1}}}}
                               };
   wlog("set producer schedule to [alice,bob]");
   BOOST_REQUIRE_EQUAL( true, c.control->proposed_producers().has_value() );
   BOOST_CHECK_EQUAL( true, compare_schedules( sch2, *c.control->proposed_producers() ) );

   produce_until_transition( c, "bob"_n, "carol"_n );
   produce_until_transition( c, "alice"_n, "bob"_n );
   BOOST_CHECK_EQUAL( c.control->pending_producers().version, 2u );
   BOOST_CHECK_EQUAL( c.control->active_producers().version, 1u );

   produce_until_transition( c, "carol"_n, "alice"_n );
   BOOST_CHECK_EQUAL( c.control->pending_producers().version, 2u );
   BOOST_CHECK_EQUAL( c.control->active_producers().version, 1u );

   produce_until_transition( c, "bob"_n, "carol"_n );
   BOOST_CHECK_EQUAL( c.control->pending_block_producer(), "carol"_n );
   BOOST_REQUIRE_EQUAL( c.control->active_producers().version, 2u );

   auto carol_last_produced_block_num = c.control->head_block_num() + 1;
   wdump((carol_last_produced_block_num));

   c.produce_block();
   BOOST_CHECK( c.control->pending_block_producer() == "alice"_n );

   res = c.set_producers( {"alice"_n,"bob"_n,"carol"_n} );
   wlog("set producer schedule to [alice,bob,carol]");
   BOOST_REQUIRE_EQUAL( true, c.control->proposed_producers().has_value() );
   BOOST_CHECK_EQUAL( true, compare_schedules( sch1, *c.control->proposed_producers() ) );

   produce_until_transition( c, "bob"_n, "alice"_n );

   auto bob_last_produced_block_num = c.control->head_block_num();
   wdump((bob_last_produced_block_num));

   produce_until_transition( c, "alice"_n, "bob"_n );

   auto alice_last_produced_block_num = c.control->head_block_num();
   wdump((alice_last_produced_block_num));

   {
      wdump((c.control->head_block_state()->producer_to_last_produced));
      const auto& last_produced = c.control->head_block_state()->producer_to_last_produced;
      auto alice_itr = last_produced.find( "alice"_n );
      BOOST_REQUIRE( alice_itr != last_produced.end() );
      BOOST_CHECK_EQUAL( alice_itr->second, alice_last_produced_block_num );
      auto bob_itr = last_produced.find( "bob"_n );
      BOOST_REQUIRE( bob_itr != last_produced.end() );
      BOOST_CHECK_EQUAL( bob_itr->second, bob_last_produced_block_num );
      auto carol_itr = last_produced.find( "carol"_n );
      BOOST_REQUIRE( carol_itr != last_produced.end() );
      BOOST_CHECK_EQUAL( carol_itr->second, carol_last_produced_block_num );
   }

   BOOST_CHECK_EQUAL( c.control->pending_producers().version, 3u );
   BOOST_REQUIRE_EQUAL( c.control->active_producers().version, 2u );

   produce_until_transition( c, "bob"_n, "alice"_n );
   BOOST_REQUIRE_EQUAL( c.control->active_producers().version, 3u );

   produce_until_transition( c, "alice"_n, "bob"_n );
   c.produce_blocks(11);
   BOOST_CHECK_EQUAL( c.control->pending_block_producer(), "bob"_n );
   c.finish_block();

   auto carol_block_num = c.control->head_block_num() + 1;
   auto carol_block_time = c.control->head_block_time() + fc::milliseconds(config::block_interval_ms);
   auto confirmed = carol_block_num - carol_last_produced_block_num - 1;

   c.control->start_block( carol_block_time, confirmed, {}, controller::block_status::incomplete );
   BOOST_CHECK_EQUAL( c.control->pending_block_producer(), "carol"_n );
   c.produce_block();
   auto h = c.control->head_block_header();

   BOOST_CHECK_EQUAL( h.producer, "carol"_n );
   BOOST_CHECK_EQUAL( h.confirmed,  confirmed );

   produce_until_transition( c, "carol"_n, "alice"_n );

} FC_LOG_AND_RETHROW()

**/

BOOST_FIXTURE_TEST_CASE( producer_one_of_n_test, validating_tester ) try {
   create_accounts( {"alice"_n,"bob"_n} );
   produce_block();

   // activate hotstuff
   set_finalizers({"alice"_n,"bob"_n});
   auto block = produce_block(); // this block contains the header extension of the finalizer set

   vector<producer_authority> sch1 = {
                                 producer_authority{"alice"_n, block_signing_authority_v0{1, {{get_public_key("alice"_n, "bs1"), 1}, {get_public_key("alice"_n, "bs2"), 1}}}},
                                 producer_authority{"bob"_n,   block_signing_authority_v0{1, {{get_public_key("bob"_n,   "bs1"), 1}, {get_public_key("bob"_n,   "bs2"), 1}}}}
                               };

   auto res = set_producer_schedule( sch1 );
   block_signing_private_keys.emplace(get_public_key("alice"_n, "bs1"), get_private_key("alice"_n, "bs1"));
   block_signing_private_keys.emplace(get_public_key("bob"_n,   "bs1"), get_private_key("bob"_n,   "bs1"));

   BOOST_REQUIRE(produce_until_blocks_from(*this, {"alice"_n, "bob"_n}, 100));

   BOOST_REQUIRE_EQUAL( validate(), true );
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( producer_m_of_n_test, validating_tester ) try {
   create_accounts( {"alice"_n,"bob"_n} );
   produce_block();

   // activate hotstuff
   set_finalizers({"alice"_n,"bob"_n});
   auto block = produce_block(); // this block contains the header extension of the finalizer set

   vector<producer_authority> sch1 = {
                                 producer_authority{"alice"_n, block_signing_authority_v0{2, {{get_public_key("alice"_n, "bs1"), 1}, {get_public_key("alice"_n, "bs2"), 1}}}},
                                 producer_authority{"bob"_n,   block_signing_authority_v0{2, {{get_public_key("bob"_n,   "bs1"), 1}, {get_public_key("bob"_n,   "bs2"), 1}}}}
                               };

   auto res = set_producer_schedule( sch1 );
   block_signing_private_keys.emplace(get_public_key("alice"_n, "bs1"), get_private_key("alice"_n, "bs1"));
   block_signing_private_keys.emplace(get_public_key("alice"_n, "bs2"), get_private_key("alice"_n, "bs2"));
   block_signing_private_keys.emplace(get_public_key("bob"_n,   "bs1"), get_private_key("bob"_n,   "bs1"));
   block_signing_private_keys.emplace(get_public_key("bob"_n,   "bs2"), get_private_key("bob"_n,   "bs2"));

   BOOST_REQUIRE(produce_until_blocks_from(*this, {"alice"_n, "bob"_n}, 100));

   BOOST_REQUIRE_EQUAL( validate(), true );
} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_SUITE_END()

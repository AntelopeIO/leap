#include <eosio/chain/global_property_object.hpp>
#include <eosio/testing/tester.hpp>

#include <boost/test/unit_test.hpp>

#include "fork_test_utilities.hpp"

using namespace eosio::testing;
using namespace eosio::chain;
using mvo = fc::mutable_variant_object;

BOOST_AUTO_TEST_SUITE(producer_schedule_tests)

namespace {

// Calculate expected producer given the schedule and slot number
account_name get_expected_producer(const vector<producer_authority>& schedule, block_timestamp_type t) {
   const auto& index = (t.slot % (schedule.size() * config::producer_repetitions)) / config::producer_repetitions;
   return schedule.at(index).producer_name;
};

} // anonymous namespace

BOOST_FIXTURE_TEST_CASE( verify_producer_schedule, validating_tester ) try {

   // Utility function to ensure that producer schedule work as expected
   const auto& confirm_schedule_correctness = [&](const vector<producer_authority>& new_prod_schd, uint32_t expected_schd_ver)  {
      const uint32_t check_duration = 1000; // number of blocks
      bool scheduled_changed_to_new = false;
      for (uint32_t i = 0; i < check_duration; ++i) {
         const auto current_schedule = control->head_block_state()->active_schedule.producers;
         if (new_prod_schd == current_schedule) {
            scheduled_changed_to_new = true;
         }

         // Produce block
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

   // Create producer accounts
   vector<account_name> producers = {
           "inita"_n, "initb"_n, "initc"_n, "initd"_n, "inite"_n, "initf"_n, "initg"_n,
           "inith"_n, "initi"_n, "initj"_n, "initk"_n, "initl"_n, "initm"_n, "initn"_n,
           "inito"_n, "initp"_n, "initq"_n, "initr"_n, "inits"_n, "initt"_n, "initu"_n
   };
   create_accounts(producers);

   // ---- Test first set of producers ----
   // Send set prods action and confirm schedule correctness
   set_producers(producers);
   const auto first_prod_schd = get_producer_authorities(producers);
   confirm_schedule_correctness(first_prod_schd, 1);

   // ---- Test second set of producers ----
   vector<account_name> second_set_of_producer = {
           producers[3], producers[6], producers[9], producers[12], producers[15], producers[18], producers[20]
   };
   // Send set prods action and confirm schedule correctness
   set_producers(second_set_of_producer);
   const auto second_prod_schd = get_producer_authorities(second_set_of_producer);
   confirm_schedule_correctness(second_prod_schd, 2);

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

BOOST_FIXTURE_TEST_CASE( verify_producers, validating_tester ) try {

   vector<account_name> valid_producers = {
      "inita"_n, "initb"_n, "initc"_n, "initd"_n, "inite"_n, "initf"_n, "initg"_n,
      "inith"_n, "initi"_n, "initj"_n, "initk"_n, "initl"_n, "initm"_n, "initn"_n,
      "inito"_n, "initp"_n, "initq"_n, "initr"_n, "inits"_n, "initt"_n, "initu"_n
   };
   create_accounts(valid_producers);
   set_producers(valid_producers);

   // account initz does not exist
   vector<account_name> nonexisting_producer = { "initz"_n };
   BOOST_CHECK_THROW(set_producers(nonexisting_producer), wasm_execution_error);

   // replace initg with inita, inita is now duplicate
   vector<account_name> invalid_producers = {
      "inita"_n, "initb"_n, "initc"_n, "initd"_n, "inite"_n, "initf"_n, "inita"_n,
      "inith"_n, "initi"_n, "initj"_n, "initk"_n, "initl"_n, "initm"_n, "initn"_n,
      "inito"_n, "initp"_n, "initq"_n, "initr"_n, "inits"_n, "initt"_n, "initu"_n
   };

   BOOST_CHECK_THROW(set_producers(invalid_producers), wasm_execution_error);

} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( producer_schedule_promotion_test, validating_tester ) try {
   create_accounts( {"alice"_n,"bob"_n,"carol"_n} );
   while (control->head_block_num() < 3) {
      produce_block();
   }

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

BOOST_FIXTURE_TEST_CASE( producer_schedule_reduction, tester ) try {
   create_accounts( {"alice"_n,"bob"_n,"carol"_n} );
   while (control->head_block_num() < 3) {
      produce_block();
   }

   auto compare_schedules = [&]( const vector<producer_authority>& a, const producer_authority_schedule& b ) {
      return std::equal( a.begin(), a.end(), b.producers.begin(), b.producers.end() );
   };

   auto res = set_producers( {"alice"_n,"bob"_n,"carol"_n} );
   vector<producer_authority> sch1 = {
                                 producer_authority{"alice"_n, block_signing_authority_v0{ 1, {{get_public_key("alice"_n, "active"),1}}}},
                                 producer_authority{"bob"_n,   block_signing_authority_v0{ 1, {{get_public_key("bob"_n,   "active"),1}}}},
                                 producer_authority{"carol"_n, block_signing_authority_v0{ 1, {{get_public_key("carol"_n, "active"),1}}}}
                               };
   wlog("set producer schedule to [alice,bob,carol]");
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

   res = set_producers( {"alice"_n,"bob"_n} );
   vector<producer_authority> sch2 = {
                                 producer_authority{"alice"_n, block_signing_authority_v0{ 1, {{ get_public_key("alice"_n, "active"),1}}}},
                                 producer_authority{"bob"_n,   block_signing_authority_v0{ 1, {{ get_public_key("bob"_n,   "active"),1}}}}
                               };
   wlog("set producer schedule to [alice,bob]");
   BOOST_REQUIRE_EQUAL( true, control->proposed_producers().has_value() );
   BOOST_CHECK_EQUAL( true, compare_schedules( sch2, *control->proposed_producers() ) );

   produce_blocks(48);
   BOOST_REQUIRE_EQUAL( control->head_block_producer(), "bob"_n );
   BOOST_REQUIRE_EQUAL( control->pending_block_producer(), "carol"_n );
   BOOST_CHECK_EQUAL( control->pending_producers().version, 2u );

   produce_blocks(47);
   BOOST_CHECK_EQUAL( control->active_producers().version, 1u );
   produce_blocks(1);

   BOOST_REQUIRE_EQUAL( control->head_block_producer(), "carol"_n );
   BOOST_REQUIRE_EQUAL( control->pending_block_producer(), "alice"_n );
   BOOST_CHECK_EQUAL( control->active_producers().version, 2u );
   BOOST_CHECK_EQUAL( true, compare_schedules( sch2, control->active_producers() ) );

   produce_blocks(2);
   BOOST_CHECK_EQUAL( control->head_block_producer(), "bob"_n );

   BOOST_REQUIRE_EQUAL( validate(), true );
} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_CASE( empty_producer_schedule_has_no_effect ) try {
   fc::temp_directory tempdir;
   validating_tester c( tempdir, true );
   c.execute_setup_policy( setup_policy::preactivate_feature_and_new_bios );

   c.create_accounts( {"alice"_n,"bob"_n,"carol"_n} );
   while (c.control->head_block_num() < 3) {
      c.produce_block();
   }

   auto compare_schedules = [&]( const vector<producer_authority>& a, const producer_authority_schedule& b ) {
      return std::equal( a.begin(), a.end(), b.producers.begin(), b.producers.end() );
   };

   auto res = c.set_producers_legacy( {"alice"_n,"bob"_n} );
   vector<producer_authority> sch1 = {
                                 producer_authority{"alice"_n, block_signing_authority_v0{ 1, {{ get_public_key("alice"_n, "active"),1}}}},
                                 producer_authority{"bob"_n,   block_signing_authority_v0{ 1, {{ get_public_key("bob"_n,   "active"),1}}}}
                               };
   wlog("set producer schedule to [alice,bob]");
   BOOST_REQUIRE_EQUAL( true, c.control->proposed_producers().has_value() );
   BOOST_CHECK_EQUAL( true, compare_schedules( sch1, *c.control->proposed_producers() ) );
   BOOST_CHECK_EQUAL( c.control->pending_producers().producers.size(), 0u );

   // Start a new block which promotes the proposed schedule to pending
   c.produce_block();
   BOOST_CHECK_EQUAL( c.control->pending_producers().version, 1u );
   BOOST_CHECK_EQUAL( true, compare_schedules( sch1, c.control->pending_producers() ) );
   BOOST_CHECK_EQUAL( c.control->active_producers().version, 0u );

   // Start a new block which promotes the pending schedule to active
   c.produce_block();
   BOOST_CHECK_EQUAL( c.control->active_producers().version, 1u );
   BOOST_CHECK_EQUAL( true, compare_schedules( sch1, c.control->active_producers() ) );
   c.produce_blocks(6);

   res = c.set_producers_legacy( {} );
   wlog("set producer schedule to []");
   BOOST_REQUIRE_EQUAL( true, c.control->proposed_producers().has_value() );
   BOOST_CHECK_EQUAL( c.control->proposed_producers()->producers.size(), 0u );
   BOOST_CHECK_EQUAL( c.control->proposed_producers()->version, 2u );

   c.produce_blocks(12);
   BOOST_CHECK_EQUAL( c.control->pending_producers().version, 1u );

   // Empty producer schedule does get promoted from proposed to pending
   c.produce_block();
   BOOST_CHECK_EQUAL( c.control->pending_producers().version, 2u );
   BOOST_CHECK_EQUAL( false, c.control->proposed_producers().has_value() );

   // However it should not get promoted from pending to active
   c.produce_blocks(24);
   BOOST_CHECK_EQUAL( c.control->active_producers().version, 1u );
   BOOST_CHECK_EQUAL( c.control->pending_producers().version, 2u );

   // Setting a new producer schedule should still use version 2
   res = c.set_producers_legacy( {"alice"_n,"bob"_n,"carol"_n} );
   vector<producer_authority> sch2 = {
                                 producer_authority{"alice"_n, block_signing_authority_v0{ 1, {{get_public_key("alice"_n, "active"),1}}}},
                                 producer_authority{"bob"_n,   block_signing_authority_v0{ 1, {{get_public_key("bob"_n,   "active"),1}}}},
                                 producer_authority{"carol"_n, block_signing_authority_v0{ 1, {{get_public_key("carol"_n, "active"),1}}}}
                               };
   wlog("set producer schedule to [alice,bob,carol]");
   BOOST_REQUIRE_EQUAL( true, c.control->proposed_producers().has_value() );
   BOOST_CHECK_EQUAL( true, compare_schedules( sch2, *c.control->proposed_producers() ) );
   BOOST_CHECK_EQUAL( c.control->proposed_producers()->version, 2u );

   // Produce enough blocks to promote the proposed schedule to pending, which it can do because the existing pending has zero producers
   c.produce_blocks(24);
   BOOST_CHECK_EQUAL( c.control->active_producers().version, 1u );
   BOOST_CHECK_EQUAL( c.control->pending_producers().version, 2u );
   BOOST_CHECK_EQUAL( true, compare_schedules( sch2, c.control->pending_producers() ) );

   // Produce enough blocks to promote the pending schedule to active
   c.produce_blocks(24);
   BOOST_CHECK_EQUAL( c.control->active_producers().version, 2u );
   BOOST_CHECK_EQUAL( true, compare_schedules( sch2, c.control->active_producers() ) );

   BOOST_REQUIRE_EQUAL( c.validate(), true );
} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_CASE( producer_watermark_test ) try {
   tester c;

   c.create_accounts( {"alice"_n,"bob"_n,"carol"_n} );
   c.produce_block();

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

BOOST_FIXTURE_TEST_CASE( producer_one_of_n_test, validating_tester ) try {
   create_accounts( {"alice"_n,"bob"_n} );
   produce_block();

   vector<producer_authority> sch1 = {
                                 producer_authority{"alice"_n, block_signing_authority_v0{1, {{get_public_key("alice"_n, "bs1"), 1}, {get_public_key("alice"_n, "bs2"), 1}}}},
                                 producer_authority{"bob"_n,   block_signing_authority_v0{1, {{get_public_key("bob"_n,   "bs1"), 1}, {get_public_key("bob"_n,   "bs2"), 1}}}}
                               };

   auto res = set_producer_schedule( sch1 );
   block_signing_private_keys.emplace(get_public_key("alice"_n, "bs1"), get_private_key("alice"_n, "bs1"));
   block_signing_private_keys.emplace(get_public_key("bob"_n,   "bs1"), get_private_key("bob"_n,   "bs1"));

   BOOST_REQUIRE(produce_until_blocks_from(*this, {"alice"_n, "bob"_n}, 300));

   BOOST_REQUIRE_EQUAL( validate(), true );
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( producer_m_of_n_test, validating_tester ) try {
   create_accounts( {"alice"_n,"bob"_n} );
   produce_block();


   vector<producer_authority> sch1 = {
                                 producer_authority{"alice"_n, block_signing_authority_v0{2, {{get_public_key("alice"_n, "bs1"), 1}, {get_public_key("alice"_n, "bs2"), 1}}}},
                                 producer_authority{"bob"_n,   block_signing_authority_v0{2, {{get_public_key("bob"_n,   "bs1"), 1}, {get_public_key("bob"_n,   "bs2"), 1}}}}
                               };

   auto res = set_producer_schedule( sch1 );
   block_signing_private_keys.emplace(get_public_key("alice"_n, "bs1"), get_private_key("alice"_n, "bs1"));
   block_signing_private_keys.emplace(get_public_key("alice"_n, "bs2"), get_private_key("alice"_n, "bs2"));
   block_signing_private_keys.emplace(get_public_key("bob"_n,   "bs1"), get_private_key("bob"_n,   "bs1"));
   block_signing_private_keys.emplace(get_public_key("bob"_n,   "bs2"), get_private_key("bob"_n,   "bs2"));

   BOOST_REQUIRE(produce_until_blocks_from(*this, {"alice"_n, "bob"_n}, 300));

   BOOST_REQUIRE_EQUAL( validate(), true );
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( satisfiable_msig_test, validating_tester ) try {
   create_accounts( {"alice"_n,"bob"_n} );
   produce_block();

   vector<producer_authority> sch1 = {
           producer_authority{"alice"_n, block_signing_authority_v0{2, {{get_public_key("alice"_n, "bs1"), 1}}}}
   };

   // ensure that the entries in a wtmsig schedule are rejected if not satisfiable
   BOOST_REQUIRE_EXCEPTION(
      set_producer_schedule( sch1 ), wasm_execution_error,
      fc_exception_message_is( "producer schedule includes an unsatisfiable authority for alice" )
   );

   BOOST_REQUIRE_EQUAL( false, control->proposed_producers().has_value() );

} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( duplicate_producers_test, validating_tester ) try {
   create_accounts( {"alice"_n} );
   produce_block();

   vector<producer_authority> sch1 = {
           producer_authority{"alice"_n, block_signing_authority_v0{1, {{get_public_key("alice"_n, "bs1"), 1}}}},
           producer_authority{"alice"_n, block_signing_authority_v0{1, {{get_public_key("alice"_n, "bs2"), 1}}}}
   };

   // ensure that the schedule is rejected if it has duplicate producers in it
   BOOST_REQUIRE_EXCEPTION(
      set_producer_schedule( sch1 ), wasm_execution_error,
      fc_exception_message_is( "duplicate producer name in producer schedule" )
   );

   BOOST_REQUIRE_EQUAL( false, control->proposed_producers().has_value() );

} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE( duplicate_keys_test, validating_tester ) try {
   create_accounts( {"alice"_n,"bob"_n} );
   produce_block();

   vector<producer_authority> sch1 = {
           producer_authority{"alice"_n, block_signing_authority_v0{2, {{get_public_key("alice"_n, "bs1"), 1}, {get_public_key("alice"_n, "bs1"), 1}}}}
   };

   // ensure that the schedule is rejected if it has duplicate keys for a single producer in it
   BOOST_REQUIRE_EXCEPTION(
      set_producer_schedule( sch1 ), wasm_execution_error,
      fc_exception_message_is( "producer schedule includes a duplicated key for alice" )
   );

   BOOST_REQUIRE_EQUAL( false, control->proposed_producers().has_value() );

   // ensure that multiple producers are allowed to share keys
   vector<producer_authority> sch2 = {
           producer_authority{"alice"_n, block_signing_authority_v0{1, {{get_public_key("alice"_n, "bs1"), 1}}}},
           producer_authority{"bob"_n,   block_signing_authority_v0{1, {{get_public_key("alice"_n, "bs1"), 1}}}}
   };

   set_producer_schedule( sch2 );
   BOOST_REQUIRE_EQUAL( true, control->proposed_producers().has_value() );
} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_CASE( large_authority_overflow_test ) try {

   block_signing_authority_v0 auth;
   { // create a large authority that should overflow
      const size_t pre_overflow_count = 65'537UL; // enough for weights of 0xFFFF to add up to 0xFFFFFFFF
      auth.keys.reserve(pre_overflow_count + 1);

      for (size_t i = 0; i < pre_overflow_count; i++) {
         auto key_str = std::to_string(i) + "_bsk";
         auth.keys.emplace_back(key_weight{get_public_key("alice"_n, key_str), 0xFFFFU});
      }

      // reduce the last weight by 1 so that its unsatisfiable
      auth.keys.back().weight = 0xFFFEU;

      // add one last key with a weight of 2 so that its only satisfiable with values that sum to an overflow of 32bit uint
      auth.keys.emplace_back(key_weight{get_public_key("alice"_n, std::to_string(pre_overflow_count) + "_bsk"), 0x0002U});

      auth.threshold = 0xFFFFFFFFUL;
   }

   std::set<public_key_type> provided_keys;
   { // construct a set of all keys to provide
      for( const auto& kw: auth.keys) {
         provided_keys.emplace(kw.key);
      }
   }

   { // prove the naive accumulation overflows
      uint32_t total = 0;
      for( const auto& kw: auth.keys) {
         total += kw.weight;
      }
      BOOST_REQUIRE_EQUAL(total, 0x0UL);
   }

   auto res = auth.keys_satisfy_and_relevant(provided_keys);

   BOOST_REQUIRE_EQUAL(res.first, true);
   BOOST_REQUIRE_EQUAL(res.second, provided_keys.size());
} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_CASE( extra_signatures_test ) try {
   tester main;

   main.create_accounts( {"alice"_n} );
   main.produce_block();

   vector<producer_authority> sch1 = {
      producer_authority{"alice"_n, block_signing_authority_v0{1,  {
                                                                     {get_public_key("alice"_n, "bs1"), 1},
                                                                     {get_public_key("alice"_n, "bs2"), 1},
                                                                     {get_public_key("alice"_n, "bs3"), 1},
                                                                  }
                                                             }
                        }
   };

   main.set_producer_schedule( sch1 );
   BOOST_REQUIRE_EQUAL( true, main.control->proposed_producers().has_value() );

   main.block_signing_private_keys.emplace(get_public_key("alice"_n, "bs1"), get_private_key("alice"_n, "bs1"));
   main.block_signing_private_keys.emplace(get_public_key("alice"_n, "bs2"), get_private_key("alice"_n, "bs2"));

   BOOST_REQUIRE( main.control->pending_block_producer() == "eosio"_n );
   main.produce_blocks(3);
   BOOST_REQUIRE( main.control->pending_block_producer() == "alice"_n );

   std::shared_ptr<signed_block> b;

   // Generate a valid block and then corrupt it by adding an extra signature.
   {
      tester remote(setup_policy::none);
      push_blocks(main, remote);

      remote.block_signing_private_keys.emplace(get_public_key("alice"_n, "bs1"), get_private_key("alice"_n, "bs1"));
      remote.block_signing_private_keys.emplace(get_public_key("alice"_n, "bs2"), get_private_key("alice"_n, "bs2"));

      // Generate the block that will be corrupted.
      auto valid_block = remote.produce_block();

      BOOST_REQUIRE( valid_block->producer == "alice"_n );

      // Make a copy of pointer to the valid block.
      b = valid_block;
      BOOST_REQUIRE_EQUAL( b->block_extensions.size(), 1u );

      // Extract the existing signatures.
      constexpr auto additional_sigs_eid = additional_block_signatures_extension::extension_id();
      auto exts = b->validate_and_extract_extensions();
      BOOST_REQUIRE_EQUAL( exts.count( additional_sigs_eid ), 1u );
      auto additional_sigs = std::get<additional_block_signatures_extension>(exts.lower_bound( additional_sigs_eid )->second).signatures;
      BOOST_REQUIRE_EQUAL( additional_sigs.size(), 1u );

      // Generate the extra signature and add to additonal_sigs.
      auto header_bmroot = digest_type::hash( std::make_pair( b->digest(), remote.control->head_block_state()->blockroot_merkle.get_root() ) );
      auto sig_digest = digest_type::hash( std::make_pair(header_bmroot, remote.control->head_block_state()->pending_schedule.schedule_hash) );
      additional_sigs.emplace_back( remote.get_private_key("alice"_n, "bs3").sign(sig_digest) );
      additional_sigs.emplace_back( remote.get_private_key("alice"_n, "bs4").sign(sig_digest) );

      // Serialize the augmented additional signatures back into the block extensions.
      b->block_extensions.clear();
      emplace_extension(b->block_extensions, additional_sigs_eid, fc::raw::pack( additional_sigs ));
   }

   // Push block with extra signature to the main chain.
   BOOST_REQUIRE_EXCEPTION( main.push_block(b), wrong_signing_key, fc_exception_message_starts_with("number of block signatures") );

} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_SUITE_END()

#include <boost/test/unit_test.hpp>

#include "eosio/chain/subjective_billing.hpp"
#include <eosio/testing/tester.hpp>
#include <fc/time.hpp>

namespace {

using namespace eosio;
using namespace eosio::chain;

BOOST_AUTO_TEST_SUITE(subjective_billing_test)

BOOST_AUTO_TEST_CASE( subjective_bill_test ) {

   fc::logger log;

   transaction_id_type id1 = sha256::hash( "1" );
   transaction_id_type id2 = sha256::hash( "2" );
   transaction_id_type id3 = sha256::hash( "3" );
   account_name a = "a"_n;
   account_name b = "b"_n;
   account_name c = "c"_n;

   const auto now = time_point::now();
   const fc::time_point_sec now_sec{now};

   subjective_billing timing_sub_bill;
   const auto halftime = now + fc::milliseconds(timing_sub_bill.get_expired_accumulator_average_window() * subjective_billing::subjective_time_interval_ms / 2);
   const auto endtime = now + fc::milliseconds(timing_sub_bill.get_expired_accumulator_average_window() * subjective_billing::subjective_time_interval_ms);


   {  // Failed transactions remain until expired in subjective billing.
      subjective_billing sub_bill;

      sub_bill.subjective_bill( id1, now_sec, a, fc::microseconds( 13 ) );
      sub_bill.subjective_bill( id2, now_sec, a, fc::microseconds( 11 ) );
      sub_bill.subjective_bill( id3, now_sec, b, fc::microseconds( 9 ) );

      BOOST_CHECK_EQUAL( 13+11, sub_bill.get_subjective_bill(a, now) );
      BOOST_CHECK_EQUAL( 9, sub_bill.get_subjective_bill(b, now) );

      sub_bill.on_block(log, {}, now);

      BOOST_CHECK_EQUAL( 13+11, sub_bill.get_subjective_bill(a, now) );
      BOOST_CHECK_EQUAL( 9, sub_bill.get_subjective_bill(b, now) );

      // expires transactions but leaves them in the decay at full value
      sub_bill.remove_expired( log, now + fc::microseconds(1), now, [](){ return false; } );

      BOOST_CHECK_EQUAL( 13+11, sub_bill.get_subjective_bill(a, now) );
      BOOST_CHECK_EQUAL( 9, sub_bill.get_subjective_bill(b, now) );
      BOOST_CHECK_EQUAL( 0, sub_bill.get_subjective_bill(c, now) );

      // ensure that the value decays away at the window
      BOOST_CHECK_EQUAL( 0, sub_bill.get_subjective_bill(a, endtime) );
      BOOST_CHECK_EQUAL( 0, sub_bill.get_subjective_bill(b, endtime) );
      BOOST_CHECK_EQUAL( 0, sub_bill.get_subjective_bill(c, endtime) );
   }
   {  // db_read_mode HEAD mode, so transactions are immediately reverted
      subjective_billing sub_bill;

      sub_bill.subjective_bill( id1, now_sec, a, fc::microseconds( 23 ) );
      sub_bill.subjective_bill( id2, now_sec, a, fc::microseconds( 19 ) );
      sub_bill.subjective_bill( id3, now_sec, b, fc::microseconds( 7 ) );

      BOOST_CHECK_EQUAL( 23+19, sub_bill.get_subjective_bill(a, now) );
      BOOST_CHECK_EQUAL( 7, sub_bill.get_subjective_bill(b, now) );

      sub_bill.on_block(log, {}, now); // have not seen any of the transactions come back yet

      BOOST_CHECK_EQUAL( 23+19, sub_bill.get_subjective_bill(a, now) );
      BOOST_CHECK_EQUAL( 7, sub_bill.get_subjective_bill(b, now) );

      sub_bill.on_block(log, {}, now);
      sub_bill.remove_subjective_billing( id1, 0 ); // simulate seeing id1 come back in block (this is what on_block would do)

      BOOST_CHECK_EQUAL( 19, sub_bill.get_subjective_bill(a, now) );
      BOOST_CHECK_EQUAL( 7, sub_bill.get_subjective_bill(b, now) );
   }
   { // failed handling logic, decay with repeated failures should be exponential, single failures should be linear
      subjective_billing sub_bill;

      sub_bill.subjective_bill_failure(a, fc::microseconds(1024), now);
      sub_bill.subjective_bill_failure(b, fc::microseconds(1024), now);
      BOOST_CHECK_EQUAL( 1024, sub_bill.get_subjective_bill(a, now) );
      BOOST_CHECK_EQUAL( 1024, sub_bill.get_subjective_bill(b, now) );

      sub_bill.subjective_bill_failure(a, fc::microseconds(1024), halftime);
      BOOST_CHECK_EQUAL( 512 + 1024, sub_bill.get_subjective_bill(a, halftime) );
      BOOST_CHECK_EQUAL( 512, sub_bill.get_subjective_bill(b, halftime) );

      sub_bill.subjective_bill_failure(a, fc::microseconds(1024), endtime);
      BOOST_CHECK_EQUAL( 256 + 512 + 1024, sub_bill.get_subjective_bill(a, endtime) );
      BOOST_CHECK_EQUAL( 0, sub_bill.get_subjective_bill(b, endtime) );
   }

   { // expired handling logic, full billing until expiration then failed/decay logic
      subjective_billing sub_bill;

      sub_bill.subjective_bill( id1, now_sec, a, fc::microseconds( 1024 ) );
      sub_bill.subjective_bill( id2, fc::time_point_sec{now + fc::seconds(1)}, a, fc::microseconds( 1024 ) );
      sub_bill.subjective_bill( id3, now_sec, b, fc::microseconds( 1024 ) );
      BOOST_CHECK_EQUAL( 1024 + 1024, sub_bill.get_subjective_bill(a, now) );
      BOOST_CHECK_EQUAL( 1024, sub_bill.get_subjective_bill(b, now) );

      sub_bill.remove_expired( log, now, now, [](){ return false; } );
      BOOST_CHECK_EQUAL( 1024 + 1024, sub_bill.get_subjective_bill(a, now) );
      BOOST_CHECK_EQUAL( 1024, sub_bill.get_subjective_bill(b, now) );

      BOOST_CHECK_EQUAL( 512 + 1024, sub_bill.get_subjective_bill(a, halftime) );
      BOOST_CHECK_EQUAL( 512, sub_bill.get_subjective_bill(b, halftime) );

      BOOST_CHECK_EQUAL( 1024, sub_bill.get_subjective_bill(a, endtime) );
      BOOST_CHECK_EQUAL( 0, sub_bill.get_subjective_bill(b, endtime) );

      sub_bill.remove_expired( log, now + fc::seconds(1), now, [](){ return false; } );
      BOOST_CHECK_EQUAL( 1024 + 1024, sub_bill.get_subjective_bill(a, now) );
      BOOST_CHECK_EQUAL( 1024, sub_bill.get_subjective_bill(b, now) );

      BOOST_CHECK_EQUAL( 512 + 512, sub_bill.get_subjective_bill(a, halftime) );
      BOOST_CHECK_EQUAL( 512, sub_bill.get_subjective_bill(b, halftime) );

      BOOST_CHECK_EQUAL( 0, sub_bill.get_subjective_bill(a, endtime) );
      BOOST_CHECK_EQUAL( 0, sub_bill.get_subjective_bill(b, endtime) );
   }

}

BOOST_AUTO_TEST_SUITE_END()

}

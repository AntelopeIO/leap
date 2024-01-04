#include <fc/exception/exception.hpp>
#include <fc/io/raw.hpp>

#include <boost/test/unit_test.hpp>
#include <boost/dynamic_bitset.hpp>

using namespace fc;

BOOST_AUTO_TEST_SUITE(raw_test_suite)


BOOST_AUTO_TEST_CASE(dynamic_bitset_test)
{
   constexpr uint8_t bits = 0b00011110;
   boost::dynamic_bitset<uint8_t> bs1(8, bits); // bit set size 8
   
   char buff[4];
   datastream<char*> ds(buff, sizeof(buff));

   fc::raw::pack( ds, bs1 );

   boost::dynamic_bitset<uint8_t> bs2(8);
   ds.seekp(0);
   fc::raw::unpack( ds, bs2 );

   // 0b00011110
   BOOST_CHECK(!bs2.test(0));
   BOOST_CHECK(bs2.test(1));
   BOOST_CHECK(bs2.test(2));
   BOOST_CHECK(bs2.test(2));
   BOOST_CHECK(bs2.test(3));
   BOOST_CHECK(bs2.test(4));
   BOOST_CHECK(!bs2.test(5));
   BOOST_CHECK(!bs2.test(6));
   BOOST_CHECK(!bs2.test(7));
}

BOOST_AUTO_TEST_SUITE_END()

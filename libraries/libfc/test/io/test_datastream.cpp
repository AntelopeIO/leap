#include <fc/exception/exception.hpp>
#include <fc/io/datastream.hpp>

#include <boost/test/unit_test.hpp>
#include <boost/dynamic_bitset.hpp>

using namespace fc;

BOOST_AUTO_TEST_SUITE(datastream_test_suite)


BOOST_AUTO_TEST_CASE(dynamic_bitset_test)
{
   constexpr uint8_t bits = 0b00011110;
   boost::dynamic_bitset<uint8_t> bs1(8, bits); // bit set size 8
   
   char buff[4];
   datastream<char*> ds(buff, sizeof(buff));

   // write bit set to stream (vector of uint32_t)
   ds << bs1;
   if (static_cast<uint8_t>(buff[0]) == bits) {
      // on big endian system: first byte is most significant and other bytes are 0
      BOOST_CHECK_EQUAL(static_cast<uint8_t>(buff[3]), 0);
   } else if (static_cast<uint8_t>(buff[3]) == bits) {
      // on little endian system: last byte is most significant and other bytes are 0
      BOOST_CHECK_EQUAL(static_cast<uint8_t>(buff[0]), 0);
   } else {
      BOOST_CHECK(false);
   }
   BOOST_CHECK_EQUAL(static_cast<uint8_t>(buff[1]), 0);
   BOOST_CHECK_EQUAL(static_cast<uint8_t>(buff[2]), 0);

   // read from stream to construct bit set (vector of uint32_t)
   boost::dynamic_bitset<uint8_t> bs2(8);
   ds.seekp(0);
   ds >> bs2;
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

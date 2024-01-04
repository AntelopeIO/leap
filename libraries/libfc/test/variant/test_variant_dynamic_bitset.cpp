#include <fc/variant_dynamic_bitset.hpp>
#include <fc/variant_object.hpp>
#include <fc/exception/exception.hpp>

#include <boost/test/unit_test.hpp>

#include <string>

using namespace fc;
using std::string;

BOOST_AUTO_TEST_SUITE(dynamic_bitset_test_suite)

BOOST_AUTO_TEST_CASE(dynamic_bitset_test)
{
   constexpr uint8_t bits = 0b0000000001010100;
   boost::dynamic_bitset<uint8_t> bs(16, bits); // 2 blocks of uint8_t

   fc::mutable_variant_object mu;
   mu("bs", bs);

   // a vector of 2 blocks
   const variants& vars = mu["bs"].get_array();
   BOOST_CHECK_EQUAL(vars.size(), 2u);

   // blocks can be in any order
   if (vars[0].as<uint32_t>() == bits ) {
      BOOST_CHECK_EQUAL(vars[1].as<uint32_t>(), 0u);
   } else if (vars[1].as<uint32_t>() == bits ) {
      BOOST_CHECK_EQUAL(vars[0].as<uint32_t>(), 0u);
   } else {
      BOOST_CHECK(false);
   }
}

BOOST_AUTO_TEST_SUITE_END()

#include <boost/test/unit_test.hpp>

#include <fc/variant_object.hpp>
#include <fc/exception/exception.hpp>
#include <fc/crypto/base64.hpp>
#include <string>

using namespace fc;
using std::string;

BOOST_AUTO_TEST_SUITE(variant_estimated_size_suite)
BOOST_AUTO_TEST_CASE(null_variant_estimated_size_test)
{
   constexpr nullptr_t np=nullptr;

   variant v;
   variant v_nullptr(np);

   BOOST_CHECK_EQUAL(v.estimated_size(), sizeof(variant));
   BOOST_CHECK_EQUAL(v_nullptr.estimated_size(), sizeof(variant));
}

BOOST_AUTO_TEST_CASE(int64_variant_estimated_size_test)
{
   int64_t i = 1;
   int32_t j = 2;
   int16_t k = 3;
   int8_t l = 4;

   variant v_int_64(i);
   variant v_int_32(j);
   variant v_int_16(k);
   variant v_int_8(l);

   BOOST_CHECK_EQUAL(v_int_64.estimated_size(), sizeof(variant));
   BOOST_CHECK_EQUAL(v_int_32.estimated_size(), sizeof(variant));
   BOOST_CHECK_EQUAL(v_int_16.estimated_size(), sizeof(variant));
   BOOST_CHECK_EQUAL(v_int_8.estimated_size(), sizeof(variant));
}

BOOST_AUTO_TEST_CASE(uint64_variant_estimated_size_test)
{
   uint64_t i = 1;
   uint32_t j = 2;
   uint16_t k = 3;
   uint8_t l = 4;

   variant v_uint_64(i);
   variant v_uint_32(j);
   variant v_uint_16(k);
   variant v_uint_8(l);

   BOOST_CHECK_EQUAL(v_uint_64.estimated_size(), sizeof(variant));
   BOOST_CHECK_EQUAL(v_uint_32.estimated_size(), sizeof(variant));
   BOOST_CHECK_EQUAL(v_uint_16.estimated_size(), sizeof(variant));
   BOOST_CHECK_EQUAL(v_uint_8.estimated_size(), sizeof(variant));
}

BOOST_AUTO_TEST_CASE(double_variant_estimated_size_test)
{
   float f = 3.14;
   double d = 12.345;

   variant v_float(f);
   variant v_double(d);

   BOOST_CHECK_EQUAL(v_float.estimated_size(), sizeof(variant));
   BOOST_CHECK_EQUAL(v_double.estimated_size(), sizeof(variant));
}

BOOST_AUTO_TEST_CASE(string_variant_estimated_size_test)
{
   char c[] = "Hello World";
   const char* cc = "Goodbye";
   wchar_t wc[] = L"0123456789";
   const wchar_t* cwc = L"foo";
   string s = "abcdefghijklmnopqrstuvwxyz";

   variant v_char(c);
   variant v_const_char(cc);
   variant v_wchar(wc);
   variant v_const_wchar(cwc);
   variant v_string(s);

   BOOST_CHECK_EQUAL(v_char.estimated_size(), 11 + sizeof(variant) + sizeof(string));
   BOOST_CHECK_EQUAL(v_const_char.estimated_size(), 7 + sizeof(variant) + sizeof(string));
   BOOST_CHECK_EQUAL(v_wchar.estimated_size(), 10 + sizeof(variant) + sizeof(string));
   BOOST_CHECK_EQUAL(v_const_wchar.estimated_size(), 3 + sizeof(variant) + sizeof(string));
   BOOST_CHECK_EQUAL(v_string.estimated_size(), 26 + sizeof(variant) + sizeof(string));
}

BOOST_AUTO_TEST_CASE(blob_variant_estimated_size_test)
{
   blob bl;
   bl.data.push_back('f');
   bl.data.push_back('o');
   bl.data.push_back('o');

   variant v_blob(bl);

   BOOST_CHECK_EQUAL(v_blob.estimated_size(), 3 + sizeof(variant) + sizeof(blob));
}

BOOST_AUTO_TEST_CASE(variant_object_variant_estimated_size_test)
{
   string k1 = "key_bool";
   string k2 = "key_string";
   string k3 = "key_int16";
   string k4 = "key_blob"; // 35 + 4 * sizeof(string)

   bool b = false;
   string s = "HelloWorld"; // 10 + sizeof(string)
   int16_t i = 123;
   blob bl;
   bl.data.push_back('b');
   bl.data.push_back('a');
   bl.data.push_back('r'); // 3 + sizeof(blob)

   variant v_bool(b);
   variant v_string(s);
   variant v_int16(i);
   variant v_blob(bl); // + 4 * sizeof(variant)

   mutable_variant_object mu;
   mu(k1, b);
   mu(k2, v_string);
   mu(k3, v_int16);
   mu(k4, bl);
   variant_object vo(mu); // + sizeof(variant_object) + sizeof(std::vector<variant_object::entry>)
   variant v_vo(vo); // + sizeof(variant)

   BOOST_CHECK_EQUAL(vo.estimated_size(), 48 + 5 * sizeof(string) + sizeof(blob) + 4 * sizeof(variant) +
                     sizeof(variant_object) + sizeof(std::vector<variant_object::entry>));
   BOOST_CHECK_EQUAL(v_vo.estimated_size(), 48 + 5 * sizeof(string) + sizeof(blob) + 5 * sizeof(variant) +
                     sizeof(variant_object) + sizeof(std::vector<variant_object::entry>));
}

BOOST_AUTO_TEST_CASE(array_variant_estimated_size_test)
{
   bool b = true;
   wchar_t wc[] = L"Goodbye"; // 7 + sizeof(string)
   uint32_t i = 54321;

   variant v_bool(b);
   variant v_wchar(wc);
   variant v_uint32(i); // + 3 * sizeof(variant)

   variants vs; // + sizeof(variants)
   vs.push_back(v_bool);
   vs.push_back(v_wchar);
   vs.push_back(v_uint32);

   variant v_variants(vs); // + sizeof(variant)
   BOOST_CHECK_EQUAL(v_variants.estimated_size(), 7 + sizeof(string) + 4 * sizeof(variant) + sizeof(variants));
}

BOOST_AUTO_TEST_SUITE_END()

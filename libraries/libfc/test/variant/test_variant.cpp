#include <fc/variant_object.hpp>
#include <fc/exception/exception.hpp>
#include <fc/crypto/base64.hpp>

#include <boost/test/unit_test.hpp>
#include <boost/dynamic_bitset.hpp>

#include <string>

using namespace fc;
using std::string;

BOOST_AUTO_TEST_SUITE(variant_test_suite)
BOOST_AUTO_TEST_CASE(mutable_variant_object_test)
{
  // no BOOST_CHECK / BOOST_REQUIRE, just see that this compiles on all supported platforms
  try {
    variant v(42);
    variant_object vo;
    mutable_variant_object mvo;
    variants vs;
    vs.push_back(mutable_variant_object("level", "debug")("color", v));
    vs.push_back(mutable_variant_object()("level", "debug")("color", v));
    vs.push_back(mutable_variant_object("level", "debug")("color", "green"));
    vs.push_back(mutable_variant_object()("level", "debug")("color", "green"));
    vs.push_back(mutable_variant_object("level", "debug")(vo));
    vs.push_back(mutable_variant_object()("level", "debug")(mvo));
    vs.push_back(mutable_variant_object("level", "debug").set("color", v));
    vs.push_back(mutable_variant_object()("level", "debug").set("color", v));
    vs.push_back(mutable_variant_object("level", "debug").set("color", "green"));
    vs.push_back(mutable_variant_object()("level", "debug").set("color", "green"));
  }
  FC_LOG_AND_RETHROW();
}

BOOST_AUTO_TEST_CASE(variant_format_string_limited)
{
   constexpr size_t long_rep_char_num = 1024;
   const std::string a_long_list = std::string(long_rep_char_num, 'a');
   const std::string b_long_list = std::string(long_rep_char_num, 'b');
   {
      const string format = "${a} ${b} ${c}";
      fc::mutable_variant_object mu;
      mu( "a", string( long_rep_char_num, 'a' ) );
      mu( "b", string( long_rep_char_num, 'b' ) );
      mu( "c", string( long_rep_char_num, 'c' ) );
      const string result = fc::format_string( format, mu, true );
      BOOST_CHECK_LT(0u, mu.size());
      const auto arg_limit_size = (1024 - format.size()) / mu.size();
      BOOST_CHECK_EQUAL( result, string(arg_limit_size, 'a' ) + "... " + string(arg_limit_size, 'b' ) + "... " + string(arg_limit_size, 'c' ) + "..." );
      BOOST_CHECK_LT(result.size(), 1024 + 3 * mu.size());
   }
   {  // verify object, array, blob, and string, all exceeds limits, fold display for each
      fc::mutable_variant_object mu;
      mu( "str", a_long_list );
      mu( "obj", variant_object(mutable_variant_object()("a", a_long_list)("b", b_long_list)) );
      mu( "arr", variants{variant(a_long_list), variant(b_long_list)} );
      mu( "blob", blob({std::vector<char>(a_long_list.begin(), a_long_list.end())}) );
      const string format_prefix = "Format string test: ";
      const string format_str = format_prefix + "${str} ${obj} ${arr} {blob}";
      const string result = fc::format_string( format_str, mu, true );
      BOOST_CHECK_LT(0u, mu.size());
      const auto arg_limit_size = (1024 - format_str.size()) / mu.size();
      BOOST_CHECK_EQUAL( result, format_prefix + a_long_list.substr(0, arg_limit_size) + "..." + " ${obj} ${arr} {blob}");
      BOOST_CHECK_LT(result.size(), 1024 + 3 * mu.size());
   }
   {  // verify object, array can be displayed properly
      const string format_prefix = "Format string test: ";
      const string format_str = format_prefix + "${str} ${obj} ${arr} ${blob} ${var}";
      BOOST_CHECK_LT(format_str.size(), 1024u);
      const size_t short_rep_char_num = (1024 - format_str.size()) / 5 - 1;
      const std::string a_short_list = std::string(short_rep_char_num, 'a');
      const std::string b_short_list = std::string(short_rep_char_num / 3, 'b');
      const std::string c_short_list = std::string(short_rep_char_num / 3, 'c');
      const std::string d_short_list = std::string(short_rep_char_num / 3, 'd');
      const std::string e_short_list = std::string(short_rep_char_num / 3, 'e');
      const std::string f_short_list = std::string(short_rep_char_num, 'f');
      const std::string g_short_list = std::string(short_rep_char_num, 'g');
      fc::mutable_variant_object mu;
      const variant_object vo(mutable_variant_object()("b", b_short_list)("c", c_short_list));
      const variants variant_list{variant(d_short_list), variant(e_short_list)};
      const blob a_blob({std::vector<char>(f_short_list.begin(), f_short_list.end())});
      const variant a_variant(g_short_list);
      mu( "str",  a_short_list );
      mu( "obj",  vo);
      mu( "arr",  variant_list);
      mu( "blob", a_blob);
      mu( "var",  a_variant);
      const string result = fc::format_string( format_str, mu, true );
      const string target_result = format_prefix + a_short_list + " " +
                                   "{" + "\"b\":\"" + b_short_list + "\",\"c\":\"" + c_short_list + "\"}" + " " +
                                   "[\"" + d_short_list + "\",\"" + e_short_list + "\"]" + " " +
                                   base64_encode( a_blob.data.data(), a_blob.data.size() ) + " " +
                                   g_short_list;

      BOOST_CHECK_EQUAL( result, target_result);
      BOOST_CHECK_LT(result.size(), 1024 + 3 * mu.size());
   }
}

BOOST_AUTO_TEST_CASE(variant_blob)
{
   // Some test cases from https://github.com/ReneNyffenegger/cpp-base64
   {
      std::string a17_orig = "aaaaaaaaaaaaaaaaa";
      std::string a17_encoded = "YWFhYWFhYWFhYWFhYWFhYWE=";
      fc::mutable_variant_object mu;
      mu("blob", blob{{a17_orig.begin(), a17_orig.end()}});
      mu("str", a17_encoded);

      BOOST_CHECK_EQUAL(mu["blob"].as_string(), a17_encoded);
      std::vector<char> b64 = mu["str"].as_blob().data;
      std::string_view b64_str(b64.data(), b64.size());
      BOOST_CHECK_EQUAL(b64_str, a17_orig);
   }
   {
      std::string s_6364 = "\x03" "\xef" "\xff" "\xf9";
      std::string s_6364_encoded = "A+//+Q==";
      fc::mutable_variant_object mu;
      mu("blob", blob{{s_6364.begin(), s_6364.end()}});
      mu("str", s_6364_encoded);

      BOOST_CHECK_EQUAL(mu["blob"].as_string(), s_6364_encoded);
      std::vector<char> b64 = mu["str"].as_blob().data;
      std::string_view b64_str(b64.data(), b64.size());
      BOOST_CHECK_EQUAL(b64_str, s_6364);
   }
   {
      std::string org = "abc";
      std::string encoded = "YWJj";

      fc::mutable_variant_object mu;
      mu("blob", blob{{org.begin(), org.end()}});
      mu("str", encoded);

      BOOST_CHECK_EQUAL(mu["blob"].as_string(), encoded);
      std::vector<char> b64 = mu["str"].as_blob().data;
      std::string_view b64_str(b64.data(), b64.size());
      BOOST_CHECK_EQUAL(b64_str, org);
   }
}

BOOST_AUTO_TEST_CASE(variant_blob_backwards_compatibility)
{
   // pre-5.0 variant would add an additional `=` as a flag that the blob data was base64 encoded
   // verify variant can process encoded data with the extra `=`
   {
      std::string a17_orig = "aaaaaaaaaaaaaaaaa";
      std::string a17_encoded = "YWFhYWFhYWFhYWFhYWFhYWE=";
      std::string a17_encoded_old = a17_encoded + '=';
      fc::mutable_variant_object mu;
      mu("blob", blob{{a17_orig.begin(), a17_orig.end()}});
      mu("str", a17_encoded_old);

      BOOST_CHECK_EQUAL(mu["blob"].as_string(), a17_encoded);
      std::vector<char> b64 = mu["str"].as_blob().data;
      std::string_view b64_str(b64.data(), b64.size());
      BOOST_CHECK_EQUAL(b64_str, a17_orig);
   }
   {
      std::string org = "abc";
      std::string encoded = "YWJj";
      std::string encoded_old = encoded + '=';

      fc::mutable_variant_object mu;
      mu("blob", blob{{org.begin(), org.end()}});
      mu("str", encoded_old);

      BOOST_CHECK_EQUAL(mu["blob"].as_string(), encoded);
      std::vector<char> b64 = mu["str"].as_blob().data;
      std::string_view b64_str(b64.data(), b64.size());
      BOOST_CHECK_EQUAL(b64_str, org);
   }
}

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

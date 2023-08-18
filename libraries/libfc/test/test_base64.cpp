#include <boost/test/unit_test.hpp>

#include <fc/crypto/base64.hpp>
#include <fc/exception/exception.hpp>

using namespace fc;
using namespace std::literals;

BOOST_AUTO_TEST_SUITE(base64)

BOOST_AUTO_TEST_CASE(base64enc) try {
   auto input = "abc123$&()'?\xb4\xf5\x01\xfa~a"s;
   auto expected_output = "YWJjMTIzJCYoKSc/tPUB+n5h"s;

   BOOST_CHECK_EQUAL(expected_output, base64_encode(input));
} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(base64urlenc) try {
   auto input = "abc123$&()'?\xb4\xf5\x01\xfa~a"s;
   auto expected_output = "YWJjMTIzJCYoKSc_tPUB-n5h"s;

   BOOST_CHECK_EQUAL(expected_output, base64url_encode(input));
} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(base64dec) try {
   auto input = "YWJjMTIzJCYoKSc/tPUB+n5h"s;
   auto expected_output = "abc123$&()'?\xb4\xf5\x01\xfa~a"s;

   BOOST_CHECK_EQUAL(expected_output, base64_decode(input));
} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(base64urldec) try {
   auto input = "YWJjMTIzJCYoKSc_tPUB-n5h"s;
   auto expected_output = "abc123$&()'?\xb4\xf5\x01\xfa~a"s;

   BOOST_CHECK_EQUAL(expected_output, base64url_decode(input));
} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(base64dec_extraequals) try {
   auto input = "YWJjMTIzJCYoKSc/tPUB+n5h========="s;
   auto expected_output = "abc123$&()'?\xb4\xf5\x01\xfa~a"s;

   BOOST_CHECK_EXCEPTION(base64_decode(input), fc::exception, [](const fc::exception& e) {
      return e.to_detail_string().find("encountered non-base64 character") != std::string::npos;
   });
} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(base64dec_bad_stuff) try {
   auto input = "YWJjMTIzJCYoKSc/tPU$B+n5h="s;

   BOOST_CHECK_EXCEPTION(base64_decode(input), fc::exception, [](const fc::exception& e) {
      return e.to_detail_string().find("encountered non-base64 character") != std::string::npos;
   });
} FC_LOG_AND_RETHROW();

// tests from https://github.com/ReneNyffenegger/cpp-base64/blob/master/test.cpp
BOOST_AUTO_TEST_CASE(base64_cpp_base64_tests) try {
   //
   // Note: this file must be encoded in UTF-8
   // for the following test, otherwise, the test item
   // fails.
   //
   const std::string orig =
      "René Nyffenegger\n"
      "http://www.renenyffenegger.ch\n"
      "passion for data\n";

   std::string encoded = base64_encode(reinterpret_cast<const unsigned char*>(orig.c_str()), orig.length());
   std::string decoded = base64_decode(encoded);

   BOOST_CHECK_EQUAL(encoded, "UmVuw6kgTnlmZmVuZWdnZXIKaHR0cDovL3d3dy5yZW5lbnlmZmVuZWdnZXIuY2gKcGFzc2lvbiBmb3IgZGF0YQo=");
   BOOST_CHECK_EQUAL(decoded, orig);

   // Test all possibilites of fill bytes (none, one =, two ==)
   // References calculated with: https://www.base64encode.org/

   std::string rest0_original = "abc";
   std::string rest0_reference = "YWJj";

   std::string rest0_encoded = base64_encode(reinterpret_cast<const unsigned char*>(rest0_original.c_str()),
                                             rest0_original.length());
   std::string rest0_decoded = base64_decode(rest0_encoded);

   BOOST_CHECK_EQUAL(rest0_decoded, rest0_original);
   BOOST_CHECK_EQUAL(rest0_reference, rest0_encoded);

   std::string rest1_original = "abcd";
   std::string rest1_reference = "YWJjZA==";

   std::string rest1_encoded = base64_encode(reinterpret_cast<const unsigned char*>(rest1_original.c_str()),
                                             rest1_original.length());
   std::string rest1_decoded = base64_decode(rest1_encoded);

   BOOST_CHECK_EQUAL(rest1_decoded, rest1_original);
   BOOST_CHECK_EQUAL(rest1_reference, rest1_encoded);

   std::string rest2_original = "abcde";
   std::string rest2_reference = "YWJjZGU=";

   std::string rest2_encoded = base64_encode(reinterpret_cast<const unsigned char*>(rest2_original.c_str()),
                                             rest2_original.length());
   std::string rest2_decoded = base64_decode(rest2_encoded);

   BOOST_CHECK_EQUAL(rest2_decoded, rest2_original);
   BOOST_CHECK_EQUAL(rest2_reference, rest2_encoded);

   // --------------------------------------------------------------
   //
   // Data that is 17 bytes long requires one padding byte when
   // base-64 encoded. Such an encoded string could not correctly
   // be decoded when encoded with «url semantics». This bug
   // was discovered by https://github.com/kosniaz. The following
   // test checks if this bug was fixed:
   //
   std::string a17_orig    = "aaaaaaaaaaaaaaaaa";
   std::string a17_encoded     = base64_encode(a17_orig);
   std::string a17_encoded_url = base64url_encode(a17_orig);

   BOOST_CHECK_EQUAL(a17_encoded, "YWFhYWFhYWFhYWFhYWFhYWE=");
   BOOST_CHECK_EQUAL(a17_encoded_url, "YWFhYWFhYWFhYWFhYWFhYWE.");
   BOOST_CHECK_EQUAL(base64_decode(a17_encoded_url), a17_orig);
   BOOST_CHECK_EQUAL(base64_decode(a17_encoded), a17_orig);

   // --------------------------------------------------------------

   // characters 63 and 64 / URL encoding

   std::string s_6364 = "\x03" "\xef" "\xff" "\xf9";

   std::string s_6364_encoded     = base64_encode(s_6364);
   std::string s_6364_encoded_url = base64url_encode(s_6364);

   BOOST_CHECK_EQUAL(s_6364_encoded, "A+//+Q==");
   BOOST_CHECK_EQUAL(s_6364_encoded_url, "A-__-Q..");
   BOOST_CHECK_EQUAL(base64_decode(s_6364_encoded), s_6364);
   BOOST_CHECK_EQUAL(base64_decode(s_6364_encoded_url), s_6364);

   // ----------------------------------------------

   std::string unpadded_input   = "YWJjZGVmZw"; // Note the 'missing' "=="
   std::string unpadded_decoded = base64_decode(unpadded_input);
   BOOST_CHECK_EQUAL(unpadded_decoded, "abcdefg");

   unpadded_input   = "YWJjZGU"; // Note the 'missing' "="
   unpadded_decoded = base64_decode(unpadded_input);
   BOOST_CHECK_EQUAL(unpadded_decoded, "abcde");

   unpadded_input   = "";
   unpadded_decoded = base64_decode(unpadded_input);
   BOOST_CHECK_EQUAL(unpadded_decoded, "");

   unpadded_input   = "YQ";
   unpadded_decoded = base64_decode(unpadded_input);
   BOOST_CHECK_EQUAL(unpadded_decoded, "a");

   unpadded_input   = "YWI";
   unpadded_decoded = base64_decode(unpadded_input);
   BOOST_CHECK_EQUAL(unpadded_decoded, "ab");

   // --------------------------------------------------------------
   //
   //    2022-11-01
   //       Replace
   //          encoded_string[…] with encoded_sring.at(…)
   //       in
   //          decode()
   //
   std::string not_null_terminated = std::string(1, 'a');
   BOOST_CHECK_THROW(base64_decode(not_null_terminated), std::out_of_range);

   // --------------------------------------------------------------
   //
   // Test the string_view interface (which required C++17)
   //
   std::string_view sv_orig    = "foobarbaz";
   std::string sv_encoded = base64_encode(sv_orig);

   BOOST_CHECK_EQUAL(sv_encoded, "Zm9vYmFyYmF6");

   std::string sv_decoded = base64_decode(sv_encoded);

   BOOST_CHECK_EQUAL(sv_decoded, sv_orig);

} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_SUITE_END()
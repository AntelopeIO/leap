#include <boost/test/unit_test.hpp>

#include <fc/string.hpp>
#include <fc/exception/exception.hpp>

using namespace fc;
using namespace std::literals;

BOOST_AUTO_TEST_SUITE(escape_str_test)

BOOST_AUTO_TEST_CASE(escape_control_chars) try {
   const std::string escape_input_str = "\b\f\n\r\t\\\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f"
                                        "\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1a\x1b\x1c\x1d\x1e\x1f";
   std::string escaped_str = "\\u0008\\u000c\\n\\r\\t\\\\\\u0001\\u0002\\u0003\\u0004\\u0005\\u0006\\u0007\\u0008\\t\\n\\u000b\\u000c\\r\\u000e\\u000f"
                             "\\u0010\\u0011\\u0012\\u0013\\u0014\\u0015\\u0016\\u0017\\u0018\\u0019\\u001a\\u001b\\u001c\\u001d\\u001e\\u001f";

   std::string input = escape_input_str;
   BOOST_CHECK_EQUAL(escape_str(input).first, escaped_str);

   input = escape_input_str;
   escaped_str = "\\u0008\\u000c\n"
                 "\r\t\\\\u0001\\u0002\\u0003\\u0004\\u0005\\u0006\\u0007\\u0008\t\n"
                 "\\u000b\\u000c\r\\u000e\\u000f\\u0010\\u0011\\u0012\\u0013\\u0014\\u0015\\u0016\\u0017\\u0018\\u0019\\u001a\\u001b\\u001c\\u001d\\u001e\\u001f";
   BOOST_CHECK_EQUAL(escape_str(input, fc::escape_control_chars::off).first, escaped_str);

} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(empty) try {
   std::string input;
   BOOST_CHECK_EQUAL(escape_str(input, fc::escape_control_chars::on, 256, "").first, "");

   input = "";
   BOOST_CHECK_EQUAL(escape_str(input, fc::escape_control_chars::off, 512, {}).first, "");
} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(truncate) try {
   const std::string repeat_512_chars(512, 'a');
   const std::string repeat_256_chars(256, 'a');

   std::string input = repeat_512_chars;
   BOOST_CHECK_EQUAL(escape_str(input, fc::escape_control_chars::on, 256, "").first, repeat_256_chars);

   input = repeat_512_chars;
   BOOST_CHECK_EQUAL(escape_str(input, fc::escape_control_chars::on, 256, {}).first, repeat_256_chars);

   input = repeat_512_chars;
   BOOST_CHECK_EQUAL(escape_str(input, fc::escape_control_chars::on, 256).first, repeat_256_chars + "...");

   input = repeat_512_chars;
   BOOST_CHECK_EQUAL(escape_str(input, fc::escape_control_chars::on, 256, "<-the end->").first, repeat_256_chars + "<-the end->");
} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(modify) try {
   const std::string repeat_512_chars(512, 'a');
   const std::string repeat_256_chars(256, 'a');

   std::string input = repeat_512_chars;
   BOOST_CHECK(escape_str(input, fc::escape_control_chars::on, 256, "").second);

   input = repeat_512_chars;
   BOOST_CHECK(escape_str(input, fc::escape_control_chars::on, 256, {}).second);

   input = repeat_512_chars;
   BOOST_CHECK(escape_str(input, fc::escape_control_chars::on, 256).second);

   input = repeat_512_chars;
   BOOST_CHECK(!escape_str(input, fc::escape_control_chars::on, 512).second);

   input = repeat_512_chars;
   BOOST_CHECK(!escape_str(input, fc::escape_control_chars::on).second);

   input = repeat_512_chars;
   BOOST_CHECK(!escape_str(input, fc::escape_control_chars::on, 1024).second);

   input = "";
   BOOST_CHECK(!escape_str(input, fc::escape_control_chars::on, 1024).second);

   input = "hello";
   BOOST_CHECK(!escape_str(input, fc::escape_control_chars::on, 1024).second);

   input = "\n";
   BOOST_CHECK(escape_str(input, fc::escape_control_chars::on, 1024).second);

   input ="\xb4";
   BOOST_CHECK(escape_str(input, fc::escape_control_chars::on, 1024).second);
   BOOST_CHECK_EQUAL(input, "");

} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(remove_invalid_utf8) try {
   auto input = "abc123$&()'?\xb4\xf5\x01\xfa~a"s; // remove invalid utf8 values, \x01 => \u0001
   auto expected_output = "abc123$&()'?\\u0001~a"s;

   BOOST_CHECK_EQUAL(escape_str(input).first, expected_output);
} FC_LOG_AND_RETHROW();


BOOST_AUTO_TEST_SUITE_END()
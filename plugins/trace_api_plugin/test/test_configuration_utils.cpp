#include <boost/test/unit_test.hpp>
#include <list>

#include <eosio/trace_api/configuration_utils.hpp>
#include <eosio/trace_api/test_common.hpp>
#include <fc/io/cfile.hpp>

using namespace eosio;
using namespace eosio::trace_api::configuration_utils;

struct temp_file_fixture {
   std::string create_temp_file( const std::string& contents ) {
      auto& tmp = temps.emplace_back("w");
      auto& file = tmp.file();
      file.write(contents.data(), contents.size());
      file.close();
      return file.get_file_path().string();
   }
   std::list<fc::temp_cfile> temps;
};

BOOST_AUTO_TEST_SUITE(configuration_utils_tests)
   BOOST_AUTO_TEST_CASE(parse_kv_pairs_test)
   {
      using spair = std::pair<std::string, std::string>;

      // basic
      BOOST_TEST( parse_kv_pairs("a=b") == spair("a", "b") );
      BOOST_TEST( parse_kv_pairs("a==b") == spair("a", "=b") );
      BOOST_TEST( parse_kv_pairs("a={}:\"=") == spair("a", "{}:\"=") );
      BOOST_TEST( parse_kv_pairs("{}:\"=a") == spair("{}:\"", "a") );

      // missing key not OK
      BOOST_REQUIRE_THROW(parse_kv_pairs("=b"), chain::plugin_config_exception);

      // missing value not OK
      BOOST_REQUIRE_THROW(parse_kv_pairs("a="), chain::plugin_config_exception);

      // missing = not OK
      BOOST_REQUIRE_THROW(parse_kv_pairs("a"), chain::plugin_config_exception);

      // emptynot OK
      BOOST_REQUIRE_THROW(parse_kv_pairs(""), chain::plugin_config_exception);

   }

   BOOST_FIXTURE_TEST_CASE(abi_def_from_file_test, temp_file_fixture)
   {
      auto data_dir = std::filesystem::path(std::filesystem::temp_directory_path());
      auto good_json = std::string("{\"version\" : \"test string please ignore\"}");
      auto good_json_filename = create_temp_file(good_json);
      auto relative_json_filename = std::filesystem::path(good_json_filename).filename().generic_string();

      auto good_abi = chain::abi_def();
      good_abi.version = "test string please ignore";

      auto bad_json  = std::string("{{\"version\":oops\"}");
      auto bad_json_filename = create_temp_file(bad_json);
      fc::temp_directory tmp_dir;
      auto bad_filename = tmp_dir.path()/ "badbadbadbadbadbadbadbadbad";
      auto directory_name = std::filesystem::temp_directory_path().generic_string();

      // good cases
      BOOST_TEST( abi_def_from_file(good_json_filename, data_dir) == good_abi );
      BOOST_TEST( abi_def_from_file(relative_json_filename, data_dir) == good_abi );

      // bad cases
      BOOST_REQUIRE_THROW( abi_def_from_file(bad_json_filename, data_dir), chain::json_parse_exception );
      BOOST_REQUIRE_THROW( abi_def_from_file(bad_filename, data_dir), chain::plugin_config_exception );
      BOOST_REQUIRE_THROW( abi_def_from_file(directory_name, data_dir), chain::plugin_config_exception );
   }

BOOST_AUTO_TEST_SUITE_END()
